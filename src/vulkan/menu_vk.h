// SPDX-License-Identifier: MIT
// Draws ImGui (the menu) over a presented frame with Dear ImGui's Vulkan backend
// (imgui_impl_vulkan, MIT). Vulkan 1.1, so a render pass (load, not clear) instead of dynamic
// rendering. Use as VkScene's Overlay: menu.overlay() passed to present_to / Display::frame.
#pragma once
#include "vk_scene.h"
#include <map>
#include <string>

struct ImDrawData;

namespace sf3ps5 {

class MenuRenderer {
public:
    // After ImGui::CreateContext(). format: the presented image's (Display::format() or
    // VkScene::kOffscreenFormat). images: swapchain image count (>= 2).
    bool init(VkScene& scene, VkFormat format, uint32_t images, std::string& why);
    // Records the current ImGui::GetDrawData() into cb (dst in COLOR_ATTACHMENT_OPTIMAL).
    void record(VkCommandBuffer cb, VkImage dst, int w, int h);
    Overlay overlay() { return [this](VkCommandBuffer cb, VkImage dst, int w, int h) { record(cb, dst, w, h); }; }
    void shutdown();
    ~MenuRenderer() { shutdown(); }
private:
    struct Target { VkImageView view = VK_NULL_HANDLE; VkFramebuffer fb = VK_NULL_HANDLE; int w = 0, h = 0; };
    VkDevice dev_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    std::map<VkImage, Target> targets_;
    bool live_ = false;
};

}  // namespace sf3ps5
