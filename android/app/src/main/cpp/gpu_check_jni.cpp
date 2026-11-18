// The launcher's look at the graphics chip (FivePocket bug report 2026-11-07: a Retroid Pocket 5, Adreno 650, Android 13,
// "won't open"). The game's renderer refuses a device it cannot drive (tools/vulkan_probe/device.cpp create(), the
// engine's initialize()), and that refusal reached the player only as "The game has stopped": the reasons are printed to
// the game's standard output, which never reaches logcat, and the descriptor-set limit is refused without any line.
//
// This library answers one question for the launcher, in its own process and before any game code is loaded: what does
// Vulkan device 0 (the device the game takes: "use device zero, no heuristic GPU choice") report for each thing the game's
// device gate asks? It only reads. It enables nothing, creates no logical device, and keeps nothing: the instance is
// destroyed before the call returns. The rule that turns these facts into "supported or not" is GpuSupport.java (pure
// Java, host-tested); this file only reports, in one line of key=value pairs whose keys match the game's own
// VULKAN_PROBE_V1 / VULKAN_CORE_CAPS_V1 lines, with the device name last because it may contain spaces.
//
//   GPU_PROBE_V1 api=1.1.128 driver=512.615 vendor=20803 device_id=100990976 descriptor_indexing=1
//       runtimeDescriptorArray=1 sampledImageArrayDynamicIndexing=1 shaderClipDistance=1 fragmentStoresAndAtomics=1
//       vertexPipelineStoresAndAtomics=1 samplerAnisotropy=1 sampleRateShading=1 independentBlend=1
//       maxBoundDescriptorSets=4 device=Adreno (TM) 650
//   GPU_PROBE_V1 error=<what failed>
#include <jni.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string probe() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "FivePocket graphics check";
    app.apiVersion = VK_API_VERSION_1_1;   // vkGetPhysicalDeviceFeatures2 is core in 1.1; the game asks for 1.2
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult created = vkCreateInstance(&info, nullptr, &instance);
    if (created != VK_SUCCESS) {
        return "GPU_PROBE_V1 error=create-instance-" + std::to_string(static_cast<int>(created));
    }
    std::string line;
    uint32_t count = 0;
    std::vector<VkPhysicalDevice> devices;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0 || count > 16) {
        line = "GPU_PROBE_V1 error=no-device";
    } else {
        devices.resize(count);
        if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS || count == 0) {
            line = "GPU_PROBE_V1 error=no-device";
        }
    }
    if (line.empty()) {
        const VkPhysicalDevice physical = devices[0];
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        bool descriptorIndexing = false;
        uint32_t n = 0;
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr) == VK_SUCCESS && n && n < 4096) {
            std::vector<VkExtensionProperties> all(n);
            if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, all.data()) == VK_SUCCESS) {
                for (uint32_t i = 0; i < n && i < all.size(); ++i) {
                    if (std::strcmp(all[i].extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
                        descriptorIndexing = true;
                    }
                }
            }
        }
        // vkGetPhysicalDeviceFeatures2 is core only from Vulkan 1.1: a 1.0 device is asked with the 1.0 query (no
        // chained structures), and its runtime arrays are then reported as 0 (the game needs 1.1 anyway).
        const bool device11 = properties.apiVersion >= VK_API_VERSION_1_1;
        const auto features2 = device11 ? reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2")) : nullptr;
        if (device11 && !features2) {
            line = "GPU_PROBE_V1 error=no-features2";
        } else {
            // The indexing structure is chained only where the driver knows it (core in 1.2, else the extension).
            const bool indexingKnown = properties.apiVersion >= VK_API_VERSION_1_2 || descriptorIndexing;
            VkPhysicalDeviceDescriptorIndexingFeatures indexing{};
            indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
            VkPhysicalDeviceFeatures2 all{};
            all.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            all.pNext = indexingKnown ? &indexing : nullptr;
            if (features2) features2(physical, &all);
            else vkGetPhysicalDeviceFeatures(physical, &all.features);
            const VkPhysicalDeviceFeatures& f = all.features;
            char text[768];
            std::snprintf(text, sizeof text,
                    "GPU_PROBE_V1 api=%u.%u.%u driver=%u vendor=%u device_id=%u descriptor_indexing=%u "
                    "runtimeDescriptorArray=%u sampledImageArrayDynamicIndexing=%u shaderClipDistance=%u "
                    "fragmentStoresAndAtomics=%u vertexPipelineStoresAndAtomics=%u samplerAnisotropy=%u "
                    "sampleRateShading=%u independentBlend=%u maxBoundDescriptorSets=%u device=%.*s",
                    VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                    VK_VERSION_PATCH(properties.apiVersion), properties.driverVersion, properties.vendorID,
                    properties.deviceID, unsigned(descriptorIndexing), unsigned(indexing.runtimeDescriptorArray),
                    unsigned(f.shaderSampledImageArrayDynamicIndexing), unsigned(f.shaderClipDistance),
                    unsigned(f.fragmentStoresAndAtomics), unsigned(f.vertexPipelineStoresAndAtomics),
                    unsigned(f.samplerAnisotropy), unsigned(f.sampleRateShading), unsigned(f.independentBlend),
                    properties.limits.maxBoundDescriptorSets, int(VK_MAX_PHYSICAL_DEVICE_NAME_SIZE),
                    properties.deviceName);
            line = text;
            // A device name is the driver's text: keep the line one line of printable characters.
            for (char& c : line) {
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = '?';
            }
        }
    }
    vkDestroyInstance(instance, nullptr);
    return line;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_recompbootstrap_smoke_GpuCheck_probe(JNIEnv* env, jclass) {
    std::string line;
    try {
        line = probe();
    } catch (...) {
        line = "GPU_PROBE_V1 error=exception";
    }
    // One line in logcat, so that a player's log names the chip and every fact the game's gate asks about.
    __android_log_print(ANDROID_LOG_INFO, "FivePocketGpu", "%s", line.c_str());
    return env->NewStringUTF(line.c_str());
}

#if defined(FIVEPOCKET_GPU_CHECK_STANDALONE)
// Host-built device check only (never in the app): the same probe as a command-line program, so that a phone's real
// driver answer can be read with adb without installing or changing any app.
int main() {
    std::puts(probe().c_str());
    return 0;
}
#endif
