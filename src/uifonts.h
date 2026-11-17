// Diorama - the UI's fonts, as Unity's Font assets: a .ttf or .otf in the project, picked on a Text, Button or Toggle.
// The editor and the Windows player draw the UI with Dear ImGui, so a font goes into ImGui's atlas the first time it's
// asked for (once, at 48 px, drawn at any size), before the next frame. The browser gets it through @font-face
// (diorama.js).
#pragma once
#include <imgui.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace UiFonts {

inline std::map<std::string, ImFont*>& cache() { static std::map<std::string, ImFont*> c; return c; }
inline std::set<std::string>& wanted() { static std::set<std::string> w; return w; }

// the font for a file: the default until it's loaded (it's loaded before the next frame), or if it can't be
inline ImFont* get(const std::string& file) {
    if (file.empty()) return ImGui::GetFont();
    auto it = cache().find(file);
    if (it != cache().end()) return it->second ? it->second : ImGui::GetFont();
    wanted().insert(file);
    return ImGui::GetFont();
}

// call before ImGui::NewFrame: loads what was asked for. True when the atlas changed: the renderer's font texture has
// to be made again (ImGui_ImplOpenGL3_DestroyFontsTexture, then CreateFontsTexture)
inline bool load() {
    if (wanted().empty()) return false;
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x2010, 0x205E, 0x20A0, 0x20BF, 0x2190, 0x21FF, 0};
    ImGuiIO& io = ImGui::GetIO();
    bool added = false;
    for (const std::string& f : wanted()) {
        std::error_code ec;
        ImFont* font = nullptr;
        if (std::filesystem::is_regular_file(std::filesystem::u8path(f), ec)) {
            ImFontConfig cfg; cfg.OversampleH = 2; cfg.OversampleV = 2;
            font = io.Fonts->AddFontFromFileTTF(f.c_str(), 48.0f, &cfg, ranges);
            added = added || font;
        }
        cache()[f] = font;
    }
    wanted().clear();
    return added;
}

}
