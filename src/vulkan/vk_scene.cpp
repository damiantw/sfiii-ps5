#include "vk_scene.h"
#include <algorithm>
#include <cstring>

namespace sf3ps5 {
namespace {
#include "fullscreen_vert.inc"
#define VK(x, what) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { why = std::string(what) + " failed (" + std::to_string(int(r_)) + ")"; return false; } } while (0)
}

uint32_t VkScene::memory_type(uint32_t bits, VkMemoryPropertyFlags want) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

bool VkScene::init(std::string& why, bool display) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "sfiii-ps5", app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    const char* iext[] = {"VK_KHR_surface", "VK_KHR_display"};
    if (display) ii.enabledExtensionCount = 2, ii.ppEnabledExtensionNames = iext;
    VK(vkCreateInstance(&ii, nullptr, &inst_), "vkCreateInstance");
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    if (!n) return why = "no Vulkan device", false;
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst_, &n, devs.data());
    phys_ = devs[0];
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(phys_, &pp);
    name_ = pp.deviceName;
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qf.data());
    family_ = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family_ = i; break; }
    if (family_ == UINT32_MAX) return why = "no graphics queue", false;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_, qi.queueCount = 1, qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1, di.pQueueCreateInfos = &qi;
    const char* dext[] = {"VK_KHR_swapchain"};
    if (display) di.enabledExtensionCount = 1, di.ppEnabledExtensionNames = dext;
    VK(vkCreateDevice(phys_, &di, nullptr, &dev_), "vkCreateDevice");
    vkGetDeviceQueue(dev_, family_, 0, &queue_);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, pi.queueFamilyIndex = family_;
    VK(vkCreateCommandPool(dev_, &pi, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_, ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, ai.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(dev_, &ai, &cmd_), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK(vkCreateFence(dev_, &fi, nullptr, &fence_), "vkCreateFence");

    // SDL_GPU's SPIR-V fragment layout: set 2 = sampler, storage buffers; set 3 = uniforms
    VkDescriptorSetLayoutBinding b2[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutBinding b3 = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VK(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &empty_), "empty set layout");
    li.bindingCount = 3, li.pBindings = b2;
    VK(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &set2_), "set 2 layout");
    li.bindingCount = 1, li.pBindings = &b3;
    VK(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &set3_), "set 3 layout");
    VkDescriptorSetLayout sets[4] = {empty_, empty_, set2_, set3_};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 4, pli.pSetLayouts = sets;
    VK(vkCreatePipelineLayout(dev_, &pli, nullptr, &layout_), "pipeline layout");
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 3, dpi.poolSizeCount = 3, dpi.pPoolSizes = ps;
    VK(vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_), "descriptor pool");
    VkDescriptorSetLayout l2[2] = {set2_, set2_};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = dpool_, dai.descriptorSetCount = 2, dai.pSetLayouts = l2;
    VK(vkAllocateDescriptorSets(dev_, &dai, ds2_), "descriptor sets");
    dai.descriptorSetCount = 1, dai.pSetLayouts = &set3_;
    VK(vkAllocateDescriptorSets(dev_, &dai, &ds3_), "descriptor set 3");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;   // SDL_SCALEMODE_LINEAR on output_
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK(vkCreateSampler(dev_, &si, nullptr, &sampler_), "sampler");
    if (!make_buffer(ubo_, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, why)) return false;
    VkDescriptorBufferInfo ub{ubo_.buf, 0, 16};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = ds3_, w.dstBinding = 0, w.descriptorCount = 1, w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, w.pBufferInfo = &ub;
    vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
    // the white texel the geometry pass "draws" (gpu-renderer.cpp's white_)
    if (!make_image(white_, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, why)) return false;
    Buffer up;
    if (!make_buffer(up, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, why)) return false;
    std::memset(up.map, 0xFF, 4);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd_, &bi);
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, ib.image = white_.img;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, cp.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd_, up.buf, white_.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    vkEndCommandBuffer(cmd_);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1, sub.pCommandBuffers = &cmd_;
    VK(vkQueueSubmit(queue_, 1, &sub, fence_), "upload white");
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_, 1, &fence_);
    free_buffer(up);
    VkAttachmentDescription at{};
    at.format = VK_FORMAT_R8G8B8A8_UNORM, at.samples = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, at.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sp.colorAttachmentCount = 1, sp.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1, rpi.pAttachments = &at, rpi.subpassCount = 1, rpi.pSubpasses = &sp;
    VK(vkCreateRenderPass(dev_, &rpi, nullptr, &pass_), "render pass");
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = sizeof kFullscreenVert, mi.pCode = kFullscreenVert;
    VK(vkCreateShaderModule(dev_, &mi, nullptr, &vert_), "vertex shader");
    return true;
}

bool VkScene::make_buffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, std::string& why) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size, bi.usage = usage;
    VK(vkCreateBuffer(dev_, &bi, nullptr, &b.buf), "vkCreateBuffer");
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev_, b.buf, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return why = "no host-visible memory", false;
    VK(vkAllocateMemory(dev_, &ai, nullptr, &b.mem), "vkAllocateMemory");
    vkBindBufferMemory(dev_, b.buf, b.mem, 0);
    VK(vkMapMemory(dev_, b.mem, 0, size, 0, &b.map), "vkMapMemory");
    b.size = size;
    return true;
}

void VkScene::free_buffer(Buffer& b) {
    if (b.buf) vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(dev_, b.mem, nullptr);
    b = {};
}

bool VkScene::make_image(Image& im, int w, int h, VkImageUsageFlags usage, std::string& why) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D, ii.format = VK_FORMAT_R8G8B8A8_UNORM, ii.extent = {uint32_t(w), uint32_t(h), 1};
    ii.mipLevels = ii.arrayLayers = 1, ii.samples = VK_SAMPLE_COUNT_1_BIT, ii.tiling = VK_IMAGE_TILING_OPTIMAL, ii.usage = usage;
    VK(vkCreateImage(dev_, &ii, nullptr, &im.img), "vkCreateImage");
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev_, im.img, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size, ai.memoryTypeIndex = memory_type(mr.memoryTypeBits, 0);
    VK(vkAllocateMemory(dev_, &ai, nullptr, &im.mem), "image memory");
    vkBindImageMemory(dev_, im.img, im.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img, vi.viewType = VK_IMAGE_VIEW_TYPE_2D, vi.format = ii.format, vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(dev_, &vi, nullptr, &im.view), "image view");
    im.w = w, im.h = h;
    return true;
}

void VkScene::free_image(Image& im) {
    if (im.view) vkDestroyImageView(dev_, im.view, nullptr);
    if (im.img) vkDestroyImage(dev_, im.img, nullptr);
    if (im.mem) vkFreeMemory(dev_, im.mem, nullptr);
    im = {};
}

bool VkScene::load_fragment(const std::vector<uint32_t>& spirv, std::string& why) {
    if (frag_) vkDestroyShaderModule(dev_, frag_, nullptr), frag_ = VK_NULL_HANDLE;
    if (pipe_) vkDestroyPipeline(dev_, pipe_, nullptr), pipe_ = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = spirv.size() * 4, mi.pCode = spirv.data();
    VK(vkCreateShaderModule(dev_, &mi, nullptr, &frag_), "fragment shader");
    return ensure_pipeline(why);
}

bool VkScene::ensure_pipeline(std::string& why) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT, st[0].module = vert_, st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, st[1].module = frag_, st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.cullMode = VK_CULL_MODE_NONE, rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;                                 // SDL_BLENDMODE_NONE
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1, cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2, ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = 2, pi.pStages = st, pi.pVertexInputState = &vin, pi.pInputAssemblyState = &ia, pi.pViewportState = &vp;
    pi.pRasterizationState = &rs, pi.pMultisampleState = &ms, pi.pColorBlendState = &cb, pi.pDynamicState = &ds;
    pi.layout = layout_, pi.renderPass = pass_;
    VK(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe_), "graphics pipeline");
    return true;
}

bool VkScene::upload(const std::vector<uint32_t>& scene, const std::vector<uint32_t>& textures, std::string& why) {
    vkDeviceWaitIdle(dev_);                                   // simple for now; the PS5 path ring-buffers per frame in flight
    auto fit = [&](Buffer& b, const std::vector<uint32_t>& v) {
        VkDeviceSize need = std::max<VkDeviceSize>(16, v.size() * 4);
        if (b.size < need) { free_buffer(b); if (!make_buffer(b, need * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, why)) return false; }
        std::memcpy(b.map, v.data(), v.size() * 4);
        return true;
    };
    if (!fit(scene_, scene) || !fit(textures_, textures)) return false;
    for (int i = 0; i < 2; ++i) {
        VkDescriptorBufferInfo bi[2] = {{scene_.buf, 0, VK_WHOLE_SIZE}, {textures_.buf, 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = ds2_[i], w.dstBinding = 1, w.descriptorCount = 2, w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w.pBufferInfo = bi;
        vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
    }
    return true;
}

bool VkScene::ensure_targets(int w, int h, std::string& why) {
    if (target_[0].w == w && target_[0].h == h) return true;
    vkDeviceWaitIdle(dev_);
    for (int i = 0; i < 2; ++i) {
        if (fb_[i]) vkDestroyFramebuffer(dev_, fb_[i], nullptr), fb_[i] = VK_NULL_HANDLE;
        free_image(target_[i]);
        if (!make_image(target_[i], w, h, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, why)) return false;
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = pass_, fi.attachmentCount = 1, fi.pAttachments = &target_[i].view, fi.width = w, fi.height = h, fi.layers = 1;
        VK(vkCreateFramebuffer(dev_, &fi, nullptr, &fb_[i]), "framebuffer");
    }
    free_buffer(readback_);
    return make_buffer(readback_, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, why);
}

bool VkScene::draw(int w, int h, uint32_t aa_pass, bool picture_is_last, std::string& why) {
    if (!pipe_ || !scene_.buf) return why = "no shader or no upload", false;
    if (!ensure_targets(w, h, why)) return false;
    int src = cur_, dst = picture_is_last ? 1 - cur_ : cur_;
    if (picture_is_last && !target_[src].img) return why = "no previous pass", false;
    uint32_t pass[4] = {aa_pass, 0, 0, 0};
    std::memcpy(ubo_.map, pass, sizeof pass);
    VkDescriptorImageInfo ii{sampler_, picture_is_last ? target_[src].view : white_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstSet = ds2_[dst], wds.dstBinding = 0, wds.descriptorCount = 1, wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, wds.pImageInfo = &ii;
    vkUpdateDescriptorSets(dev_, 1, &wds, 0, nullptr);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = pass_, rp.framebuffer = fb_[dst], rp.renderArea = {{0, 0}, {uint32_t(w), uint32_t(h)}};
    vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_);
    VkDescriptorSet sets[2] = {ds2_[dst], ds3_};
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 2, 2, sets, 0, nullptr);
    VkViewport v{0, 0, float(w), float(h), 0, 1};
    VkRect2D sc{{0, 0}, {uint32_t(w), uint32_t(h)}};
    vkCmdSetViewport(cmd_, 0, 1, &v);
    vkCmdSetScissor(cmd_, 0, 1, &sc);
    vkCmdDraw(cmd_, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd_);
    vkEndCommandBuffer(cmd_);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1, sub.pCommandBuffers = &cmd_;
    VK(vkQueueSubmit(queue_, 1, &sub, fence_), "submit pass");
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_, 1, &fence_);
    cur_ = dst;
    return true;
}

bool VkScene::read_rgba(std::vector<uint8_t>& out, std::string& why) {
    Image& t = target_[cur_];
    if (!t.img) return why = "nothing drawn", false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, ib.image = t.img, ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, cp.imageExtent = {uint32_t(t.w), uint32_t(t.h), 1};
    vkCmdCopyImageToBuffer(cmd_, t.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buf, 1, &cp);
    ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    vkEndCommandBuffer(cmd_);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1, sub.pCommandBuffers = &cmd_;
    VK(vkQueueSubmit(queue_, 1, &sub, fence_), "submit readback");
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_, 1, &fence_);
    auto* p = static_cast<const uint8_t*>(readback_.map);
    out.assign(p, p + size_t(t.w) * t.h * 4);
    return true;
}

bool VkScene::present_to(VkImage dst, int w, int h, VkImageLayout old_layout, VkImageLayout final_layout, Rect r,
                         bool nearest, std::string& why, VkSemaphore wait, VkSemaphore signal) {
    Image& t = target_[cur_];
    if (!t.img) return why = "nothing drawn", false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    VkImageMemoryBarrier b[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}, {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
    for (auto& x : b) x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b[0].image = t.img, b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].image = dst, b[1].oldLayout = old_layout, b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].srcAccessMask = 0, b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
    VkClearColorValue black{};
    black.float32[3] = 1.0f;
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd_, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    if (r.w > 0 && r.h > 0) {
        VkImageBlit bl{};
        bl.srcSubresource = bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bl.srcOffsets[1] = {t.w, t.h, 1};
        bl.dstOffsets[0] = {std::max(r.x, 0), std::max(r.y, 0), 0};
        bl.dstOffsets[1] = {std::min(r.x + r.w, w), std::min(r.y + r.h, h), 1};
        vkCmdBlitImage(cmd_, t.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl,
                       nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    }
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, b[1].newLayout = final_layout;
    b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, b[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
    vkEndCommandBuffer(cmd_);
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1, sub.pCommandBuffers = &cmd_;
    if (wait) sub.waitSemaphoreCount = 1, sub.pWaitSemaphores = &wait, sub.pWaitDstStageMask = &ws;
    if (signal) sub.signalSemaphoreCount = 1, sub.pSignalSemaphores = &signal;
    VK(vkQueueSubmit(queue_, 1, &sub, fence_), "submit present");
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_, 1, &fence_);
    return true;
}

bool VkScene::present_offscreen(int w, int h, Rect r, bool nearest, std::vector<uint8_t>& out, std::string& why) {
    if (screen_.w != w || screen_.h != h) {
        free_image(screen_), free_buffer(screen_rb_);
        if (!make_image(screen_, w, h, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, why)) return false;
        if (!make_buffer(screen_rb_, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, why)) return false;
    }
    if (!present_to(screen_.img, w, h, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r, nearest, why)) return false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, cp.imageExtent = {uint32_t(w), uint32_t(h), 1};
    vkCmdCopyImageToBuffer(cmd_, screen_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screen_rb_.buf, 1, &cp);
    vkEndCommandBuffer(cmd_);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1, sub.pCommandBuffers = &cmd_;
    VK(vkQueueSubmit(queue_, 1, &sub, fence_), "submit screen readback");
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_, 1, &fence_);
    auto* p = static_cast<const uint8_t*>(screen_rb_.map);
    out.assign(p, p + size_t(w) * h * 4);
    return true;
}

VkScene::~VkScene() {
    if (!dev_) { if (inst_) vkDestroyInstance(inst_, nullptr); return; }
    vkDeviceWaitIdle(dev_);
    for (auto& f : fb_) if (f) vkDestroyFramebuffer(dev_, f, nullptr);
    for (auto& t : target_) free_image(t);
    free_image(white_), free_image(screen_);
    free_buffer(screen_rb_);
    free_buffer(scene_), free_buffer(textures_), free_buffer(ubo_), free_buffer(readback_);
    if (pipe_) vkDestroyPipeline(dev_, pipe_, nullptr);
    if (frag_) vkDestroyShaderModule(dev_, frag_, nullptr);
    if (vert_) vkDestroyShaderModule(dev_, vert_, nullptr);
    if (pass_) vkDestroyRenderPass(dev_, pass_, nullptr);
    if (sampler_) vkDestroySampler(dev_, sampler_, nullptr);
    if (dpool_) vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (layout_) vkDestroyPipelineLayout(dev_, layout_, nullptr);
    for (auto l : {set2_, set3_, empty_}) if (l) vkDestroyDescriptorSetLayout(dev_, l, nullptr);
    if (fence_) vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    vkDestroyInstance(inst_, nullptr);
}

}  // namespace sf3ps5
