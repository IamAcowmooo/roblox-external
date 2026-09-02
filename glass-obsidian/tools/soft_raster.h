// -----------------------------------------------------------------------------
//  tools/soft_raster.h  --  VERIFICATION TOOLING, not part of the shipped theme
// -----------------------------------------------------------------------------
//  A tiny CPU rasteriser for ImGui's ImDrawData plus a dependency-free PNG
//  writer. It exists so the Glass Obsidian theme can be *seen* and *regression
//  tested* on a machine with no GPU, no windowing system and no image library:
//  run the real ImGui frame loop headless, then turn the draw output into a PNG.
//
//  Scope: correctness over speed. Per-pixel barycentric interpolation with
//  bilinear glyph sampling and straight-alpha blending. ImGui already tessellates
//  its own anti-aliasing fringe into vertex colours/alpha, so interpolating
//  those per pixel reproduces the GPU result closely enough to review a design.
//
//  Do not ship this in a product build.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace softras {

// =============================================================================
//  Framebuffer
// =============================================================================
struct Framebuffer
{
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA8, row-major, top-left origin

    void Resize(int width, int height)
    {
        w = width; h = height;
        px.assign((size_t)w * (size_t)h * 4, 0);
    }
    void Clear(const ImVec4& c)
    {
        const uint8_t r = (uint8_t)(c.x * 255.0f + 0.5f);
        const uint8_t g = (uint8_t)(c.y * 255.0f + 0.5f);
        const uint8_t b = (uint8_t)(c.z * 255.0f + 0.5f);
        const uint8_t a = (uint8_t)(c.w * 255.0f + 0.5f);
        for (size_t i = 0; i + 3 < px.size(); i += 4) { px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a; }
    }
    uint8_t* At(int x, int y) { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
};

// =============================================================================
//  Rasteriser
// =============================================================================
class Raster
{
public:
    // tex_id: the ImTextureID the font atlas was tagged with (see
    // ImFontAtlas::SetTexID). Commands carrying that id are sampled as glyphs;
    // everything else is flat vertex colour.
    void SetFontAtlas(ImTextureID tex_id)
    {
        m_tex_id = tex_id;
        unsigned char* pixels = nullptr;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &m_tex_w, &m_tex_h);
        m_tex = pixels;
    }

    void Render(ImDrawData* dd, Framebuffer& fb)
    {
        if (!dd || !dd->Valid) return;
        const ImVec2 dp = dd->DisplayPos;

        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* cl = dd->CmdLists[n];
            const ImDrawVert* vtx = cl->VtxBuffer.Data;
            const ImDrawIdx*  idx = cl->IdxBuffer.Data;

            for (int ci = 0; ci < cl->CmdBuffer.Size; ++ci) {
                const ImDrawCmd& cmd = cl->CmdBuffer[ci];
                if (cmd.UserCallback) continue;
                if (cmd.ElemCount == 0) continue;

                // Clip rect, in framebuffer space.
                float cx0 = cmd.ClipRect.x - dp.x;
                float cy0 = cmd.ClipRect.y - dp.y;
                float cx1 = cmd.ClipRect.z - dp.x;
                float cy1 = cmd.ClipRect.w - dp.y;
                if (cx1 <= cx0 || cy1 <= cy0) continue;

                const bool textured = (cmd.TextureId != nullptr && cmd.TextureId == m_tex_id && m_tex);

                for (unsigned int i = 0; i < cmd.ElemCount; i += 3) {
                    const ImDrawVert& v0 = vtx[idx[cmd.IdxOffset + i + 0]];
                    const ImDrawVert& v1 = vtx[idx[cmd.IdxOffset + i + 1]];
                    const ImDrawVert& v2 = vtx[idx[cmd.IdxOffset + i + 2]];
                    DrawTriangle(fb, v0, v1, v2, textured,
                                 (int)cx0, (int)cy0, (int)cx1, (int)cy1);
                }
            }
        }
    }

private:
    struct Vert { float x, y, u, v, r, g, b, a; };

    static Vert Prep(const ImDrawVert& iv)
    {
        Vert o;
        o.x = iv.pos.x; o.y = iv.pos.y; o.u = iv.uv.x; o.v = iv.uv.y;
        const uint8_t* c = (const uint8_t*)&iv.col;
        // ImGui packs ImU32 as RGBA little-endian on the usual targets.
        o.r = c[0] / 255.0f; o.g = c[1] / 255.0f; o.b = c[2] / 255.0f; o.a = c[3] / 255.0f;
        return o;
    }

    float SampleAlpha(float u, float v) const
    {
        if (!m_tex) return 1.0f;
        float fx = u * (float)m_tex_w - 0.5f;
        float fy = v * (float)m_tex_h - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - (float)x0, ty = fy - (float)y0;
        auto A = [&](int x, int y) -> float {
            if (x < 0) x = 0; if (y < 0) y = 0;
            if (x >= m_tex_w) x = m_tex_w - 1;
            if (y >= m_tex_h) y = m_tex_h - 1;
            return m_tex[((size_t)y * (size_t)m_tex_w + (size_t)x) * 4 + 3] / 255.0f;
        };
        const float a00 = A(x0, y0),     a10 = A(x0 + 1, y0);
        const float a01 = A(x0, y0 + 1), a11 = A(x0 + 1, y0 + 1);
        const float top = a00 + (a10 - a00) * tx;
        const float bot = a01 + (a11 - a01) * tx;
        return top + (bot - top) * ty;
    }

    void Blend(Framebuffer& fb, int x, int y, float r, float g, float b, float a) const
    {
        if (x < 0 || y < 0 || x >= fb.w || y >= fb.h || a <= 0.0f) return;
        uint8_t* p = fb.At(x, y);
        const float da = p[3] / 255.0f;
        const float oa = a + da * (1.0f - a);
        if (oa <= 0.0001f) { p[0] = p[1] = p[2] = p[3] = 0; return; }
        // straight-alpha "over", preserving the destination's own alpha
        const float dr = p[0] / 255.0f, dg = p[1] / 255.0f, db = p[2] / 255.0f;
        p[0] = (uint8_t)(255.0f * (r * a + dr * da * (1.0f - a)) / oa + 0.5f);
        p[1] = (uint8_t)(255.0f * (g * a + dg * da * (1.0f - a)) / oa + 0.5f);
        p[2] = (uint8_t)(255.0f * (b * a + db * da * (1.0f - a)) / oa + 0.5f);
        p[3] = (uint8_t)(255.0f * oa + 0.5f);
    }

    void DrawTriangle(Framebuffer& fb, const ImDrawVert& i0, const ImDrawVert& i1,
                      const ImDrawVert& i2, bool textured,
                      int cx0, int cy0, int cx1, int cy1) const
    {
        const Vert a = Prep(i0), b = Prep(i1), c = Prep(i2);

        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (area > -1e-6f && area < 1e-6f) return;      // degenerate
        const float inv = 1.0f / area;

        int minx = (int)std::floor(fminf(fminf(a.x, b.x), c.x));
        int maxx = (int)std::ceil (fmaxf(fmaxf(a.x, b.x), c.x));
        int miny = (int)std::floor(fminf(fminf(a.y, b.y), c.y));
        int maxy = (int)std::ceil (fmaxf(fmaxf(a.y, b.y), c.y));
        if (minx < cx0) minx = cx0;  if (maxx > cx1 - 1) maxx = cx1 - 1;
        if (miny < cy0) miny = cy0;  if (maxy > cy1 - 1) maxy = cy1 - 1;
        if (minx > maxx || miny > maxy) return;
        if (minx < 0) minx = 0;  if (miny < 0) miny = 0;
        if (maxx > fb.w - 1) maxx = fb.w - 1;
        if (maxy > fb.h - 1) maxy = fb.h - 1;

        for (int y = miny; y <= maxy; ++y) {
            const float py = (float)y + 0.5f;
            for (int x = minx; x <= maxx; ++x) {
                const float px = (float)x + 0.5f;
                // barycentric
                float w0 = ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) * inv;
                float w1 = ((c.x - b.x) * (py - b.y) - (px - b.x) * (c.y - b.y)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

                // w0 is the weight of C, w1 of A, w2 of B (each edge function
                // measures the area opposite its vertex). Applying them in
                // a,b,c order shears every textured quad -- the classic
                // barycentric indexing bug.
                const float wA = w1, wB = w2, wC = w0;
                const float r = a.r * wA + b.r * wB + c.r * wC;
                const float g = a.g * wA + b.g * wB + c.g * wC;
                const float bl= a.b * wA + b.b * wB + c.b * wC;
                float       al= a.a * wA + b.a * wB + c.a * wC;
                if (al <= 0.0f) continue;

                if (textured) {
                    const float u = a.u * wA + b.u * wB + c.u * wC;
                    const float v = a.v * wA + b.v * wB + c.v * wC;
                    al *= SampleAlpha(u, v);
                    if (al <= 0.0f) continue;
                }
                Blend(fb, x, y, r, g, bl, al);
            }
        }
    }

    const uint8_t* m_tex = nullptr;
    int m_tex_w = 0, m_tex_h = 0;
    ImTextureID m_tex_id = nullptr;
};

// =============================================================================
//  Minimal PNG writer (stored/uncompressed deflate -- no zlib dependency)
// =============================================================================
namespace png {

inline uint32_t Crc32(const uint8_t* data, size_t len)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline uint32_t Adler32(const uint8_t* data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) { a = (a + data[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

inline void PutBE32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

inline void Chunk(std::vector<uint8_t>& out, const char* type,
                  const uint8_t* data, size_t len)
{
    PutBE32(out, (uint32_t)len);
    const size_t tpos = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    PutBE32(out, Crc32(&out[tpos], 4 + len));
}

// Writes an RGBA8 PNG. Uncompressed, so files are large but the writer is ~80
// lines with zero dependencies.
inline bool WriteRGBA(const char* path, int w, int h, const uint8_t* rgba)
{
    std::vector<uint8_t> out;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;    // bit depth
    ihdr[9] = 6;    // colour type: RGBA
    ihdr[10] = 0;   // compression
    ihdr[11] = 0;   // filter
    ihdr[12] = 0;   // interlace
    Chunk(out, "IHDR", ihdr, 13);

    // Raw scanlines, each prefixed by filter type 0 (None).
    const size_t stride = (size_t)w * 4;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * (size_t)h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * stride, rgba + (size_t)y * stride + stride);
    }

    // zlib wrapper + stored deflate blocks
    std::vector<uint8_t> idat;
    idat.push_back(0x78); idat.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = raw.size() - off;
        if (n > 65535) n = 65535;
        const bool last = (off + n >= raw.size());
        idat.push_back(last ? 0x01 : 0x00);
        idat.push_back((uint8_t)(n & 0xFF)); idat.push_back((uint8_t)((n >> 8) & 0xFF));
        idat.push_back((uint8_t)(~n & 0xFF)); idat.push_back((uint8_t)((~n >> 8) & 0xFF));
        idat.insert(idat.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    PutBE32(idat, Adler32(raw.data(), raw.size()));
    Chunk(out, "IDAT", idat.data(), idat.size());
    Chunk(out, "IEND", nullptr, 0);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const size_t wrote = fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return wrote == out.size();
}

} // namespace png

inline bool SavePNG(const char* path, const Framebuffer& fb)
{
    return png::WriteRGBA(path, fb.w, fb.h, fb.px.data());
}

} // namespace softras
