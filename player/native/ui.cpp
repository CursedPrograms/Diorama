// Diorama's native player: the Canvas UI with Dear ImGui. Keep in step with diorama.js's uiElement / uiPlace.
#include "ui.h"
#include "script.h"
#include "viewer.h"

#include <imgui.h>
#include "uifonts.h"

#include <algorithm>
#include <map>

using Scene::Object;

namespace Ui {

static bool overUi = false;
bool wantsMouse() { return overUi; }

static ImU32 col(const float* c) { return IM_COL32((int)(std::clamp(c[0], 0.f, 1.f) * 255), (int)(std::clamp(c[1], 0.f, 1.f) * 255), (int)(std::clamp(c[2], 0.f, 1.f) * 255), (int)(std::clamp(c[3], 0.f, 1.f) * 255)); }

struct Rect { float x, y, w, h; };
static void drawText(ImDrawList* dl, const Rect& r, const std::string& text, float size, int align, ImU32 c, const std::string& font = "") {
    ImFont* f = UiFonts::get(font);
    const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, r.w, text.c_str());
    const float x = align == 0 ? r.x : align == 2 ? r.x + r.w - ts.x : r.x + (r.w - ts.x) / 2;
    dl->AddText(f, size, ImVec2(x, r.y + (r.h - ts.y) / 2), c, text.c_str(), nullptr, r.w);
}

void draw(Scene::Project& p, const std::vector<bool>& seen) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y, s = std::min(W / 400.0f, H / 711.0f);
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##canvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    overUi = false;
    for (int a = -1; a < (int)p.markers.size(); ++a) {
        Scene::Marker& m = a < 0 ? p.camera : p.markers[a];
        const bool anchorSeen = a < 0 || (a < (int)seen.size() && seen[a]);
        std::map<int, Rect> rectOf; std::map<int, bool> shown; std::map<int, bool> inCanvas;
        for (Object& o : m.objects) {   // parents come first in the list
            const bool parentShown = o.parent == -1 || (shown.count(o.parent) && shown[o.parent]);
            bool on = parentShown && o.active && (o.ui == 0 || o.uiOn);
            const bool canvasHere = o.hasCanvas || (o.parent != -1 && inCanvas.count(o.parent) && inCanvas[o.parent]);
            inCanvas[o.id] = canvasHere;
            if (o.hasCanvas && o.canvasWithAnchor && !anchorSeen) on = false;
            shown[o.id] = on;
            const Rect parent = o.parent != -1 && rectOf.count(o.parent) ? rectOf[o.parent] : Rect{0, 0, W, H};
            if (o.hasCanvas) { rectOf[o.id] = Rect{0, 0, W, H}; continue; }
            if (!canvasHere || o.ui == Scene::UI_NONE) { rectOf[o.id] = parent; continue; }
            const int ax = o.uiAnchor % 3, ay = o.uiAnchor / 3;
            const float w = s * o.uiRect[2], h = s * o.uiRect[3];
            Rect r{parent.x + ax * parent.w / 2 + s * o.uiRect[0] - ax * w / 2, parent.y + ay * parent.h / 2 + s * o.uiRect[1] - ay * h / 2, w, h};
            if (o.uiRole) {   // Stretch, and a Slider's Fill and Handle (at its slider's value)
                float t = 0;
                for (int id = o.parent, guard = 0; id >= 0 && guard < 64; ++guard) {
                    const Object* p = nullptr; for (const Object& q : m.objects) if (q.id == id) p = &q;
                    if (!p) break;
                    if (p->ui == Scene::UI_SLIDER) { t = std::clamp((p->uiValue - p->uiMin) / std::max(1e-6f, p->uiMax - p->uiMin), 0.0f, 1.0f); break; }
                    id = p->parent;
                }
                if (o.uiRole == 1) r = Rect{parent.x + s * o.uiRect[0], parent.y + s * o.uiRect[1], parent.w - 2 * s * o.uiRect[0], parent.h - 2 * s * o.uiRect[1]};
                else if (o.uiRole == 2) r = Rect{parent.x, parent.y + s * o.uiRect[1], parent.w * t, parent.h - 2 * s * o.uiRect[1]};
                else r = Rect{parent.x + parent.w * t - w / 2, parent.y + parent.h / 2 - h / 2, w, h};
            }
            rectOf[o.id] = r;
            if (!on) continue;
            const float font = s * o.uiFont;
            ImGui::PushID(o.id);
            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
            switch (o.ui) {
            case Scene::UI_TEXT: drawText(dl, r, o.uiText, font, o.uiAlign, col(o.uiColour), o.uiFontFile); break;
            case Scene::UI_PANEL:
                dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), col(o.uiBg), o.uiRole == 3 ? std::min(r.w, r.h) / 2 : s * 6);
                if (!o.uiRole) ImGui::InvisibleButton("##p", ImVec2(std::max(1.f, r.w), std::max(1.f, r.h)));   // (a slider's parts leave the clicks to it)
                break;
            case Scene::UI_IMAGE: {
                int iw = 0, ih = 0; const unsigned t = o.uiFile.empty() ? 0 : Viewer::pictureTexture(o.uiFile, &iw, &ih);
                if (t && iw > 0 && ih > 0) {   // object-fit: contain
                    const float k = std::min(r.w / iw, r.h / ih), dw = iw * k, dh = ih * k, x = r.x + (r.w - dw) / 2, y = r.y + (r.h - dh) / 2;
                    dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(x, y), ImVec2(x + dw, y + dh), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(std::clamp(o.uiColour[3], 0.f, 1.f) * 255)));
                }
                break;
            }
            case Scene::UI_BUTTON: {
                const bool pressed = ImGui::InvisibleButton("##b", ImVec2(std::max(1.f, r.w), std::max(1.f, r.h)));
                float bg[4] = {o.uiBg[0], o.uiBg[1], o.uiBg[2], o.uiBg[3]};
                if (ImGui::IsItemActive()) for (int k = 0; k < 3; ++k) bg[k] *= 0.85f;   // :active
                dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), col(bg), s * 6);
                drawText(dl, r, o.uiText, font, 1, col(o.uiColour), o.uiFontFile);
                if (pressed) Scripts::ui(o.id, "onClick", 0, false);
                break;
            }
            case Scene::UI_SLIDER: {
                ImGui::InvisibleButton("##s", ImVec2(std::max(1.f, r.w), std::max(1.f, r.h)));
                const float lo = o.uiMin, hi = o.uiMax > o.uiMin ? o.uiMax : o.uiMin + 1;
                if (ImGui::IsItemActive()) {
                    const float v = lo + std::clamp((io.MousePos.x - r.x) / std::max(1.f, r.w), 0.f, 1.f) * (hi - lo);
                    if (v != o.uiValue) { o.uiValue = v; Scripts::ui(o.id, "onChange", v, false); }
                }
                bool parts = false; for (const Object& c : m.objects) if (c.parent == o.id) parts = true;
                if (parts) break;   // Unity's prefab: its Background, Fill and Handle draw it
                const float u = std::clamp((o.uiValue - lo) / (hi - lo), 0.f, 1.f), cy = r.y + r.h / 2, th = std::max(3.f, s * 4);
                const float grey[4] = {0.8f, 0.8f, 0.8f, 0.9f};
                dl->AddRectFilled(ImVec2(r.x, cy - th / 2), ImVec2(r.x + r.w, cy + th / 2), col(grey), th / 2);
                dl->AddRectFilled(ImVec2(r.x, cy - th / 2), ImVec2(r.x + r.w * u, cy + th / 2), col(o.uiBg), th / 2);
                dl->AddCircleFilled(ImVec2(r.x + r.w * u, cy), std::max(6.f, s * 9), col(o.uiBg));
                break;
            }
            case Scene::UI_TOGGLE: {
                if (ImGui::InvisibleButton("##t", ImVec2(std::max(1.f, r.w), std::max(1.f, r.h)))) { o.uiChecked = !o.uiChecked; Scripts::ui(o.id, "onChange", o.uiChecked ? 1 : 0, true); }
                const float b = std::min(r.h, font * 1.2f), by = r.y + (r.h - b) / 2;
                const float white[4] = {1, 1, 1, 1};
                dl->AddRectFilled(ImVec2(r.x, by), ImVec2(r.x + b, by + b), o.uiChecked ? col(o.uiBg) : col(white), s * 3);
                if (o.uiChecked) dl->AddText(ImGui::GetFont(), b, ImVec2(r.x + b * 0.18f, by - b * 0.05f), IM_COL32(255, 255, 255, 255), "\xe2\x9c\x93");
                drawText(dl, Rect{r.x + b + s * 8, r.y, r.w - b - s * 8, r.h}, o.uiText, font, 0, col(o.uiColour), o.uiFontFile);
                break;
            }
            default: break;
            }
            if (o.ui == Scene::UI_BUTTON || o.ui == Scene::UI_SLIDER || o.ui == Scene::UI_TOGGLE || o.ui == Scene::UI_PANEL)
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) overUi = true;
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}
