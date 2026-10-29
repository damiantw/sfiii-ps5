# Vulkan backend (PS5)

sfiii-native renders the game field with an **SDL_GPU render state**, in `native/gpu-renderer.cpp` and `hd-gpu.cpp`, about 400 lines.
The fragment shader `geometry.hlsl` covers the target and reads these resources:
- two storage buffers: `scene` (pixels, composition, quads, tile bins) and `textures`
- a sampled `picture`
- the `Pass` uniform

There are two passes. The geometry pass draws over a white texel. The optional effects pass (AA / ray shadows) samples the geometry output.

The PS5 has SDL2 and homebrew RADV (PS5_Vulkan / PS5_Mesa), but no SDL_GPU. `src/vulkan/vk_scene.*` runs the same SPIR-V
with SDL_GPU's SPIR-V resource layout:
- set 2: b0 sampler, b1 and b2 storage buffers
- set 3: b0 uniforms
- inputs: location 0 color, location 1 uv

So the shader itself is unchanged.

## Status
- `vk_scene_test` passes on lavapipe on the box. It runs both passes with a layout-identical test shader and checks every pixel.
- The pipeline also builds from the desktop's own `geometry.hlsl`, compiled to SPIR-V by glslang.
- `present.h` handles placement on the 4K screen:
  - 4:3 pillarbox, integer-scaled when the scale fills ≥90 % of the height (320×224 → 2688×2016).
  - 16:9 is renderer-only: it uses only the columns the game already composes, never game memory, and is capped at 16:9.

## Next
1. Swapchain present on the PS5 (FIFO, 3840×2160), like PS5_Shipwright's `gfx_vulkan` path.
   A per-frame ring of the two storage buffers replaces the `vkDeviceWaitIdle` in `upload`.
2. A `GpuRenderer`-compatible adapter so sfiii-native's frame loop calls VkScene. This needs a small fork-side
   interface (`render(Frame, scale)` returning an image handle), proposed as a fork PR.
3. Parity: render the same captured `Frame`s through SDL_GPU (desktop) and VkScene (lavapipe), then compare.
   Captures stay local; only invented frames go in the repo.
