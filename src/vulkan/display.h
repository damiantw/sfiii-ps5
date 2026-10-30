// SPDX-License-Identifier: MIT
// The PS5 screen through VK_KHR_display (PS5_Mesa's VideoOut WSI: one display, one plane, modes
// 3840x2160 first), a FIFO swapchain, and a frame step that runs VkScene::present_to into it.
#pragma once
#include "present.h"
#include "vk_scene.h"
#include <string>
#include <vector>

namespace sf3ps5 {

class Display {
public:
    // scene must have been init(why, true). Picks the largest mode (4K on the PS5).
    // low_latency: the minimum swapchain (2 images) instead of 3.
    bool open(VkScene& scene, std::string& why, bool low_latency = false);
    // Acquire, present_to(r) and queue present. Blocks on FIFO (vsync).
    bool frame(Rect r, bool nearest, std::string& why, const Overlay& overlay = nullptr);
    VkFormat format() const { return format_; }
    uint32_t image_count() const { return uint32_t(images_.size()); }
    int width() const { return int(extent_.width); }
    int height() const { return int(extent_.height); }
    ~Display();

private:
    VkScene* s_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR chain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> images_;
    std::vector<VkImageLayout> layouts_;
    VkSemaphore acquired_ = VK_NULL_HANDLE, rendered_ = VK_NULL_HANDLE;
};

}  // namespace sf3ps5
