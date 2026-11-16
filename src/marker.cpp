// Diorama - markers (see marker.h)
#include "marker.h"
#include "aruco_dicts.h"

#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Markers {

static const float MARGIN = 0.10f;   // white round the black square, of the whole printed marker
static const float RATIO = 0.5f;     // the picture inside the black square, of its width (ARToolKit's pattRatio)

bool load(const std::string& path, Image& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string d = ss.str();
    int w, h, n;
    unsigned char* p = stbi_load_from_memory((const unsigned char*)d.data(), (int)d.size(), &w, &h, &n, 4);
    if (!p) return false;
    out.w = w; out.h = h; out.px.assign(p, p + (size_t)w * h * 4);
    stbi_image_free(p);
    return true;
}

static void writeTo(void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, size); }
bool savePng(const std::string& path, const Image& im) {
    std::string out;
    if (!stbi_write_png_to_func(writeTo, &out, im.w, im.h, 4, im.px.data(), im.w * 4)) return false;
    std::ofstream f(fs::u8path(path), std::ios::binary);
    f << out;
    return (bool)f;
}

// a box average when shrinking, bilinear when growing; the alpha laid onto white (a printed marker has no see-through)
Image resized(const Image& im, int w, int h) {
    Image o; o.w = w; o.h = h; o.px.assign((size_t)w * h * 4, 255);
    if (im.w <= 0 || im.h <= 0) return o;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float x0 = (float)x * im.w / w, x1 = (float)(x + 1) * im.w / w, y0 = (float)y * im.h / h, y1 = (float)(y + 1) * im.h / h;
            float acc[4] = {0, 0, 0, 0}, wsum = 0;
            if (x1 - x0 <= 1.0f && y1 - y0 <= 1.0f) {   // growing: bilinear
                const float sx = std::max(0.0f, (x + 0.5f) * im.w / w - 0.5f), sy = std::max(0.0f, (y + 0.5f) * im.h / h - 0.5f);
                const int ix = std::min((int)sx, im.w - 1), iy = std::min((int)sy, im.h - 1), jx = std::min(ix + 1, im.w - 1), jy = std::min(iy + 1, im.h - 1);
                const float fx = sx - ix, fy = sy - iy;
                for (int c = 0; c < 4; ++c) {
                    auto at = [&](int xx, int yy) { return (float)im.px[((size_t)yy * im.w + xx) * 4 + c]; };
                    acc[c] = (at(ix, iy) * (1 - fx) + at(jx, iy) * fx) * (1 - fy) + (at(ix, jy) * (1 - fx) + at(jx, jy) * fx) * fy;
                }
                wsum = 1;
            } else
                for (int yy = (int)y0; yy < std::min(im.h, (int)std::ceil(y1)); ++yy)
                    for (int xx = (int)x0; xx < std::min(im.w, (int)std::ceil(x1)); ++xx) {
                        const float wx = std::min((float)xx + 1, x1) - std::max((float)xx, x0), wy = std::min((float)yy + 1, y1) - std::max((float)yy, y0);
                        const float wt = std::max(0.0f, wx) * std::max(0.0f, wy);
                        const unsigned char* p = &im.px[((size_t)yy * im.w + xx) * 4];
                        for (int c = 0; c < 4; ++c) acc[c] += p[c] * wt;
                        wsum += wt;
                    }
            unsigned char* q = &o.px[((size_t)y * w + x) * 4];
            const float a = wsum > 0 ? acc[3] / wsum / 255.0f : 1;
            for (int c = 0; c < 3; ++c) q[c] = (unsigned char)std::clamp(std::lround((wsum > 0 ? acc[c] / wsum : 255) * a + 255 * (1 - a)), 0L, 255L);
            q[3] = 255;
        }
    return o;
}

std::string pattFrom(const Image& inside) {
    const int N = 16;
    Image s = resized(inside, N, N);
    std::string out;
    char b[8];
    for (int k = 0; k < 4; ++k) {   // the four turns, a quarter anticlockwise each (as the trainer turns its canvas)
        if (k) out += "\n";
        for (int ch = 2; ch >= 0; --ch) {   // blue, green, red
            for (int y = 0; y < N; ++y) {
                for (int x = 0; x < N; ++x) {
                    snprintf(b, sizeof b, "%4d", s.px[((size_t)y * N + x) * 4 + ch]);
                    out += b;
                }
                out += "\n";
            }
        }
        // turn a quarter anticlockwise: the new (x, y) is the old (N-1-y, x)
        Image r = s;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                memcpy(&r.px[((size_t)y * N + x) * 4], &s.px[((size_t)x * N + (N - 1 - y)) * 4], 4);
        s = r;
    }
    return out;
}

static Image blank(int w, int h) { Image o; o.w = w; o.h = h; o.px.assign((size_t)w * h * 4, 255); return o; }
static void fillRect(Image& im, int x0, int y0, int x1, int y1, unsigned char v) {
    for (int y = std::max(0, y0); y < std::min(im.h, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(im.w, x1); ++x) { unsigned char* p = &im.px[((size_t)y * im.w + x) * 4]; p[0] = p[1] = p[2] = v; p[3] = 255; }
}
static void paste(Image& dst, const Image& src, int ox, int oy) {
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            const int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            memcpy(&dst.px[((size_t)dy * dst.w + dx) * 4], &src.px[((size_t)y * src.w + x) * 4], 4);
        }
}

Image printablePattern(const Image& inside, int size) {
    Image o = blank(size, size);
    const int m = (int)std::lround(size * MARGIN), sq = size - 2 * m;
    fillRect(o, m, m, m + sq, m + sq, 0);
    const int in = (int)std::lround(sq * RATIO), io = m + (sq - in) / 2;
    paste(o, resized(inside, in, in), io, io);
    return o;
}

Image printableBarcode(int id, int size) {
    Image o = blank(size, size);
    const int m = (int)std::lround(size * MARGIN), sq = size - 2 * m;
    fillRect(o, m, m, m + sq, m + sq, 0);
    // the 3x3 cells fill the middle half (pattRatio 0.5); black = 1. The corners say which way up it is:
    // top-left and bottom-left black, bottom-right white; the other six cells are the number, first bit first
    const float in = sq * RATIO, io = m + (sq - in) / 2, cell = in / 3;
    bool bits[3][3] = {};
    bits[0][0] = true; bits[2][0] = true; bits[2][2] = false;
    const int order[6][2] = {{0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2}, {2, 1}};   // (row, column), most significant first
    for (int k = 0; k < 6; ++k) bits[order[k][0]][order[k][1]] = (id >> (5 - k)) & 1;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            fillRect(o, (int)std::lround(io + c * cell), (int)std::lround(io + r * cell), (int)std::lround(io + (c + 1) * cell), (int)std::lround(io + (r + 1) * cell), bits[r][c] ? 0 : 255);
    return o;
}

Image printableAruco(int dict, int id, int size) {
    const Aruco::Dict& d = Aruco::DICTS[std::clamp(dict, 0, Aruco::DICT_COUNT - 1)];
    id = std::clamp(id, 0, d.count - 1);
    Image o = blank(size, size);
    const int m = (int)std::lround(size * MARGIN), sq = size - 2 * m, cells = d.size + 2;
    const float cell = (float)sq / cells;
    fillRect(o, m, m, m + sq, m + sq, 0);   // the border (black), then the white cells
    const unsigned char* b = d.bits + (size_t)id * d.size * d.size;
    for (int r = 0; r < d.size; ++r)
        for (int c = 0; c < d.size; ++c)
            if (b[r * d.size + c])
                fillRect(o, (int)std::lround(m + (c + 1) * cell), (int)std::lround(m + (r + 1) * cell), (int)std::lround(m + (c + 2) * cell), (int)std::lround(m + (r + 2) * cell), 255);
    return o;
}
int arucoCount(int dict) { return Aruco::DICTS[std::clamp(dict, 0, Aruco::DICT_COUNT - 1)].count; }
const char* arucoName(int dict) { return Aruco::DICTS[std::clamp(dict, 0, Aruco::DICT_COUNT - 1)].name; }

// ---- text, for letter markers and the sheet's labels
static std::string fontData(const std::string& file) {
    std::ifstream f(fs::u8path(file), std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static void drawText(Image& im, const std::string& text, float px, float cx, float baseline, const std::string& font, bool centre, unsigned char v) {
    const std::string d = fontData(font);
    if (d.empty()) return;
    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, (const unsigned char*)d.data(), 0)) return;
    const float sc = stbtt_ScaleForPixelHeight(&fi, px);
    float width = 0;
    for (char ch : text) { int adv, lsb; stbtt_GetCodepointHMetrics(&fi, (unsigned char)ch, &adv, &lsb); width += adv * sc; }
    float x = centre ? cx - width / 2 : cx;
    for (char ch : text) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&fi, (unsigned char)ch, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&fi, (unsigned char)ch, sc, sc, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> g((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&fi, g.data(), gw, gh, gw, sc, sc, (unsigned char)ch);
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    const int dx = (int)x + x0 + xx, dy = (int)baseline + y0 + yy;
                    if (dx < 0 || dy < 0 || dx >= im.w || dy >= im.h) continue;
                    unsigned char* p = &im.px[((size_t)dy * im.w + dx) * 4];
                    const float a = g[(size_t)yy * gw + xx] / 255.0f;
                    for (int c = 0; c < 3; ++c) p[c] = (unsigned char)std::lround(p[c] * (1 - a) + v * a);
                }
        }
        x += adv * sc;
    }
}

Image letterPicture(const std::string& text, int size, const std::string& fontFile) {
    Image o = blank(size, size);
    const float px = text.size() <= 1 ? size * 0.95f : text.size() == 2 ? size * 0.62f : size * 1.6f / (float)text.size();
    drawText(o, text, px, size / 2.0f, size * 0.5f + px * 0.36f, fontFile, true, 0);
    return o;
}

Image sheet(const std::vector<std::pair<Image, std::string>>& markers, const std::string& fontFile) {
    const int W = 1240, H = 1754, cols = 2, rows = 3, cellW = W / cols, cellH = (H - 80) / rows;   // A4 at 150 dpi
    Image o = blank(W, H);
    const int msize = std::min(cellW, cellH - 50) - 60;
    for (size_t i = 0; i < markers.size() && i < (size_t)(cols * rows); ++i) {
        const int cx = (int)(i % cols) * cellW + cellW / 2, cy = 40 + (int)(i / cols) * cellH;
        paste(o, resized(markers[i].first, msize, msize), cx - msize / 2, cy + 10);
        drawText(o, markers[i].second, 26, (float)cx, (float)(cy + 10 + msize + 34), fontFile, true, 60);
    }
    drawText(o, "Made with Diorama. Print at 100%; keep the white margin round each marker.", 20, W / 2.0f, H - 24.0f, fontFile, true, 120);
    return o;
}

}
