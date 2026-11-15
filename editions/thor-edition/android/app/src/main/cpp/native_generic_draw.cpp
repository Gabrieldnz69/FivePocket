#include "native_generic_draw.h"
#include <algorithm>
#include <functional>

namespace recomp::gpu {
namespace {
constexpr unsigned HeapSize=16;
struct Cleanup {
    std::vector<std::function<void()>> steps;
    ~Cleanup(){for(auto it=steps.rbegin();it!=steps.rend();++it)(*it)();}
    void add(std::function<void()> f){steps.push_back(std::move(f));}
};
std::optional<std::uint32_t> memoryType(ProbeDevice& device,std::uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(device.physical,&p);
    for(std::uint32_t i=0;i<p.memoryTypeCount;++i)
        if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    return std::nullopt;
}
struct Buffer {VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};};
bool makeBuffer(ProbeDevice& device,Cleanup& c,VkDeviceSize size,VkBufferUsageFlags usage,Buffer& out) {
    const auto d=device.device;
    VkBufferCreateInfo b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;b.size=std::max<VkDeviceSize>(size,16);b.usage=usage;
    if(vkCreateBuffer(d,&b,nullptr,&out.buffer)!=VK_SUCCESS)return false;
    c.add([d,x=out.buffer]{vkDestroyBuffer(d,x,nullptr);});
    VkMemoryRequirements r{};vkGetBufferMemoryRequirements(d,out.buffer,&r);
    const auto type=memoryType(device,r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!type)return false;
    VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=r.size;a.memoryTypeIndex=*type;
    if(vkAllocateMemory(d,&a,nullptr,&out.memory)!=VK_SUCCESS)return false;
    c.add([d,x=out.memory]{vkFreeMemory(d,x,nullptr);});
    if(vkBindBufferMemory(d,out.buffer,out.memory,0)!=VK_SUCCESS || vkMapMemory(d,out.memory,0,r.size,0,&out.mapped)!=VK_SUCCESS)return false;
    return true;
}
struct Image {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
bool makeImage(ProbeDevice& device,Cleanup& c,unsigned w,unsigned h,VkFormat format,VkImageUsageFlags usage,
               const VkComponentMapping& swizzle,Image& out) {
    const auto d=device.device;
    VkImageCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;i.imageType=VK_IMAGE_TYPE_2D;i.format=format;
    i.extent={w,h,1};i.mipLevels=1;i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;
    i.usage=usage;i.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(d,&i,nullptr,&out.image)!=VK_SUCCESS)return false;
    c.add([d,x=out.image]{vkDestroyImage(d,x,nullptr);});
    VkMemoryRequirements r{};vkGetImageMemoryRequirements(d,out.image,&r);
    const auto type=memoryType(device,r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(!type)return false;
    VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=r.size;a.memoryTypeIndex=*type;
    if(vkAllocateMemory(d,&a,nullptr,&out.memory)!=VK_SUCCESS)return false;
    c.add([d,x=out.memory]{vkFreeMemory(d,x,nullptr);});
    if(vkBindImageMemory(d,out.image,out.memory,0)!=VK_SUCCESS)return false;
    VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=out.image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;
    v.format=format;v.components=swizzle;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(vkCreateImageView(d,&v,nullptr,&out.view)!=VK_SUCCESS)return false;
    c.add([d,x=out.view]{vkDestroyImageView(d,x,nullptr);});
    return true;
}
void barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst,
             VkPipelineStageFlags srcStage,VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=from;b.newLayout=to;b.srcAccessMask=src;b.dstAccessMask=dst;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cmd,srcStage,dstStage,0,0,nullptr,0,nullptr,1,&b);
}
}

std::optional<std::vector<std::uint8_t>> renderGenericDraw(ProbeDevice& device,GenericDraw& draw,const char** why) {
    const auto fail=[&](const char* s){if(why)*why=s;return std::optional<std::vector<std::uint8_t>>{};};
    if(draw.vertexCode.empty() || draw.pixelCode.empty() || !draw.width || !draw.height || !draw.stride || !draw.vertexCount ||
       draw.vertices.size()<std::size_t{draw.stride}*draw.vertexCount || draw.vertexConstants.size()!=4096 ||
       draw.pixelConstants.size()!=4096 || draw.sharedConstants.size()!=512 || draw.textures.size()>HeapSize)return fail("request");
    const auto d=device.device;Cleanup c;
    // Descriptor set layouts: sets 0-2 sampled-image heaps, 3 sampler heap, 4 constants.
    std::array<VkDescriptorSetLayout,5> layouts{};
    for(unsigned s=0;s<5;++s) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        if(s<3)bindings.push_back({0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,HeapSize,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        else if(s==3)bindings.push_back({0,VK_DESCRIPTOR_TYPE_SAMPLER,HeapSize,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        else for(unsigned b=0;b<3;++b)bindings.push_back({b,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        VkDescriptorSetLayoutCreateInfo l{};l.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        l.bindingCount=static_cast<std::uint32_t>(bindings.size());l.pBindings=bindings.data();
        if(vkCreateDescriptorSetLayout(d,&l,nullptr,&layouts[s])!=VK_SUCCESS)return fail("set-layout");
        c.add([d,x=layouts[s]]{vkDestroyDescriptorSetLayout(d,x,nullptr);});
    }
    VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=5;pl.pSetLayouts=layouts.data();
    VkPipelineLayout pipelineLayout{};
    if(vkCreatePipelineLayout(d,&pl,nullptr,&pipelineLayout)!=VK_SUCCESS)return fail("pipeline-layout");
    c.add([d,pipelineLayout]{vkDestroyPipelineLayout(d,pipelineLayout,nullptr);});
    std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3*HeapSize},{VK_DESCRIPTOR_TYPE_SAMPLER,HeapSize},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3}}};
    VkDescriptorPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pi.maxSets=5;pi.poolSizeCount=3;pi.pPoolSizes=sizes.data();
    VkDescriptorPool pool{};
    if(vkCreateDescriptorPool(d,&pi,nullptr,&pool)!=VK_SUCCESS)return fail("descriptor-pool");
    c.add([d,pool]{vkDestroyDescriptorPool(d,pool,nullptr);});
    std::array<VkDescriptorSet,5> sets{};
    VkDescriptorSetAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=pool;ai.descriptorSetCount=5;ai.pSetLayouts=layouts.data();
    if(vkAllocateDescriptorSets(d,&ai,sets.data())!=VK_SUCCESS)return fail("descriptor-sets");

    VkCommandPoolCreateInfo cp{};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.queueFamilyIndex=device.queueFamily;
    VkCommandPool commandPool{};
    if(vkCreateCommandPool(d,&cp,nullptr,&commandPool)!=VK_SUCCESS)return fail("command-pool");
    c.add([d,commandPool]{vkDestroyCommandPool(d,commandPool,nullptr);});
    VkCommandBufferAllocateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;cb.commandPool=commandPool;cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb.commandBufferCount=1;
    VkCommandBuffer cmd{};
    if(vkAllocateCommandBuffers(d,&cb,&cmd)!=VK_SUCCESS)return fail("command-buffer");
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd,&begin);

    // Textures (heap slot i) plus one black dummy filling the rest of every heap.
    GenericTexture dummy;dummy.pixels.assign(4,0);
    std::vector<Image> images;std::vector<Buffer> staging;images.reserve(HeapSize+1);staging.reserve(HeapSize+1);
    std::vector<VkSampler> samplers;
    const auto upload=[&](const GenericTexture& t)->bool {
        images.emplace_back();staging.emplace_back();
        if(!makeImage(device,c,t.width,t.height,t.format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,t.swizzle,images.back()) ||
           !makeBuffer(device,c,t.pixels.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,staging.back()))return false;
        std::memcpy(staging.back().mapped,t.pixels.data(),t.pixels.size());
        barrier(cmd,images.back().image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={t.width,t.height,1};
        vkCmdCopyBufferToImage(cmd,staging.back().buffer,images.back().image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier(cmd,images.back().image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        VkSamplerCreateInfo s{};s.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;s.magFilter=s.minFilter=t.filter;
        s.addressModeU=s.addressModeV=s.addressModeW=t.address;s.maxLod=0;
        VkSampler sampler{};if(vkCreateSampler(d,&s,nullptr,&sampler)!=VK_SUCCESS)return false;
        c.add([d,sampler]{vkDestroySampler(d,sampler,nullptr);});samplers.push_back(sampler);
        return true;
    };
    if(!upload(dummy))return fail("dummy-texture");
    for(const auto& t:draw.textures) {
        if(t.reg>=16 || t.pixels.empty())return fail("texture-request");
        if(!upload(t))return fail("texture-upload");
        const std::uint32_t slot=static_cast<std::uint32_t>(images.size()-1); // heap index
        for(unsigned b=0;b<4;++b){draw.sharedConstants[t.reg*4+b]=std::uint8_t(slot>>(8*b));draw.sharedConstants[192+t.reg*4+b]=std::uint8_t(slot>>(8*b));}
    }
    std::vector<VkDescriptorImageInfo> heap(HeapSize),samplerHeap(HeapSize);
    for(unsigned i=0;i<HeapSize;++i) {
        const unsigned source=i<images.size()?i:0;
        heap[i]={VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        samplerHeap[i]={samplers[source],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
    }
    std::array<Buffer,3> constants{};
    const std::array<const std::vector<std::uint8_t>*,3> constantData{&draw.vertexConstants,&draw.pixelConstants,&draw.sharedConstants};
    for(unsigned b=0;b<3;++b) {
        if(!makeBuffer(device,c,constantData[b]->size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,constants[b]))return fail("constant-buffer");
        std::memcpy(constants[b].mapped,constantData[b]->data(),constantData[b]->size());
    }
    std::vector<VkWriteDescriptorSet> writes;std::array<VkDescriptorBufferInfo,3> bufferInfo{};
    for(unsigned s=0;s<3;++s) {
        VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[s];w.dstBinding=0;w.descriptorCount=HeapSize;
        w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w.pImageInfo=heap.data();writes.push_back(w);
    }
    {VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[3];w.dstBinding=0;w.descriptorCount=HeapSize;
     w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;w.pImageInfo=samplerHeap.data();writes.push_back(w);}
    for(unsigned b=0;b<3;++b) {
        bufferInfo[b]={constants[b].buffer,0,VK_WHOLE_SIZE};
        VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[4];w.dstBinding=b;w.descriptorCount=1;
        w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&bufferInfo[b];writes.push_back(w);
    }
    vkUpdateDescriptorSets(d,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);

    // Target, render pass, framebuffer.
    Image target;
    if(!makeImage(device,c,draw.width,draw.height,draw.target,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY},target))
        return fail("target");
    VkAttachmentDescription att{};att.format=draw.target;att.samples=VK_SAMPLE_COUNT_1_BIT;att.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp=VK_ATTACHMENT_STORE_OP_STORE;att.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;att.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;att.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=1;rp.pAttachments=&att;rp.subpassCount=1;rp.pSubpasses=&sub;
    VkRenderPass pass{};if(vkCreateRenderPass(d,&rp,nullptr,&pass)!=VK_SUCCESS)return fail("render-pass");
    c.add([d,pass]{vkDestroyRenderPass(d,pass,nullptr);});
    VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=pass;fb.attachmentCount=1;fb.pAttachments=&target.view;
    fb.width=draw.width;fb.height=draw.height;fb.layers=1;
    VkFramebuffer framebuffer{};if(vkCreateFramebuffer(d,&fb,nullptr,&framebuffer)!=VK_SUCCESS)return fail("framebuffer");
    c.add([d,framebuffer]{vkDestroyFramebuffer(d,framebuffer,nullptr);});

    // Pipeline.
    std::array<VkShaderModule,2> modules{};
    for(unsigned s=0;s<2;++s) {
        const auto& code=s?draw.pixelCode:draw.vertexCode;
        VkShaderModuleCreateInfo m{};m.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;m.codeSize=code.size()*4;m.pCode=code.data();
        if(vkCreateShaderModule(d,&m,nullptr,&modules[s])!=VK_SUCCESS)return fail("shader-module");
        c.add([d,x=modules[s]]{vkDestroyShaderModule(d,x,nullptr);});
    }
    VkSpecializationMapEntry specEntry{0,0,4};VkSpecializationInfo spec{1,&specEntry,4,&draw.specFlags};
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    for(unsigned s=0;s<2;++s) {
        stages[s].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[s].stage=s?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[s].module=modules[s];stages[s].pName="shaderMain";stages[s].pSpecializationInfo=&spec;
    }
    VkVertexInputBindingDescription binding{0,draw.stride,VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for(const auto& a:draw.attributes)attributes.push_back({a.location,0,a.format,a.offset});
    VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&binding;
    vi.vertexAttributeDescriptionCount=static_cast<std::uint32_t>(attributes.size());vi.pVertexAttributeDescriptions=attributes.data();
    VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0,0,float(draw.width),float(draw.height),0,1};VkRect2D scissor{{0,0},{draw.width,draw.height}};
    VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=1;vp.pViewports=&viewport;vp.scissorCount=1;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo rs{};rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;rs.polygonMode=VK_POLYGON_MODE_FILL;
    rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_CLOCKWISE;rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=0xF;
    VkPipelineColorBlendStateCreateInfo cbs{};cbs.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cbs.attachmentCount=1;cbs.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo gp{};gp.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;gp.stageCount=2;gp.pStages=stages.data();
    gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;gp.pViewportState=&vp;gp.pRasterizationState=&rs;gp.pMultisampleState=&ms;
    gp.pColorBlendState=&cbs;gp.layout=pipelineLayout;gp.renderPass=pass;
    VkPipeline pipeline{};
    if(vkCreateGraphicsPipelines(d,VK_NULL_HANDLE,1,&gp,nullptr,&pipeline)!=VK_SUCCESS)return fail("pipeline");
    c.add([d,pipeline]{vkDestroyPipeline(d,pipeline,nullptr);});

    Buffer vertexBuffer,readback;
    if(!makeBuffer(device,c,draw.vertices.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertexBuffer))return fail("vertex-buffer");
    std::memcpy(vertexBuffer.mapped,draw.vertices.data(),draw.vertices.size());
    const VkDeviceSize outputBytes=VkDeviceSize{draw.width}*draw.height*draw.targetBytesPerPixel;
    if(!makeBuffer(device,c,outputBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,readback))return fail("readback");

    VkClearValue clear{};
    VkRenderPassBeginInfo rb{};rb.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rb.renderPass=pass;rb.framebuffer=framebuffer;
    rb.renderArea={{0,0},{draw.width,draw.height}};rb.clearValueCount=1;rb.pClearValues=&clear;
    vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,5,sets.data(),0,nullptr);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(cmd,0,1,&vertexBuffer.buffer,&offset);
    vkCmdDraw(cmd,draw.vertexCount,1,0,0);
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={draw.width,draw.height,1};
    vkCmdCopyImageToBuffer(cmd,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;VkFence fence{};
    if(vkCreateFence(d,&fi,nullptr,&fence)!=VK_SUCCESS)return fail("fence");
    c.add([d,fence]{vkDestroyFence(d,fence,nullptr);});
    VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    if(vkQueueSubmit(device.queue,1,&submit,fence)!=VK_SUCCESS || vkWaitForFences(d,1,&fence,VK_TRUE,5'000'000'000ull)!=VK_SUCCESS)
        return fail("submit");
    std::vector<std::uint8_t> out(outputBytes);std::memcpy(out.data(),readback.mapped,out.size());
    if(why)*why="generic-draw-rendered";
    return out;
}
}
