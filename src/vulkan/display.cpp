// SPDX-License-Identifier: MIT
#include "display.h"

namespace sf3ps5 {

#define VKD(x, what) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { why = std::string(what) + " failed: " + std::to_string(r_); return false; } } while (0)

bool Display::open(VkScene& scene, std::string& why) {
    s_ = &scene;
    VkPhysicalDevice phys = scene.physical_device();
    uint32_t n = 0;
    VKD(vkGetPhysicalDeviceDisplayPropertiesKHR(phys, &n, nullptr), "display properties");
    if (!n) return why = "no display", false;
    std::vector<VkDisplayPropertiesKHR> displays(n);
    VKD(vkGetPhysicalDeviceDisplayPropertiesKHR(phys, &n, displays.data()), "display properties");
    VkDisplayKHR display = displays[0].display;
    uint32_t m = 0;
    VKD(vkGetDisplayModePropertiesKHR(phys, display, &m, nullptr), "display modes");
    if (!m) return why = "no display mode", false;
    std::vector<VkDisplayModePropertiesKHR> modes(m);
    VKD(vkGetDisplayModePropertiesKHR(phys, display, &m, modes.data()), "display modes");
    size_t best = 0;                                        // largest; at 59.94 Hz over a faster one of the same size
    for (size_t i = 1; i < modes.size(); ++i) {
        const auto& a = modes[i].parameters, &b = modes[best].parameters;
        uint64_t pa = uint64_t(a.visibleRegion.width) * a.visibleRegion.height, pb = uint64_t(b.visibleRegion.width) * b.visibleRegion.height;
        if (pa > pb || (pa == pb && a.refreshRate < b.refreshRate)) best = i;
    }
    VkDisplaySurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    si.displayMode = modes[best].displayMode, si.planeIndex = 0, si.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    si.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, si.globalAlpha = 1.0f;
    si.imageExtent = modes[best].parameters.visibleRegion;
    VKD(vkCreateDisplayPlaneSurfaceKHR(scene.instance(), &si, nullptr, &surface_), "vkCreateDisplayPlaneSurfaceKHR");
    VkBool32 ok = VK_FALSE;
    VKD(vkGetPhysicalDeviceSurfaceSupportKHR(phys, scene.queue_family(), surface_, &ok), "surface support");
    if (!ok) return why = "queue cannot present", false;
    VkSurfaceCapabilitiesKHR caps;
    VKD(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface_, &caps), "surface capabilities");
    uint32_t fn = 0;
    VKD(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface_, &fn, nullptr), "surface formats");
    std::vector<VkSurfaceFormatKHR> formats(fn);
    VKD(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface_, &fn, formats.data()), "surface formats");
    if (!fn) return why = "no surface format", false;
    VkSurfaceFormatKHR fmt = formats[0];
    for (auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) { fmt = f; break; }
    extent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : si.imageExtent;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    format_ = fmt.format;
    ci.imageFormat = fmt.format, ci.imageColorSpace = fmt.colorSpace, ci.imageExtent = extent_;
    ci.imageArrayLayers = 1, ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, ci.presentMode = VK_PRESENT_MODE_FIFO_KHR, ci.clipped = VK_TRUE;
    VKD(vkCreateSwapchainKHR(scene.device(), &ci, nullptr, &chain_), "vkCreateSwapchainKHR");
    uint32_t in = 0;
    VKD(vkGetSwapchainImagesKHR(scene.device(), chain_, &in, nullptr), "swapchain images");
    images_.resize(in);
    VKD(vkGetSwapchainImagesKHR(scene.device(), chain_, &in, images_.data()), "swapchain images");
    layouts_.assign(in, VK_IMAGE_LAYOUT_UNDEFINED);
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VKD(vkCreateSemaphore(scene.device(), &sci, nullptr, &acquired_), "semaphore");
    VKD(vkCreateSemaphore(scene.device(), &sci, nullptr, &rendered_), "semaphore");
    return true;
}

bool Display::frame(Rect r, bool nearest, std::string& why, const Overlay& overlay) {
    uint32_t i = 0;
    VKD(vkAcquireNextImageKHR(s_->device(), chain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &i), "vkAcquireNextImageKHR");
    if (!s_->present_to(images_[i], width(), height(), layouts_[i], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, r, nearest, why,
                        acquired_, rendered_, overlay)) return false;
    layouts_[i] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1, pi.pWaitSemaphores = &rendered_;
    pi.swapchainCount = 1, pi.pSwapchains = &chain_, pi.pImageIndices = &i;
    VKD(vkQueuePresentKHR(s_->queue(), &pi), "vkQueuePresentKHR");
    return true;
}

Display::~Display() {
    if (!s_) return;
    vkDeviceWaitIdle(s_->device());
    if (acquired_) vkDestroySemaphore(s_->device(), acquired_, nullptr);
    if (rendered_) vkDestroySemaphore(s_->device(), rendered_, nullptr);
    if (chain_) vkDestroySwapchainKHR(s_->device(), chain_, nullptr);
    if (surface_) vkDestroySurfaceKHR(s_->instance(), surface_, nullptr);
}

}  // namespace sf3ps5
