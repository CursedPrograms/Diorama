// Diorama - the splash: a little world in a box standing on a printed marker (a frame, the trees behind it, a moon,
// a butterfly coming out), the name, the version, the credits and what's loading. Atelier greys and blue, solid;
// drawn with ImGui's draw list so it's sharp at any size.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef DIORAMA_VERSION
#define DIORAMA_VERSION "0.1.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;
void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255), DEEP = IM_COL32(0x00, 0x40, 0x67, 255), LIGHT = IM_COL32(0x34, 0xa9, 0xe3, 255);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f ? f : ImGui::GetFont(), size, p, col, s); }
static float textWidth(ImFont* f, float size, const char* s) { return (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0, s).x; }

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);

    // the printed marker on the table, in perspective
    const float cx = x + 470, by = y + 300;
    const ImVec2 m0(cx - 120, by + 40), m1(cx + 120, by + 40), m2(cx + 80, by - 10), m3(cx - 80, by - 10);
    dl->AddQuadFilled(m0, m1, m2, m3, IM_COL32(0xd0, 0xd0, 0xd0, 255));
    auto lerp = [](ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
    auto in = [&](float u, float v) { return lerp(lerp(m0, m1, u), lerp(m3, m2, u), v); };
    dl->AddQuadFilled(in(0.12f, 0.12f), in(0.88f, 0.12f), in(0.88f, 0.88f), in(0.12f, 0.88f), IM_COL32(0x10, 0x10, 0x10, 255));
    dl->AddQuadFilled(in(0.32f, 0.32f), in(0.68f, 0.32f), in(0.68f, 0.68f), in(0.32f, 0.68f), IM_COL32(0xd0, 0xd0, 0xd0, 255));
    // the box standing on it: a frame, the world inside
    const float bw = 150, bh = 170, bx = cx - bw / 2, bt = by - 10 - bh;
    dl->AddRectFilled(ImVec2(bx, bt), ImVec2(bx + bw, by - 10), DEEP);
    dl->AddCircleFilled(ImVec2(bx + bw * 0.72f, bt + 42), 18, IM_COL32(0xe6, 0xe6, 0xe6, 255));   // the moon
    for (int k = 0; k < 5; ++k) {   // trees
        const float tx = bx + 18 + k * 30, th = 60.0f + (k % 2) * 30;
        dl->AddTriangleFilled(ImVec2(tx, by - 10 - th), ImVec2(tx - 16, by - 10), ImVec2(tx + 16, by - 10), k % 2 ? BLUE : IM_COL32(0x00, 0x5a, 0x88, 255));
    }
    dl->AddRect(ImVec2(bx - 6, bt - 6), ImVec2(bx + bw + 6, by - 4), IM_COL32(0x9a, 0x9a, 0x9a, 255), 0, 0, 6);
    // a butterfly flying out of the frame
    const ImVec2 b(bx + bw + 28, bt + 30);
    dl->AddTriangleFilled(b, ImVec2(b.x - 14, b.y - 12), ImVec2(b.x - 12, b.y + 4), LIGHT);
    dl->AddTriangleFilled(b, ImVec2(b.x + 14, b.y - 12), ImVec2(b.x + 12, b.y + 4), LIGHT);

    // the name: "Dio" in the text colour, "rama" in blue
    const float ts = 50, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Dio");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Dio"), ty), BLUE, "rama");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 62), DIM, "Version " DIORAMA_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 84), DIM, "Little worlds on markers, by Cursed Entertainment");

    const float cs = 13, cry = y + 306;
    text(dl, smallFont, cs, ImVec2(tx, cry), DIM, "Exports to jsartoolkit5 (LGPL-3.0) and three.js (MIT).");
    text(dl, smallFont, cs, ImVec2(tx, cry + 17), DIM, "Built with Dear ImGui, SDL2, cgltf and stb.");
    text(dl, smallFont, cs, ImVec2(tx, cry + 34), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");

    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), BLUE);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}
