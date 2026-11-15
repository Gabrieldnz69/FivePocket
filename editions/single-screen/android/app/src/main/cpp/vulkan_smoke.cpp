#include "vulkan_smoke.h"

#include "smoke_runtime.h"
#include "native_rgba_surface.h"

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace recomp::bootstrap {
namespace {

std::string resultDiagnostic(const char* operation, VkResult result) {
    std::ostringstream message;
    message << operation << " failed (VkResult " << static_cast<int>(result) << ')';
    return message.str();
}

bool hasExtension(
        const std::vector<VkExtensionProperties>& extensions,
        const char* requiredName) {
    return std::any_of(
            extensions.begin(),
            extensions.end(),
            [requiredName](const VkExtensionProperties& extension) {
                return std::string(extension.extensionName) == requiredName;
            });
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preferences{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const auto preference : preferences) {
        if ((supported & preference) != 0U) {
            return preference;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

class VulkanSmokeRenderer::Impl final {
public:
    explicit Impl(SmokeRuntime& runtime) : runtime_(runtime) {}

    ~Impl() {
        std::lock_guard lock(mutex_);
        tearDown(false);
    }

    bool attachWindow(ANativeWindow* ownedWindow, int width, int height) {
        std::lock_guard lock(mutex_);
        tearDown(false);

        window_ = ownedWindow;
        if (window_ == nullptr) {
            runtime_.surfaceAttached(width, height);
            return fail("Android supplied a null native window", false);
        }
        width_ = width > 0 ? width : ANativeWindow_getWidth(window_);
        height_ = height > 0 ? height : ANativeWindow_getHeight(window_);
        runtime_.surfaceAttached(width_, height_);
        if (!createInstance() || !createSurface() || !selectPhysicalDevice()
                || !createDevice() || !createSwapchainResources()) {
            tearDown(false);
            return false;
        }

        std::ostringstream ready;
        ready << "Vulkan " << VK_VERSION_MAJOR(deviceApiVersion_) << '.'
              << VK_VERSION_MINOR(deviceApiVersion_) << " on " << deviceName_
              << "; swapchain ready";
        runtime_.rendererReady(ready.str());
        if (!presentFrame()) {
            tearDown(false);
            return false;
        }
        return true;
    }

    bool resize(int width, int height) {
        std::lock_guard lock(mutex_);
        width_ = width;
        height_ = height;

        if (window_ == nullptr || device_ == VK_NULL_HANDLE) {
            return false;
        }
        runtime_.surfaceAttached(width_, height_);

        vkDeviceWaitIdle(device_);
        destroySwapchainResources();
        if (!createSwapchainResources()) {
            return false;
        }
        runtime_.rendererReady("Vulkan swapchain recreated after surface resize");
        return presentFrame();
    }

    void detachWindow() {
        std::lock_guard lock(mutex_);
        tearDown(false);
        runtime_.surfaceDetached();
    }

    bool hasQueuedPresentation() const noexcept {return queuedPresentation_.load(std::memory_order_acquire);}
    std::uint64_t queuedPresentationCount() const noexcept {return queuedPresentationCount_.load(std::memory_order_acquire);}

    bool configureRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback) {
        std::lock_guard lock(mutex_);
#if defined(NDEBUG)
        if(rejectReadback)return false;
#endif
        if(window_ || (!rgba.empty() && rgba.size()!=gpu::NativeSwapImagePlan::PixelBytes))return false;
        if(rejectReadback && rgba.empty())return false;
        for(std::size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255)return false;
        sourceRgba_.swap(rgba);rejectRgbaReadback_=rejectReadback;return true;
    }

    bool updateRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback) {
        std::lock_guard lock(mutex_);
#if defined(NDEBUG)
        if(rejectReadback)return false;
#endif
        if(!window_ || device_==VK_NULL_HANDLE || swapchain_==VK_NULL_HANDLE || !rgbaMapping_ ||
            rgba.size()!=gpu::NativeSwapImagePlan::PixelBytes || sourceRgba_.empty())return false;
        for(std::size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255)return false;
        std::vector<std::uint8_t> packed;
        if(!gpu::packNativeRgbaSurface(rgba,extent_.width,extent_.height,
            surfaceFormat_.format==VK_FORMAT_B8G8R8A8_UNORM,packed) || packed.size()!=packedRgba_.size())return false;
        if(!checkResult("vkWaitForFences(RGBA update)",vkWaitForFences(device_,1,&frameFence_,VK_TRUE,5000000000ull)))return false;
        sourceRgba_=std::move(rgba);packedRgba_=std::move(packed);rejectRgbaReadback_=rejectReadback;
        std::memcpy(rgbaMapping_,packedRgba_.data(),packedRgba_.size());
        std::memset(static_cast<std::uint8_t*>(rgbaMapping_)+packedRgba_.size(),0xCD,packedRgba_.size());
        return presentFrame();
    }

private:
    bool fail(std::string diagnostic, bool unsupported) {
        if (unsupported) {
            runtime_.rendererUnsupported(std::move(diagnostic));
        } else {
            runtime_.rendererFailed(std::move(diagnostic));
        }
        return false;
    }

    bool checkResult(const char* operation, VkResult result) {
        return result == VK_SUCCESS || fail(resultDiagnostic(operation, result), false);
    }

    bool createInstance() {
        std::uint32_t extensionCount = 0;
        VkResult result = vkEnumerateInstanceExtensionProperties(
                nullptr, &extensionCount, nullptr);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkEnumerateInstanceExtensionProperties", result),
                        result == VK_ERROR_INCOMPATIBLE_DRIVER);
        }

        std::vector<VkExtensionProperties> extensions(extensionCount);
        result = vkEnumerateInstanceExtensionProperties(
                nullptr, &extensionCount, extensions.data());
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkEnumerateInstanceExtensionProperties", result), false);
        }
        if (!hasExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME)
                || !hasExtension(extensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
            return fail("Vulkan Android surface extensions are unavailable", true);
        }

        const std::array<const char*, 2> requiredExtensions{
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        };
        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Recomp Bootstrap";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        applicationInfo.pEngineName = "Asset-free Vulkan smoke";
        applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = requiredExtensions.size();
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();

        result = vkCreateInstance(&createInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateInstance", result),
                        result == VK_ERROR_INCOMPATIBLE_DRIVER);
        }
        return true;
    }

    bool createSurface() {
        VkAndroidSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = window_;
        return checkResult(
                "vkCreateAndroidSurfaceKHR",
                vkCreateAndroidSurfaceKHR(instance_, &createInfo, nullptr, &surface_));
    }

    bool selectPhysicalDevice() {
        std::uint32_t deviceCount = 0;
        VkResult result = vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkEnumeratePhysicalDevices", result), false);
        }
        if (deviceCount == 0) {
            return fail("No Vulkan physical device is available", true);
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        result = vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkEnumeratePhysicalDevices", result), false);
        }

        for (VkPhysicalDevice candidate : devices) {
            std::uint32_t extensionCount = 0;
            result = vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, nullptr);
            if (result != VK_SUCCESS) {
                continue;
            }
            std::vector<VkExtensionProperties> extensions(extensionCount);
            result = vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, extensions.data());
            if (result != VK_SUCCESS
                    || !hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                continue;
            }

            std::uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (std::uint32_t index = 0; index < queueCount; ++index) {
                VkBool32 supportsPresent = VK_FALSE;
                result = vkGetPhysicalDeviceSurfaceSupportKHR(
                        candidate, index, surface_, &supportsPresent);
                if (result == VK_SUCCESS
                        && supportsPresent == VK_TRUE
                        && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                    physicalDevice_ = candidate;
                    queueFamilyIndex_ = index;
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(candidate, &properties);
                    deviceName_ = properties.deviceName;
                    deviceApiVersion_ = properties.apiVersion;
                    return true;
                }
            }
        }

        return fail("No graphics queue supports presentation with VK_KHR_swapchain", true);
    }

    bool createDevice() {
        constexpr float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndex_;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        const std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.enabledExtensionCount = extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();

        const VkResult result = vkCreateDevice(
                physicalDevice_, &createInfo, nullptr, &device_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateDevice", result), false);
        }
        vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);
        return true;
    }

    bool createSwapchainResources() {
        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice_, surface_, &capabilities);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result),
                        false);
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0U) {
            return fail("Surface cannot be used as a Vulkan color attachment", true);
        }

        std::uint32_t formatCount = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice_, surface_, &formatCount, nullptr);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkGetPhysicalDeviceSurfaceFormatsKHR", result), false);
        }
        if (formatCount == 0) {
            return fail("Vulkan surface exposes no image formats", true);
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice_, surface_, &formatCount, formats.data());
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkGetPhysicalDeviceSurfaceFormatsKHR", result), false);
        }
        surfaceFormat_ = formats.front();
        if (surfaceFormat_.format == VK_FORMAT_UNDEFINED) {
            surfaceFormat_.format = VK_FORMAT_R8G8B8A8_UNORM;
            surfaceFormat_.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        }
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_UNORM
                    && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat_ = format;
                break;
            }
        }
        if(!sourceRgba_.empty() &&
            (surfaceFormat_.format!=VK_FORMAT_R8G8B8A8_UNORM && surfaceFormat_.format!=VK_FORMAT_B8G8R8A8_UNORM))
            return fail("RGBA diagnostic requires a native RGBA8/BGRA8 UNORM surface",true);
        constexpr VkImageUsageFlags transferUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if(!sourceRgba_.empty() && (capabilities.supportedUsageFlags&transferUsage)!=transferUsage)
            return fail("Surface lacks diagnostic upload/readback transfer usage",true);
        // This CPU-packed diagnostic is in logical window coordinates. Do not
        // claim it was pre-rotated just because currentTransform is rotated.
        // Let Android compose the identity image; a future optimized presenter
        // must explicitly rotate both its extent and content before opting out.
        if(!sourceRgba_.empty() && !(capabilities.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
            return fail("Diagnostic surface cannot compose an identity image",true);

        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            extent_ = capabilities.currentExtent;
        } else {
            const std::uint32_t requestedWidth = static_cast<std::uint32_t>(std::max(width_, 1));
            const std::uint32_t requestedHeight = static_cast<std::uint32_t>(std::max(height_, 1));
            extent_.width = std::clamp(
                    requestedWidth,
                    capabilities.minImageExtent.width,
                    capabilities.maxImageExtent.width);
            extent_.height = std::clamp(
                    requestedHeight,
                    capabilities.minImageExtent.height,
                    capabilities.maxImageExtent.height);
        }

        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount != 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat_.format;
        createInfo.imageColorSpace = surfaceFormat_.colorSpace;
        createInfo.imageExtent = extent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if(!sourceRgba_.empty())createInfo.imageUsage|=transferUsage;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        if(!sourceRgba_.empty())createInfo.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        createInfo.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;

        result = vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateSwapchainKHR", result), false);
        }

        result = vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
        if (result != VK_SUCCESS || imageCount == 0) {
            return fail(result == VK_SUCCESS
                            ? "Vulkan swapchain exposes no images"
                            : resultDiagnostic("vkGetSwapchainImagesKHR", result),
                        false);
        }
        images_.resize(imageCount);
        result = vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images_.data());
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkGetSwapchainImagesKHR", result), false);
        }

        if (!createImageViews() || !createRenderPass() || !createFramebuffers()
                || !createCommands() || !createSynchronization()) {
            return false;
        }
        return true;
    }

    bool createImageViews() {
        imageViews_.reserve(images_.size());
        for (const VkImage image : images_) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = image;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = surfaceFormat_.format;
            createInfo.components = {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY};
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            const VkResult result = vkCreateImageView(
                    device_, &createInfo, nullptr, &imageView);
            if (result != VK_SUCCESS) {
                return fail(resultDiagnostic("vkCreateImageView", result), false);
            }
            imageViews_.push_back(imageView);
        }
        return true;
    }

    bool createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = surfaceFormat_.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        return checkResult(
                "vkCreateRenderPass",
                vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_));
    }

    bool createFramebuffers() {
        framebuffers_.reserve(imageViews_.size());
        for (const VkImageView imageView : imageViews_) {
            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = renderPass_;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = &imageView;
            createInfo.width = extent_.width;
            createInfo.height = extent_.height;
            createInfo.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            const VkResult result = vkCreateFramebuffer(
                    device_, &createInfo, nullptr, &framebuffer);
            if (result != VK_SUCCESS) {
                return fail(resultDiagnostic("vkCreateFramebuffer", result), false);
            }
            framebuffers_.push_back(framebuffer);
        }
        return true;
    }

    bool createCommands() {
        if(!sourceRgba_.empty() && !createRgbaTransfer())return false;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex_;
        VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateCommandPool", result), false);
        }

        commandBuffers_.resize(framebuffers_.size());
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = commandBuffers_.size();
        result = vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers_.data());
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkAllocateCommandBuffers", result), false);
        }

        for (std::size_t index = 0; index < commandBuffers_.size(); ++index) {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            result = vkBeginCommandBuffer(commandBuffers_[index], &beginInfo);
            if (result != VK_SUCCESS) {
                return fail(resultDiagnostic("vkBeginCommandBuffer", result), false);
            }

            if(!sourceRgba_.empty()){
                recordRgbaTransfer(commandBuffers_[index],images_[index]);
                result=vkEndCommandBuffer(commandBuffers_[index]);
                if(result!=VK_SUCCESS)return fail(resultDiagnostic("vkEndCommandBuffer(RGBA)",result),false);
                continue;
            }

            VkClearValue clear{};
            clear.color.float32[0] = 0.025F;
            clear.color.float32[1] = 0.075F;
            clear.color.float32[2] = 0.16F;
            clear.color.float32[3] = 1.0F;

            VkRenderPassBeginInfo renderInfo{};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderInfo.renderPass = renderPass_;
            renderInfo.framebuffer = framebuffers_[index];
            renderInfo.renderArea.extent = extent_;
            renderInfo.clearValueCount = 1;
            renderInfo.pClearValues = &clear;
            vkCmdBeginRenderPass(
                    commandBuffers_[index], &renderInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(commandBuffers_[index]);
            result = vkEndCommandBuffer(commandBuffers_[index]);
            if (result != VK_SUCCESS) {
                return fail(resultDiagnostic("vkEndCommandBuffer", result), false);
            }
        }
        return true;
    }

    bool createRgbaTransfer() {
        if(!gpu::packNativeRgbaSurface(sourceRgba_,extent_.width,extent_.height,
            surfaceFormat_.format==VK_FORMAT_B8G8R8A8_UNORM,packedRgba_))return fail("RGBA surface packing rejected",false);
        VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size=packedRgba_.size()*2; // Disjoint upload and audited readback halves.
        buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(!checkResult("vkCreateBuffer(RGBA)",vkCreateBuffer(device_,&buffer,nullptr,&rgbaBuffer_)))return false;
        VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device_,rgbaBuffer_,&requirements);
        VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(physicalDevice_,&properties);
        std::uint32_t type=properties.memoryTypeCount;
        constexpr auto needed=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(std::uint32_t i=0;i<properties.memoryTypeCount;++i)
            if((requirements.memoryTypeBits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&needed)==needed){type=i;break;}
        if(type==properties.memoryTypeCount)return fail("No host-coherent diagnostic transfer memory",true);
        VkMemoryAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=type;
        if(!checkResult("vkAllocateMemory(RGBA)",vkAllocateMemory(device_,&allocate,nullptr,&rgbaMemory_)) ||
            !checkResult("vkBindBufferMemory(RGBA)",vkBindBufferMemory(device_,rgbaBuffer_,rgbaMemory_,0)) ||
            !checkResult("vkMapMemory(RGBA)",vkMapMemory(device_,rgbaMemory_,0,VK_WHOLE_SIZE,0,&rgbaMapping_)))return false;
        std::memcpy(rgbaMapping_,packedRgba_.data(),packedRgba_.size());
        std::memset(static_cast<std::uint8_t*>(rgbaMapping_)+packedRgba_.size(),0xCD,packedRgba_.size());
        return true;
    }

    void recordRgbaTransfer(VkCommandBuffer command,VkImage image) {
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={extent_.width,extent_.height,1};
        vkCmdCopyBufferToImage(command,rgbaBuffer_,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        copy.bufferOffset=packedRgba_.size();
        vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rgbaBuffer_,1,&copy);
        VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        host.buffer=rgbaBuffer_;host.offset=packedRgba_.size();host.size=packedRgba_.size();
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }

    bool createSynchronization() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkResult result = vkCreateSemaphore(
                device_, &semaphoreInfo, nullptr, &imageAvailable_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateSemaphore(image available)", result), false);
        }
        // Present waits are owned by acquired image index, not the CPU fence.
        // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
        renderFinished_.resize(images_.size(),VK_NULL_HANDLE);
        for(auto& semaphore:renderFinished_){
            result=vkCreateSemaphore(device_,&semaphoreInfo,nullptr,&semaphore);
            if(result!=VK_SUCCESS)return fail(resultDiagnostic("vkCreateSemaphore(render finished)",result),false);
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        result = vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkCreateFence", result), false);
        }
        return true;
    }

    bool presentFrame() {
        VkResult result = vkWaitForFences(
                device_, 1, &frameFence_, VK_TRUE, 5000000000ull);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkWaitForFences", result), false);
        }

        std::uint32_t imageIndex = 0;
        result = vkAcquireNextImageKHR(
                device_,
                swapchain_,
                5000000000ull,
                imageAvailable_,
                VK_NULL_HANDLE,
                &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return fail("Surface changed before the first Vulkan frame; resize will retry", false);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return fail(resultDiagnostic("vkAcquireNextImageKHR", result), false);
        }

        result = vkResetFences(device_, 1, &frameFence_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkResetFences", result), false);
        }

        const VkPipelineStageFlags waitStage = sourceRgba_.empty()?
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
        result = vkQueueSubmit(queue_, 1, &submitInfo, frameFence_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkQueueSubmit", result), false);
        }

        if(!sourceRgba_.empty()){
            if(!checkResult("vkWaitForFences(RGBA audit)",vkWaitForFences(device_,1,&frameFence_,VK_TRUE,5000000000ull)))return false;
            auto* readback=static_cast<std::uint8_t*>(rgbaMapping_)+packedRgba_.size();
            // Debug-only negative control AFTER the real GPU fence: alter one
            // owned readback byte, not the source or a guest buffer. This tests
            // the audit gate, not a driver/device failure or timeout.
            if(rejectRgbaReadback_)readback[0]^=1;
            if(std::memcmp(readback,packedRgba_.data(),packedRgba_.size()))return fail("RGBA GPU readback mismatch; not presented",false);
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(queue_, &presentInfo);
        const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return fail(resultDiagnostic("vkQueuePresentKHR", result), false);
        }

        queuedPresentation_.store(true,std::memory_order_release);
        queuedPresentationCount_.fetch_add(1,std::memory_order_release);
        result = vkQueueWaitIdle(queue_);
        if (result != VK_SUCCESS) {
            return fail(resultDiagnostic("vkQueueWaitIdle", result), false);
        }

        std::ostringstream message;
        if(sourceRgba_.empty())message << "Clear/present succeeded at ";
        else message << "RGBA diagnostic upload/readback exact (" << packedRgba_.size() << " bytes); present queued at ";
        message << extent_.width << 'x' << extent_.height;
        if (suboptimal) {
            message << " (suboptimal surface)";
        }
        runtime_.framePresented(message.str());
        return true;
    }

    void destroySwapchainResources() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        if (frameFence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frameFence_, nullptr);
            frameFence_ = VK_NULL_HANDLE;
        }
        for(auto semaphore:renderFinished_)if(semaphore!=VK_NULL_HANDLE)vkDestroySemaphore(device_,semaphore,nullptr);
        renderFinished_.clear();
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
            imageAvailable_ = VK_NULL_HANDLE;
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        commandBuffers_.clear();
        if(rgbaMapping_){vkUnmapMemory(device_,rgbaMemory_);rgbaMapping_=nullptr;}
        if(rgbaBuffer_!=VK_NULL_HANDLE){vkDestroyBuffer(device_,rgbaBuffer_,nullptr);rgbaBuffer_=VK_NULL_HANDLE;}
        if(rgbaMemory_!=VK_NULL_HANDLE){vkFreeMemory(device_,rgbaMemory_,nullptr);rgbaMemory_=VK_NULL_HANDLE;}
        packedRgba_.clear();
        for (const VkFramebuffer framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
        for (const VkImageView imageView : imageViews_) {
            vkDestroyImageView(device_, imageView, nullptr);
        }
        imageViews_.clear();
        images_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void tearDown(bool reportDetach) {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            destroySwapchainResources();
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        queue_ = VK_NULL_HANDLE;
        physicalDevice_ = VK_NULL_HANDLE;
        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        if (window_ != nullptr) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }
        if (reportDetach) {
            runtime_.surfaceDetached();
        }
    }

    SmokeRuntime& runtime_;
    std::mutex mutex_;
    ANativeWindow* window_{nullptr};
    int width_{0};
    int height_{0};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamilyIndex_{0};
    std::uint32_t deviceApiVersion_{VK_API_VERSION_1_0};
    std::string deviceName_{"unknown device"};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkSurfaceFormatKHR surfaceFormat_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkRenderPass renderPass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_{VK_NULL_HANDLE};
    std::vector<VkSemaphore> renderFinished_;
    VkFence frameFence_{VK_NULL_HANDLE};
    std::vector<std::uint8_t> sourceRgba_,packedRgba_;
    VkBuffer rgbaBuffer_{VK_NULL_HANDLE};
    VkDeviceMemory rgbaMemory_{VK_NULL_HANDLE};
    void* rgbaMapping_{nullptr};
    bool rejectRgbaReadback_{false};
    std::atomic<bool> queuedPresentation_{false};
    std::atomic<std::uint64_t> queuedPresentationCount_{0};
};

VulkanSmokeRenderer::VulkanSmokeRenderer(SmokeRuntime& runtime)
    : implementation_(std::make_unique<Impl>(runtime)) {}

VulkanSmokeRenderer::~VulkanSmokeRenderer() = default;

bool VulkanSmokeRenderer::attachWindow(
        ANativeWindow* ownedWindow, int width, int height) {
    return implementation_->attachWindow(ownedWindow, width, height);
}

bool VulkanSmokeRenderer::resize(int width, int height) {
    return implementation_->resize(width, height);
}

void VulkanSmokeRenderer::detachWindow() {
    implementation_->detachWindow();
}

bool VulkanSmokeRenderer::configureRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback) {
    return implementation_->configureRgbaDiagnostic(std::move(rgba),rejectReadback);
}
bool VulkanSmokeRenderer::hasQueuedPresentation() const noexcept {
    return implementation_->hasQueuedPresentation();
}
bool VulkanSmokeRenderer::updateRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback) {
    return implementation_->updateRgbaDiagnostic(std::move(rgba),rejectReadback);
}
std::uint64_t VulkanSmokeRenderer::queuedPresentationCount() const noexcept {
    return implementation_->queuedPresentationCount();
}

} // namespace recomp::bootstrap
