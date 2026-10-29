// SPDX-License-Identifier: MIT
#include "menu_vk.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"

namespace sf3ps5 {

bool MenuRenderer::init(VkScene& s, VkFormat format, uint32_t images, std::string& why) {
    dev_ = s.device();
    format_ = format;
    VkAttachmentDescription at{};
    at.format = format, at.samples = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout = at.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sp.colorAttachmentCount = 1, sp.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1, rp.pAttachments = &at, rp.subpassCount = 1, rp.pSubpasses = &sp;
    if (vkCreateRenderPass(dev_, &rp, nullptr, &pass_) != VK_SUCCESS) return why = "menu render pass", false;
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = s.instance(), ii.PhysicalDevice = s.physical_device(), ii.Device = dev_;
    ii.QueueFamily = s.queue_family(), ii.Queue = s.queue();
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2, ii.ImageCount = images < 2 ? 2 : images;
    ii.PipelineInfoMain.RenderPass = pass_;
    if (!ImGui_ImplVulkan_Init(&ii)) return why = "ImGui_ImplVulkan_Init", false;
    live_ = true;
    return true;
}

void MenuRenderer::record(VkCommandBuffer cb, VkImage dst, int w, int h) {
    ImDrawData* dd = ImGui::GetDrawData();
    if (!live_ || !dd || dd->CmdListsCount == 0) return;
    Target& t = targets_[dst];
    if (t.w != w || t.h != h) {
        if (t.fb) vkDestroyFramebuffer(dev_, t.fb, nullptr);
        if (t.view) vkDestroyImageView(dev_, t.view, nullptr);
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = dst, vi.viewType = VK_IMAGE_VIEW_TYPE_2D, vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev_, &vi, nullptr, &t.view);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = pass_, fi.attachmentCount = 1, fi.pAttachments = &t.view;
        fi.width = uint32_t(w), fi.height = uint32_t(h), fi.layers = 1;
        vkCreateFramebuffer(dev_, &fi, nullptr, &t.fb);
        t.w = w, t.h = h;
    }
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = pass_, bi.framebuffer = t.fb, bi.renderArea = {{0, 0}, {uint32_t(w), uint32_t(h)}};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(dd, cb);
    vkCmdEndRenderPass(cb);
}

void MenuRenderer::shutdown() {
    if (!dev_) return;
    vkDeviceWaitIdle(dev_);
    if (live_) ImGui_ImplVulkan_Shutdown();
    live_ = false;
    for (auto& [img, t] : targets_) {
        if (t.fb) vkDestroyFramebuffer(dev_, t.fb, nullptr);
        if (t.view) vkDestroyImageView(dev_, t.view, nullptr);
    }
    targets_.clear();
    if (pass_) vkDestroyRenderPass(dev_, pass_, nullptr);
    pass_ = VK_NULL_HANDLE;
    dev_ = VK_NULL_HANDLE;
}

}  // namespace sf3ps5
