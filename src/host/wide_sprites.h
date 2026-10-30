// SPDX-License-Identifier: MIT
// 16:9 on the console: the sprites in the columns beyond 4:3, drawn on the CPU.
//
// The runtime's native VDP1 framebuffer holds only the 4:3 picture. For 16:9 the desktop app hands
// the expanded field (vdp2_compose_wide: VDP2 layers drawn across the extra columns, plus the
// field's VDP1 command quads and per-dot composition records) to a GPU fragment shader that
// draws those quads into the side columns (sfiii-native native/gpu-renderer.cpp + geometry.hlsl;
// at 1x the centre is the CPU raster, untouched). The console draws VDP1 in software at 1x, so
// this is the same per-dot rule as that shader's 1x path, run on the CPU over the side columns
// only: about 2 x 53 x 240 dots. Quads are tested last to first (the top one wins), each through
// the shader's coverage test, inverse-bilinear texel lookup, Gouraud and colour calculation.
#pragma once
#if __has_include("video/video.h")
#include "video/video.h"
#else
#include "video.h"
#endif
#include <algorithm>
#include <atomic>
#include <memory>
#include <system_error>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace sf3ps5 {

class WideSprites {
    struct Quad {
        const HdQuad* q;
        float xy[8];
        int clip_x0, clip_x1, clip_y1, user[4];
        uint32_t steps_u, steps_v;
        const std::vector<uint32_t>* tex;
        float minx, maxx, miny, maxy;   // native coordinates
    };
    std::vector<Quad> quads_;
    std::vector<std::vector<uint32_t>> rows_;     // per output row: quad indices touching a side column
    // Texels with end codes resolved, per texture and mode. HdTextures never change once made (the
    // runtime makes a new one for new texels), so this outlives a field; it holds the texture so
    // its address is not reused while cached.
    struct Texels { std::shared_ptr<HdTexture> owner; std::vector<uint32_t> px; };
    std::unordered_map<uint64_t, Texels> textures_;
    // the last field drawn: an unchanged field (30 Hz pictures, menus) is not drawn again
    std::vector<uint32_t> last_in_, last_out_;
    std::shared_ptr<HdScene> last_hd_;
    int last_w_ = 0, last_h_ = 0, last_origin_ = 0;

    const std::vector<uint32_t>* texels(const HdQuad& q) {
        unsigned mode = (q.hflip ? 1u : 0u) | (q.gpu_flags & 0x80 ? 2u : 0u);
        uint64_t key = uint64_t(uintptr_t(q.texture.get())) << 2 | mode;
        auto [it, added] = textures_.try_emplace(key);
        if (added) {
            const HdTexture& t = *q.texture;
            it->second.owner = q.texture;
            std::vector<uint32_t>& px = it->second.px;
            px.assign(t.raw.begin(), t.raw.end());
            // End codes end a source row (as gpu-renderer.cpp resolves them once per texture/mode).
            if (!(mode & 2))
                for (int y = 0; y < t.h; ++y) {
                    unsigned ends = 0;
                    for (int x = 0; x < t.w; ++x) {
                        int column = q.hflip ? t.w - 1 - x : x;
                        uint32_t& v = px[size_t(y) * t.w + column];
                        if (v == 0x7FFF) ++ends;
                        if (v == 0x7FFF || ends >= 2) v = 0x2000000;
                    }
                }
        }
        return &it->second.px;
    }

    static float round_away(float v) { return v < 0 ? -std::floor(-v + .5f) : std::floor(v + .5f); }

    static bool inverse_bilinear(const float a[2], const float b[2], const float c[2], const float d[2], const float p[2], float uv[2]) {
        uv[0] = uv[1] = .5f;
        for (int i = 0; i < 6; ++i) {
            float f0 = a[0] + b[0] * uv[0] + d[0] * uv[1] + c[0] * uv[0] * uv[1] - p[0];
            float f1 = a[1] + b[1] * uv[0] + d[1] * uv[1] + c[1] * uv[0] * uv[1] - p[1];
            float du0 = b[0] + c[0] * uv[1], du1 = b[1] + c[1] * uv[1];
            float dv0 = d[0] + c[0] * uv[0], dv1 = d[1] + c[1] * uv[0];
            float det = du0 * dv1 - du1 * dv0;
            if (std::fabs(det) < 0.00001f) return false;
            uv[0] -= (f0 * dv1 - f1 * dv0) / det;
            uv[1] -= (du0 * f1 - du1 * f0) / det;
        }
        return true;
    }

    // geometry.hlsl sample_quad: the colour (0x00RRGGBB, or 0x1000000 = SPD clear) of quad Q at P.
    static bool sample(const Quad& Q, float px, float py, int cx, int cy, uint32_t& color) {
        const HdQuad& q = *Q.q;
        const uint32_t flags = q.gpu_flags;
        if (cx < Q.clip_x0 || cy < 0 || cx > Q.clip_x1 || cy > Q.clip_y1) return false;
        if (flags & 0x400) {
            bool inside = cx >= Q.user[0] && cy >= Q.user[1] && cx <= Q.user[2] && cy <= Q.user[3];
            if (inside == ((flags & 0x200) != 0)) return false;
        }
        const float* v = Q.xy;
        float a[2] = {v[0], v[1]}, b[2] = {v[2] - v[0], v[3] - v[1]}, d[2] = {v[6] - v[0], v[7] - v[1]};
        float c[2] = {v[0] - v[2] + v[4] - v[6], v[1] - v[3] + v[5] - v[7]};
        float orientation = 0;
        for (int e = 0; e < 4; ++e) {
            int n = (e + 1) & 3;
            float vx = v[2 * n] - v[2 * e], vy = v[2 * n + 1] - v[2 * e + 1], tx = px - v[2 * e], ty = py - v[2 * e + 1];
            float side = vx * ty - vy * tx;
            if (std::fabs(side) > 0.00001f) { if (orientation * side < 0) return false; orientation = side; }
        }
        float p[2] = {px, py}, uv[2];
        if (!inverse_bilinear(a, b, c, d, p, uv)) return false;
        if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]) || uv[0] < 0 || uv[1] < 0 || uv[0] > 1 || uv[1] > 1) return false;
        if (std::fabs(a[0] + b[0] * uv[0] + d[0] * uv[1] + c[0] * uv[0] * uv[1] - px) > 0.001f ||
            std::fabs(a[1] + b[1] * uv[0] + d[1] * uv[1] + c[1] * uv[0] * uv[1] - py) > 0.001f) return false;
        const uint32_t w = uint32_t(q.texture->w), h = uint32_t(q.texture->h), lu = Q.steps_u, lv = Q.steps_v;
        const bool distorted = ((flags >> 16) & 15) >= 2;
        uint32_t u = std::min(uint32_t(uv[0] * w), w - 1), t = std::min(uint32_t(uv[1] * h), h - 1);
        if (w <= lu + 1 && lu > 0) {
            float k = uv[0] * (lu + 1) - .5f;
            if (!distorted) k = std::clamp(std::floor(k + .5f), 0.f, float(lu));
            u = uint32_t(std::clamp(std::floor(k * (w - 1) / lu + .5f), 0.f, float(w - 1)));
        }
        if (h <= lv + 1 && lv > 0) {
            float k = uv[1] * (lv + 1) - .5f;
            if (!distorted) k = std::clamp(std::floor(k + .5f), 0.f, float(lv));
            float s = k * (h - 1) / lv;
            t = uint32_t(std::clamp(q.vflip ? std::floor(s + .5f) : std::ceil(s - .5f), 0.f, float(h - 1)));
        }
        if (q.hflip) u = w - 1 - u;
        if (q.vflip) t = h - 1 - t;
        uint32_t raw = (*Q.tex)[size_t(t) * w + u];
        if (raw & 0x2000000) return false;
        if (raw == 0 && !(flags & 0x40)) return false;
        if (raw == 0) { color = 0x1000000; return true; }   // SPD clears the previous sprite dot
        int rgb[3] = {int(raw & 31), int(raw >> 5 & 31), int(raw >> 10 & 31)};
        if (flags & 4) {
            for (int ch = 0; ch < 3; ++ch) {
                float ga = q.shade[ch] / 8 + 16, gb = q.shade[3 + ch] / 8 + 16, gc = q.shade[6 + ch] / 8 + 16, gd = q.shade[9 + ch] / 8 + 16;
                float left = round_away(ga + (gd - ga) * uv[1]), right = round_away(gb + (gc - gb) * uv[1]);
                rgb[ch] = std::clamp(rgb[ch] + int(round_away(left + (right - left) * uv[0])) - 16, 0, 31);
            }
        }
        color = uint32_t(rgb[0] << 3) << 16 | uint32_t(rgb[1] << 3) << 8 | uint32_t(rgb[2] << 3);
        return true;
    }

public:
    // F: vdp2_compose_wide's frame (origin_x > 0, composition records present). Fills the side
    // columns in place; the centre stays the native picture. False when F has no records.
    bool draw(Frame& f) {
        if (!f.hd || f.origin_x <= 0 || f.hd->composition.size() != f.px.size()) return false;
        const int W = f.w, H = f.h, O = f.origin_x, N = f.native_w > 0 ? f.native_w : W;
        if (f.hd == last_hd_ && f.w == last_w_ && f.h == last_h_ && f.origin_x == last_origin_ && f.px == last_in_) {
            f.px = last_out_;
            return true;
        }
        last_hd_ = f.hd, last_w_ = f.w, last_h_ = f.h, last_origin_ = f.origin_x, last_in_ = f.px;
        if (textures_.size() > 4096) textures_.clear();
        quads_.clear();
        rows_.resize(size_t(H));
        for (auto& r : rows_) r.clear();
        const auto& src = f.hd->quads;
        for (size_t i = 0; i < src.size(); ++i) {
            const HdQuad& q = src[i];
            if (!q.gpu_eligible || !q.texture || q.texture->raw.size() != size_t(q.texture->w) * q.texture->h || q.texture->w < 1 || q.texture->h < 1) continue;
            Quad Q{};
            Q.q = &q;
            std::copy(q.xy, q.xy + 8, Q.xy);
            float* xy = Q.xy;
            auto steps = [&](int a, int b) { return std::max(std::abs(int(xy[2 * b]) - int(xy[2 * a])), std::abs(int(xy[2 * b + 1]) - int(xy[2 * a + 1]))); };
            Q.steps_u = uint32_t(std::max(steps(0, 1), steps(3, 2)));
            Q.steps_v = uint32_t(std::max(steps(0, 3), steps(1, 2)));
            if ((q.gpu_flags >> 16 & 15) < 2) {
                const float dx = xy[2] >= xy[0] ? .5f : -.5f, dy = xy[7] >= xy[1] ? .5f : -.5f;
                for (int v = 0; v < 4; ++v) { xy[v * 2] += (v == 1 || v == 2) ? dx : -dx; xy[v * 2 + 1] += v >= 2 ? dy : -dy; }
            } else {
                // distorted: vertices are dot centres; the drawn area reaches half a dot beyond them
                const float cx = (xy[0] + xy[2] + xy[4] + xy[6]) / 4, cy = (xy[1] + xy[3] + xy[5] + xy[7]) / 4;
                for (int v = 0; v < 4; ++v) {
                    xy[v * 2] += xy[v * 2] > cx ? .5f : xy[v * 2] < cx ? -.5f : 0.f;
                    xy[v * 2 + 1] += xy[v * 2 + 1] > cy ? .5f : xy[v * 2 + 1] < cy ? -.5f : 0.f;
                }
            }
            Q.clip_x1 = q.clip[0], Q.clip_y1 = q.clip[1], Q.clip_x0 = 0;
            for (int k = 0; k < 4; ++k) Q.user[k] = q.clip[2 + k];
            // a system/user clip that spans the 4:3 width spans the 16:9 width
            if (q.clip[0] >= N - 1) Q.clip_x0 = -O, Q.clip_x1 = W - O - 1;
            if (q.clip[2] == 0 && q.clip[4] >= N - 1) Q.user[0] = -O, Q.user[2] = W - O - 1;
            Q.minx = Q.maxx = xy[0], Q.miny = Q.maxy = xy[1];
            for (int k = 1; k < 4; ++k) {
                Q.minx = std::min(Q.minx, xy[2 * k]), Q.maxx = std::max(Q.maxx, xy[2 * k]);
                Q.miny = std::min(Q.miny, xy[2 * k + 1]), Q.maxy = std::max(Q.maxy, xy[2 * k + 1]);
            }
            // only quads reaching a side column: native x < 0 or x >= N
            if (Q.maxy < -.5f || Q.miny > H || Q.maxx < -O - 1 || Q.minx > W - O + 1) continue;
            if (Q.minx > -1.f && Q.maxx < N) continue;
            Q.tex = texels(q);
            const uint32_t id = uint32_t(quads_.size());
            quads_.push_back(Q);
            int y0 = std::clamp(int(std::floor(Q.miny - 1)), 0, H - 1), y1 = std::clamp(int(std::floor(Q.maxy + 1)), 0, H - 1);
            for (int y = y0; y <= y1; ++y) rows_[size_t(y)].push_back(id);
        }
        const auto& comp = f.hd->composition;
        // Rows are independent (each reads the quads and writes its own dots): bands of rows on up
        // to 4 threads, handed out in turn, as the runtime composes VDP2.
        auto rows = [&](int from, int to) {
        for (int y = from; y < to; ++y) {
            const auto& row = rows_[size_t(y)];
            for (int side = 0; side < 2; ++side) {
                const int x0 = side ? O + N : 0, x1 = side ? W : O;
                for (int x = x0; x < x1; ++x) {
                    const size_t i = size_t(y) * W + x;
                    const GpuPixel& e = comp[i];
                    if (e.mode & 0x100) continue;
                    const int nx = x - O;
                    const float px = float(nx), py = float(y);
                    uint32_t color = 0;
                    bool found = false;
                    for (size_t k = row.size(); k > 0; --k) {
                        const Quad& Q = quads_[row[k - 1]];
                        if (px < Q.minx - 1 || px > Q.maxx + 1) continue;
                        if (sample(Q, px, py, nx, y, color)) { found = true; break; }
                    }
                    uint32_t& out = f.px[i];
                    if (!found) { if (e.owner) out = e.absent; continue; }
                    if (color & 0x1000000) { out = e.absent; continue; }
                    if (e.mode == 0) { out = e.constant; continue; }
                    uint32_t result = 0;
                    for (int ch = 0; ch < 3; ++ch) {
                        int sh = 16 - ch * 8;
                        uint32_t a = color >> sh & 255, b = e.constant >> sh & 255, c = a;
                        if (e.mode == 2) c = (a * (31 - e.ratio) + b * (e.ratio + 1)) >> 5;
                        if (e.mode == 3 || e.mode == 5) c = std::min(a + b, 255u);
                        if (e.mode == 4) c = (b * (31 - e.ratio) + a * (e.ratio + 1)) >> 5;
                        c = uint32_t(std::clamp(int(c) + int(e.offset[ch]), 0, 255));
                        result |= c << sh;
                    }
                    out = result;
                }
            }
        }
        };
        const int bands = 16;
        std::atomic<int> next{0};
        auto work = [&] { for (int b; (b = next.fetch_add(1)) < bands;) rows(H * b / bands, H * (b + 1) / bands); };
        std::thread workers[3];
        static const unsigned hw = std::thread::hardware_concurrency();
        for (unsigned i = 0; i + 1 < hw && i < 3; ++i) {
            try { workers[i] = std::thread(work); } catch (const std::system_error&) { break; }
        }
        work();
        for (auto& w : workers) if (w.joinable()) w.join();
        last_out_ = f.px;
        return true;
    }
};

}  // namespace sf3ps5
