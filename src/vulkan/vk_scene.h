// sfiii-ps5: direct-Vulkan replacement for sfiii-native's SDL_GPU scene pass (MIT).
//
// On the desktop, native/gpu-renderer.cpp uses an SDL_GPURenderState: the fragment shader
// geometry.hlsl runs over a full-target quad and reads two storage buffers (scene, textures),
// a sampled picture and one uniform block. The PS5 stack has SDL2 and RADV, but no SDL_GPU, so this
// class runs the same SPIR-V with the same resource layout as SDL_GPU's SPIR-V convention:
//   set 2: binding 0 combined image sampler (picture), bindings 1,2 storage buffers (scene, textures)
//   set 3: binding 0 uniform buffer (Pass)
//   inputs: location 0 color (vec4), location 1 uv (vec2), as SDL's renderer vertex shader feeds them.
// So the desktop's compiled shader is used unchanged. The present path (swapchain, 4K,
// place() in present.h) comes in the next step. This step is headless and tested on lavapipe.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace sf3ps5 {

class VkScene {
public:
    bool init(std::string& why);                                   // instance, device, queue (headless)
    bool load_fragment(const std::vector<uint32_t>& spirv, std::string& why);
    bool upload(const std::vector<uint32_t>& scene, const std::vector<uint32_t>& textures, std::string& why);
    // One pass: draws the full target with the fragment shader. `picture` is the previous output
    // (the effects pass) or nullptr for a white texel (the geometry pass, like gpu-renderer.cpp).
    bool draw(int w, int h, uint32_t aa_pass, bool picture_is_last, std::string& why);
    bool read_rgba(std::vector<uint8_t>& out, std::string& why);   // the last pass, RGBA8, top row first
    const char* device_name() const { return name_.c_str(); }
    ~VkScene();

private:
    struct Image { VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; int w = 0, h = 0; };
    struct Buffer { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; void* map = nullptr; VkDeviceSize size = 0; };
    bool make_buffer(Buffer&, VkDeviceSize, VkBufferUsageFlags, std::string&);
    bool make_image(Image&, int w, int h, VkImageUsageFlags, std::string&);
    void free_image(Image&);
    void free_buffer(Buffer&);
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags) const;
    bool ensure_targets(int w, int h, std::string&);
    bool ensure_pipeline(std::string&);

    VkInstance inst_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set2_ = VK_NULL_HANDLE, set3_ = VK_NULL_HANDLE, empty_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderModule vert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    VkPipeline pipe_ = VK_NULL_HANDLE;
    VkDescriptorPool dpool_ = VK_NULL_HANDLE;
    VkDescriptorSet ds2_[2] = {}, ds3_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    Image white_, target_[2];
    VkFramebuffer fb_[2] = {};
    int cur_ = 0;                                                  // the target written last
    Buffer scene_, textures_, ubo_, readback_;
    std::string name_;
};

}  // namespace sf3ps5
