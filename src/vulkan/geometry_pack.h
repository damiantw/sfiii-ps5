// sfiii-ps5: packs one runtime Frame (with its HdScene) into geometry.hlsl's two storage buffers (MIT).
//
// The same layout as sfiii-native's native/gpu-renderer.cpp (MIT), GpuRenderer::render: a header,
// 10 words per dot (picture + composition record), 40 words per VDP1 quad, 4x4-dot tile bins and the
// quad indices. Textures carry end codes resolved once per texture and direction. The PS5 host
// uploads the result to VkScene and draws the target at `scale` x the field size (2x internal 3D).
// Textures are cached across fields by HdTexture pointer: the runtime keeps a texture alive while
// its pixels are unchanged.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sf3ps5 {

class GeometryPack {
public:
    static constexpr uint32_t kTile = 4;
    std::vector<uint32_t> data, tex;
    // false: no geometry (no HdScene for this field); the scene is still packed (the picture alone).
    template <class Frame>
    void pack(const Frame& f, int scale) {
        const bool geometry = f.hd && f.hd->composition.size() == f.px.size();
        const size_t count = geometry ? f.hd->quads.size() : 0;
        const uint32_t columns = (f.w + kTile - 1) / kTile, rows = (f.h + kTile - 1) / kTile, pixels = uint32_t(f.px.size());
        const uint32_t quad_at = 12 + pixels * 10, bin_at = quad_at + uint32_t(count) * 40, index_at = bin_at + columns * rows * 2;
        data.assign(index_at, 0);
        bins_.resize(columns * rows);
        for (auto& b : bins_) b.clear();
        if (++fields_ % 1800 == 0 || tex.size() > (8u << 20)) tex.assign(1, 0), cache_.clear();   // drop stale textures now and then
        if (tex.empty()) tex.assign(1, 0);
        data[0] = f.w, data[1] = f.h, data[2] = scale, data[3] = uint32_t(count), data[4] = 12, data[5] = quad_at;
        data[6] = bin_at, data[7] = index_at, data[8] = columns;
        data[9] = f.origin_x, data[10] = kTile, data[11] = uint32_t(f.native_w > 0 ? f.native_w : f.w);
        for (uint32_t i = 0; i < pixels; ++i) {
            uint32_t* d = &data[12 + i * 10];
            d[0] = f.px[i], d[3] = 0x100;
            if (geometry) {
                const auto& e = f.hd->composition[i];
                d[1] = e.absent, d[2] = e.constant, d[3] = e.mode, d[4] = e.ratio;
                for (int c = 0; c < 3; ++c) d[5 + c] = uint32_t(e.offset[c]);
                d[8] = e.owner;
            }
        }
        for (uint32_t i = 0; i < count; ++i) {
            const auto& q = f.hd->quads[i];
            uint32_t* d = &data[quad_at + i * 40];
            d[25] = 0xFFFFFFFF;
            if (!q.gpu_eligible || !q.texture || q.texture->raw.size() != size_t(q.texture->w) * q.texture->h) continue;
            unsigned mode = (q.hflip ? 1u : 0u) | (q.gpu_flags & 0x80 ? 2u : 0u);
            const uint32_t at_tex = texture(q.texture, q.hflip, mode);
            float xy[8];
            std::copy(q.xy, q.xy + 8, xy);
            auto steps = [&](int a, int b) { return std::max(std::abs(int(xy[2 * b]) - int(xy[2 * a])), std::abs(int(xy[2 * b + 1]) - int(xy[2 * a + 1]))); };
            d[33] = uint32_t(std::max(steps(0, 1), steps(3, 2))), d[34] = uint32_t(std::max(steps(0, 3), steps(1, 2)));
            if ((q.gpu_flags >> 16 & 15) < 2) {
                const float dx = xy[2] >= xy[0] ? 0.5f : -0.5f, dy = xy[7] >= xy[1] ? 0.5f : -0.5f;
                for (int v = 0; v < 4; ++v) { xy[v * 2] += (v == 1 || v == 2) ? dx : -dx; xy[v * 2 + 1] += (v >= 2) ? dy : -dy; }
            } else {   // distorted: vertices are dot centres, VDP1 fills half a dot beyond them
                const float cx = (xy[0] + xy[2] + xy[4] + xy[6]) / 4, cy = (xy[1] + xy[3] + xy[5] + xy[7]) / 4;
                for (int v = 0; v < 4; ++v) {
                    xy[v * 2] += xy[v * 2] > cx ? 0.5f : xy[v * 2] < cx ? -0.5f : 0.f;
                    xy[v * 2 + 1] += xy[v * 2 + 1] > cy ? 0.5f : xy[v * 2 + 1] < cy ? -0.5f : 0.f;
                }
            }
            std::memcpy(d, xy, 32);
            std::memcpy(d + 8, q.shade, 48);
            d[20] = q.hflip, d[21] = q.vflip, d[22] = at_tex, d[23] = q.texture->w, d[24] = q.texture->h, d[25] = q.gpu_flags;
            for (int k = 0; k < 6; ++k) d[26 + k] = uint32_t(q.clip[k]);
            if (f.origin_x) {
                if (q.clip[0] >= f.native_w - 1) d[32] = uint32_t(-f.origin_x), d[26] = f.w - f.origin_x - 1;
                if (q.clip[2] == 0 && q.clip[4] >= f.native_w - 1) d[28] = uint32_t(-f.origin_x), d[30] = f.w - f.origin_x - 1;
            }
            float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
            for (int k = 1; k < 4; ++k) minx = std::min(minx, xy[2 * k]), maxx = std::max(maxx, xy[2 * k]), miny = std::min(miny, xy[2 * k + 1]), maxy = std::max(maxy, xy[2 * k + 1]);
            d[35] = uint32_t(std::clamp((maxx - minx) * (maxy - miny), 1.0f, 1000000.0f));
            minx += f.origin_x, maxx += f.origin_x;
            if (maxx < -.5f || maxy < -.5f || minx > f.w || miny > f.h) continue;
            const int x0 = std::clamp(int(std::floor((minx - 1) / kTile)), 0, int(columns) - 1), x1 = std::clamp(int(std::floor((maxx + 1) / kTile)), 0, int(columns) - 1);
            const int y0 = std::clamp(int(std::floor((miny - 1) / kTile)), 0, int(rows) - 1), y1 = std::clamp(int(std::floor((maxy + 1) / kTile)), 0, int(rows) - 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) bins_[y * columns + x].push_back(i);
        }
        for (uint32_t i = 0; i < bins_.size(); ++i) {
            data[bin_at + 2 * i] = uint32_t(data.size()), data[bin_at + 2 * i + 1] = uint32_t(bins_[i].size());
            data.insert(data.end(), bins_[i].begin(), bins_[i].end());
        }
    }

private:
    struct Entry { std::shared_ptr<const void> owner; uint32_t at; };
    template <class Tex>
    uint32_t texture(const std::shared_ptr<Tex>& t, bool hflip, unsigned mode) {
        const uint64_t key = uint64_t(uintptr_t(t.get())) << 2 | mode;
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.owner.get() == t.get()) return it->second.at;
        const uint32_t base = uint32_t(tex.size());
        tex.insert(tex.end(), t->raw.begin(), t->raw.end());
        if (!(mode & 2))
            for (int y = 0; y < t->h; ++y) {   // end codes terminate a source row
                unsigned ends = 0;
                for (int x = 0; x < t->w; ++x) {
                    const int column = hflip ? t->w - 1 - x : x;
                    auto& value = tex[base + y * t->w + column];
                    if (value == 0x7FFF) ++ends;
                    if (value == 0x7FFF || ends >= 2) value = 0x2000000;
                }
            }
        cache_[key] = Entry{std::static_pointer_cast<const void>(t), base};
        return base;
    }
    std::vector<std::vector<uint32_t>> bins_;
    std::unordered_map<uint64_t, Entry> cache_;
    uint64_t fields_ = 0;
};

}  // namespace sf3ps5
