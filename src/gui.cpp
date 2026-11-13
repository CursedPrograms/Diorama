// Diorama - Atelier's AR engine: scenes on markers, laid out like Unity (Hierarchy, Scene, Inspector), markers made
// without a webcam, exported as jsartoolkit5 web packages that run in a phone's browser.
//
//   Diorama [project folder]
//   Diorama --capture out.bmp [--template gallery|basic] [--select N]   a screenshot from a hidden window, then quit
#include "marker.h"
#include "scene.h"
#include "splash.h"
#include "serve.h"
#include "viewer.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace fs = std::filesystem;
using namespace Scene;

#ifndef DIORAMA_VERSION
#define DIORAMA_VERSION "0.1.0"
#endif

// ---------- the Atelier look ----------
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255),
                   C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);
static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2; s.TabRounding = 2;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1; s.IndentSpacing = 16;
    s.WindowPadding = ImVec2(10, 10); s.FramePadding = ImVec2(6, 4); s.ItemSpacing = ImVec2(6, 6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(IM_COL32(0x21, 0x21, 0x21, 255));
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255)); c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_CheckMark] = col4(C_BLUE); c[ImGuiCol_SliderGrab] = col4(C_BLUE); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(C_DEEP); c[ImGuiCol_HeaderHovered] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_Tab] = col4(C_PANEL); c[ImGuiCol_TabActive] = col4(C_DEEP); c[ImGuiCol_TabHovered] = col4(C_BLUE);
    c[ImGuiCol_DragDropTarget] = col4(C_BLUE);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

static bool fileExists(const char* f) { if (FILE* t = fopen(f, "rb")) { fclose(t); return true; } return false; }
static ImFont* boldFont = nullptr;
static void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* f = "C:/Windows/Fonts/segoeui.ttf";
    if (!fileExists(f)) { io.Fonts->AddFontDefault(); return; }
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2600, 0x27BF, 0};
    auto add = [&](const char* file, float size) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
        if (fileExists("C:/Windows/Fonts/seguisym.ttf")) {
            ImFontConfig merge; merge.MergeMode = true;
            static const ImWchar sym[] = {0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2600, 0x27BF, 0};
            io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", size, &merge, sym);
        }
        return font;
    };
    add(f, 16.0f);
    boldFont = add(fileExists("C:/Windows/Fonts/seguisb.ttf") ? "C:/Windows/Fonts/seguisb.ttf" : f, 16.0f);
    const char* light = fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : f;
    Splash::setFonts(add(light, 50.0f), add(f, 13.0f));
}

// ---------- dialogs ----------
#ifdef _WIN32
static std::string openDialog(const wchar_t* filter) {
    wchar_t buf[4096] = L"";
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFilter = filter; o.lpstrFile = buf; o.nMaxFile = 4096; o.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) ? fs::path(buf).u8string() : "";
}
static std::string saveDialog(const wchar_t* filter, const wchar_t* ext, const std::string& start) {
    wchar_t buf[4096] = L"";
    wcsncpy(buf, fs::u8path(start).wstring().c_str(), 4095);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFilter = filter; o.lpstrFile = buf; o.nMaxFile = 4096; o.lpstrDefExt = ext; o.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&o) ? fs::path(buf).u8string() : "";
}
static std::string pickFolder() {
    std::string out;
    IFileOpenDialog* d = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(d->Show(nullptr))) { IShellItem* it = nullptr; if (SUCCEEDED(d->GetResult(&it))) { PWSTR p = nullptr; if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = fs::path(p).u8string(); CoTaskMemFree(p); } it->Release(); } }
        d->Release();
    }
    return out;
}
static void openInExplorer(const std::string& p) { ShellExecuteW(nullptr, L"open", fs::u8path(p).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
static std::string documents() {
    PWSTR p = nullptr; std::string out = ".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) { out = fs::path(p).u8string(); CoTaskMemFree(p); }
    return out;
}
#else
static std::string openDialog(const wchar_t*) { return ""; }
static std::string saveDialog(const wchar_t*, const wchar_t*, const std::string&) { return ""; }
static std::string pickFolder() { return ""; }
static void openInExplorer(const std::string&) {}
static std::string documents() { return "."; }
#endif

// ---------- state ----------
static SDL_Window* window = nullptr;
static bool quit = false;
static std::string exeDir, fontFile = "C:/Windows/Fonts/segoeuib.ttf";
static Project proj;
static std::string folder;                        // the project's folder ("" until saved)
static int selMarker = 0, selObject = -1;         // selObject: an object id, or -1 for the marker itself
static std::string toast; static double toastUntil = 0;
static double now() { return SDL_GetTicks() / 1000.0; }
static void say(const std::string& s) { toast = s; toastUntil = now() + 6; }
static bool showAbout = false;

static Marker* curMarker() { return selMarker >= 0 && selMarker < (int)proj.markers.size() ? &proj.markers[selMarker] : nullptr; }
static Object* curObject() {
    Marker* m = curMarker(); if (!m || selObject < 0) return nullptr;
    const int i = indexOfId(*m, selObject); return i >= 0 ? &m->objects[i] : nullptr;
}


// ---------- undo / redo: the whole project before each edit (projects are small) ----------
static std::vector<Project> undoStack, redoStack;
static Project frameStart;   // the project as this frame began
static void beginEdit() {
    undoStack.push_back(frameStart);
    if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
    redoStack.clear();
}
static void undo() { if (undoStack.empty()) return; redoStack.push_back(proj); proj = undoStack.back(); undoStack.pop_back(); if (!curObject()) selObject = -1; }
static void redo() { if (redoStack.empty()) return; undoStack.push_back(proj); proj = redoStack.back(); redoStack.pop_back(); if (!curObject()) selObject = -1; }

// ---------- the printed marker ----------
static std::string markerAssets() { return (fs::u8path(exeDir) / "assets" / "markers").u8string(); }
static Markers::Image insidePicture(const Scene::Marker& m) {
    Markers::Image im;
    if (!m.preset.empty()) {   // Hiro, Kanji: the middle half of their black square
        Markers::Image full;
        if (Markers::load((fs::u8path(markerAssets()) / (m.preset + ".png")).u8string(), full)) {
            const int x0 = full.w / 4, y0 = full.h / 4, w = full.w / 2, h = full.h / 2;
            im.w = w; im.h = h; im.px.resize((size_t)w * h * 4);
            for (int y = 0; y < h; ++y) memcpy(&im.px[(size_t)y * w * 4], &full.px[((size_t)(y + y0) * full.w + x0) * 4], (size_t)w * 4);
            return im;
        }
    }
    if (!m.letter.empty()) return Markers::letterPicture(m.letter, 512, fontFile);
    if (!m.picture.empty() && Markers::load(m.picture, im)) return im;
    return im;
}
// the whole printed marker (white margin, black square, its inside)
static Markers::Image printable(const Scene::Marker& m, int size) {
    if (m.type == MK_BARCODE) return Markers::printableBarcode(std::clamp(m.barcode, 0, 63), size);
    if (!m.preset.empty()) {   // Hiro, Kanji: their own pictures (the black square edge to edge), a margin added
        Markers::Image full;
        if (Markers::load((fs::u8path(markerAssets()) / (m.preset + ".png")).u8string(), full)) {
            Markers::Image o; o.w = o.h = size; o.px.assign((size_t)size * size * 4, 255);
            const int mg = (int)std::lround(size * 0.1), sq = size - 2 * mg;
            Markers::Image s = Markers::resized(full, sq, sq);
            for (int y = 0; y < sq; ++y) memcpy(&o.px[((size_t)(y + mg) * size + mg) * 4], &s.px[(size_t)y * sq * 4], (size_t)sq * 4);
            return o;
        }
    }
    return Markers::printablePattern(insidePicture(m), size);
}
static int shownMarker = -1; static std::string shownKey;
static void refreshMarkerPicture() {
    const Scene::Marker* m = curMarker();
    const std::string key = m ? (std::to_string(m->type) + m->preset + "|" + m->picture + "|" + m->letter + "|" + std::to_string(m->barcode)) : "";
    if (selMarker == shownMarker && key == shownKey) return;
    shownMarker = selMarker; shownKey = key;
    if (!m) { Viewer::setMarkerPicture({}, 0, 0); return; }
    const Markers::Image im = printable(*m, 512);
    Viewer::setMarkerPicture(im.px, im.w, im.h);
}

// ---------- files: kept absolute while editing, relative to the project in the file ----------
static std::string bring(const std::string& file) {   // a file into the project's assets folder (when it has one)
    if (folder.empty() || file.empty()) return file;
    std::error_code ec;
    const fs::path src = fs::u8path(file), assets = fs::u8path(folder) / "assets";
    if (src.parent_path() == assets) return file;
    fs::create_directories(assets, ec);
    fs::path dst = assets / src.filename();
    if (!fs::exists(dst, ec)) fs::copy_file(src, dst, ec);
    return dst.u8string();
}
static void forEachFile(Project& p, const std::function<void(std::string&)>& f) {
    for (auto& m : p.markers) { f(m.picture); for (auto& o : m.objects) f(o.file); }
}
static bool saveProject(bool askFolder) {
    if (folder.empty() || askFolder) {
        const std::string base = (fs::u8path(documents()) / "Diorama").u8string();
        std::error_code ec; fs::create_directories(fs::u8path(base), ec);
        const std::string f = saveDialog(L"Diorama project\0*.diorama\0\0", L"diorama", (fs::u8path(base) / (proj.title + ".diorama")).u8string());
        if (f.empty()) return false;
        // a project is a folder: <name>/project.diorama with its assets beside it
        const fs::path pf = fs::u8path(f);
        folder = (pf.parent_path() / pf.stem()).u8string();
        fs::create_directories(fs::u8path(folder), ec);
    }
    forEachFile(proj, [](std::string& s) { s = bring(s); });
    Project rel = proj;
    const fs::path base = fs::u8path(folder);
    forEachFile(rel, [&](std::string& s) { if (!s.empty()) { std::error_code ec; const fs::path r = fs::relative(fs::u8path(s), base, ec); if (!ec && !r.empty() && r.u8string().rfind("..", 0) != 0) s = r.generic_u8string(); } });
    std::string err;
    if (!Scene::save(rel, folder, err)) { say(err); return false; }
    say("Saved " + (fs::u8path(folder) / "project.diorama").u8string());
    return true;
}
static void openProject(const std::string& path) {
    fs::path p = fs::u8path(path);
    if (p.extension() == ".diorama") p = p.parent_path();
    Project np; std::string err;
    if (!Scene::load(np, p.u8string(), err)) { say(err); return; }
    forEachFile(np, [&](std::string& s) { if (!s.empty() && fs::u8path(s).is_relative()) s = (p / fs::u8path(s)).u8string(); });
    proj = np; folder = p.u8string(); selMarker = 0; selObject = -1; shownMarker = -1;
    say("Opened " + proj.title);
}
static void newFromTemplate(int which) {
    const std::string dir = (fs::u8path(exeDir) / "templates" / "gallery").u8string() + "/";
    proj = which == 1 ? galleryTemplate(dir) : basicTemplate();
    folder.clear(); selMarker = 0; selObject = -1; shownMarker = -1;
    Viewer::frameMarker();
}

// ---------- creating ----------
static void create(int kind) {
    Marker* m = curMarker();
    if (!m) { Scene::Marker k; k.id = newId(proj); k.name = "AR Marker"; proj.markers.push_back(k); selMarker = 0; m = curMarker(); }
    // under the selected object, like Unity's "Create Empty Child" when something's selected
    const int parent = curObject() ? selObject : -1;
    Object o = make(proj, kind, parent);
    if (kind == PICTURE || kind == SPRITES) {
        const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg\0\0");
        if (f.empty()) return;
        o.file = bring(f); o.name = fs::u8path(f).stem().u8string();
    } else if (kind == MODEL) {
        const std::string f = openDialog(L"3D models (glTF)\0*.glb;*.gltf\0\0");
        if (f.empty()) return;
        o.file = bring(f); o.name = fs::u8path(f).stem().u8string();
        const auto clips = Viewer::clipsOf(o.file);
        if (!clips.empty()) o.clip = clips[0];
    }
    m->objects.push_back(o);
    reparent(*m, o.id, parent);
    selObject = o.id;
}
static void addMarker() {
    Scene::Marker k; k.id = newId(proj);
    int n = (int)proj.markers.size();
    k.name = "AR Marker " + std::to_string(n + 1);
    k.type = n == 0 ? MK_PATTERN : MK_BARCODE; k.preset = n == 0 ? "hiro" : ""; k.barcode = n;
    proj.markers.push_back(k);
    selMarker = (int)proj.markers.size() - 1; selObject = -1;
}


// ---------- Build: the game as a static website (jsartoolkit5 + three.js), for phones and any web host ----------
static std::string lastBuild;
static bool copyTree(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}
static std::string slugOf(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (char)(isalnum(c) ? tolower(c) : '-');
    while (o.find("--") != std::string::npos) o.erase(o.find("--"), 1);
    while (!o.empty() && o.back() == '-') o.pop_back();
    while (!o.empty() && o.front() == '-') o.erase(0, 1);
    return o.empty() ? "game" : o;
}
static bool buildGame(const std::string& out, std::string& err) {
    std::error_code ec;
    const fs::path o = fs::u8path(out), web = fs::u8path(exeDir) / "assets" / "web";
    fs::create_directories(o / "assets", ec); fs::create_directories(o / "markers", ec);
    if (!copyTree(web / "lib", o / "lib") || !copyTree(web / "data", o / "data")) { err = "Couldn't copy the player into " + out; return false; }
    fs::copy_file(web / "diorama.js", o / "lib" / "diorama.js", fs::copy_options::overwrite_existing, ec);
    // the files the scene uses, each once, under assets/
    Project b = proj;
    std::map<std::string, std::string> done;
    auto take = [&](std::string& f) {
        if (f.empty()) return;
        auto it = done.find(f);
        if (it != done.end()) { f = it->second; return; }
        const fs::path src = fs::u8path(f);
        std::string name = src.filename().u8string();
        for (int k = 2; fs::exists(o / "assets" / fs::u8path(name), ec) && std::none_of(done.begin(), done.end(), [&](const auto& d) { return d.second == "assets/" + name; }) == false; ++k)
            name = src.stem().u8string() + "-" + std::to_string(k) + src.extension().u8string();
        fs::copy_file(src, o / "assets" / fs::u8path(name), fs::copy_options::overwrite_existing, ec);
        const std::string key = f; f = "assets/" + name; done[key] = f;
    };
    // the markers: their pattern files (made from their pictures), their printable pictures, a sheet of them all
    std::vector<std::pair<Markers::Image, std::string>> sheet;
    for (size_t i = 0; i < b.markers.size(); ++i) {
        Scene::Marker& m = b.markers[i];
        const std::string slug = slugOf(m.name) + "-" + std::to_string(i + 1);
        for (auto& ob : m.objects) take(ob.file);
        if (m.type == MK_PATTERN) {
            // every pattern file is written from the very picture that's printed, so the two always agree
            std::ofstream(o / "markers" / (slug + ".patt"), std::ios::binary) << Markers::pattFrom(insidePicture(m));
            m.patt = "markers/" + slug + ".patt";
        }
        m.picture.clear();
        const Markers::Image im = printable(proj.markers[i], 1024);
        Markers::savePng((o / "markers" / (slug + ".png")).u8string(), im);
        sheet.push_back({im, m.name + (m.type == MK_BARCODE ? "  (barcode " + std::to_string(m.barcode) + ")" : "")});
    }
    if (!sheet.empty()) Markers::savePng((o / "markers.png").u8string(), Markers::sheet(sheet, fontFile));
    fs::copy_file(fs::u8path(exeDir) / "assets" / "logo.png", o / "icon.png", fs::copy_options::overwrite_existing, ec);
    { std::ofstream f(o / "scene.json", std::ios::binary); f << Scene::toJson(b); if (!f) { err = "Couldn't write scene.json"; return false; } }
    { std::ofstream f(o / "scene.js", std::ios::binary); f << "// the scene, inside the page (so it needs no loading)\nwindow.DIORAMA_SCENE = " << Scene::toJson(b) << ";\n"; }
    // Play.bat: play it on this PC without a web host (a tiny local server: browsers only let a page load its pictures
    // and use the webcam when it comes from a server)
    std::ofstream(o / "play.ps1", std::ios::binary) <<
        "# Serves this folder on http://localhost:8765/ and opens it: the game, played on this PC.\r\n"
        "$root = $PSScriptRoot; $port = 8765\r\n"
        "$l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $port)\r\n"
        "try { $l.Start() } catch { Write-Host \"Port $port is busy: is it already open?\"; Start-Process \"http://localhost:$port/\"; exit }\r\n"
        "Start-Process \"http://localhost:$port/\"\r\n"
        "Write-Host \"Playing on http://localhost:$port/  (close this window to stop)\"\r\n"
        "$types = @{ '.html'='text/html'; '.js'='application/javascript'; '.json'='application/json'; '.png'='image/png'; '.jpg'='image/jpeg';\r\n"
        "  '.jpeg'='image/jpeg'; '.glb'='model/gltf-binary'; '.gltf'='model/gltf+json'; '.wasm'='application/wasm'; '.mp3'='audio/mpeg'; '.ogg'='audio/ogg'; '.wav'='audio/wav' }\r\n"
        "while ($true) {\r\n"
        "  $c = $l.AcceptTcpClient(); $s = $c.GetStream(); $r = [System.IO.StreamReader]::new($s)\r\n"
        "  $line = $r.ReadLine(); while (($h = $r.ReadLine()) -ne '' -and $h -ne $null) {}\r\n"
        "  $path = if ($line) { [Uri]::UnescapeDataString(($line -split ' ')[1].Split('?')[0]) } else { '/' }\r\n"
        "  if ($path.EndsWith('/')) { $path += 'index.html' }\r\n"
        "  $file = [System.IO.Path]::GetFullPath((Join-Path $root $path.TrimStart('/')))\r\n"
        "  if ($file.StartsWith($root) -and (Test-Path $file -PathType Leaf)) {\r\n"
        "    $body = [System.IO.File]::ReadAllBytes($file); $t = $types[[System.IO.Path]::GetExtension($file).ToLower()]; if (-not $t) { $t = 'application/octet-stream' }\r\n"
        "    $head = \"HTTP/1.0 200 OK`r`nContent-Type: $t`r`nContent-Length: $($body.Length)`r`nCache-Control: no-store`r`n`r`n\"\r\n"
        "  } else { $body = [Text.Encoding]::ASCII.GetBytes('Not found'); $head = \"HTTP/1.0 404 Not Found`r`nContent-Length: 9`r`n`r`n\" }\r\n"
        "  $hb = [Text.Encoding]::ASCII.GetBytes($head); try { $s.Write($hb, 0, $hb.Length); $s.Write($body, 0, $body.Length) } catch {}\r\n"
        "  $c.Close()\r\n"
        "}\r\n";
    std::ofstream(o / "Play.bat", std::ios::binary) <<
        "@echo off\r\nrem Plays this game on this PC: a little local server, then the browser (allow the webcam).\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File \"%~dp0play.ps1\"\r\n";
    std::ofstream(o / "play.sh", std::ios::binary) <<
        "#!/usr/bin/env bash\n# Plays this game on this PC: a little local server, then the browser (allow the webcam).\n"
        "cd \"$(dirname \"$0\")\"\n(sleep 1; xdg-open http://localhost:8765/ 2>/dev/null || open http://localhost:8765/) &\n"
        "python3 -m http.server 8765 --bind 127.0.0.1\n";
    // the page
    std::string title;
    for (char c : proj.title) { if (c == '<') title += "&lt;"; else if (c == '&') title += "&amp;"; else title += c; }
    std::ofstream h(o / "index.html", std::ios::binary);
    h << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"UTF-8\">\n"
         "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
         "  <title>" << title << "</title>\n"
         "  <style>\n    html, body { margin: 0; background: #111; color: #e6e6e6; font-family: 'Segoe UI', Arial, sans-serif; overflow: hidden; }\n"
         "    canvas { display: block; margin: 0 auto; }\n"
         "    #hint { position: fixed; left: 0; right: 0; bottom: 14px; text-align: center; font-size: 14px; opacity: 0.8; }\n"
         "    #hint a { color: #34a9e3; }\n  </style>\n</head>\n<body>\n"
         "  <div id=\"hint\">Point the camera at the marker. <a href=\"markers.png\" target=\"_blank\">Print the markers</a></div>\n"
         "  <script src=\"lib/three.min.js\"></script>\n  <script src=\"lib/GLTFLoader.js\"></script>\n"
         "  <script src=\"lib/diorama.js\"></script>\n  <script src=\"scene.js\"></script>\n"
         "  <script>Diorama.start('scene.json');</script>\n"
         "  <script async src=\"lib/artoolkit.min.js\"></script>\n  <script async src=\"lib/artoolkit.three.js\"></script>\n"
         "  <!-- Made with Diorama (Cursed Entertainment). jsartoolkit5: LGPL-3.0; three.js: MIT (see lib/). -->\n"
         "</body>\n</html>\n";
    std::ofstream(o / "README.txt", std::ios::binary) <<
        proj.title << ", made with Diorama.\n\n"
        "To play it on this PC: double-click Play.bat (opening index.html straight from the disk can't load the game).\n\n"
        "A static website: put this folder on any web host with https (GitHub Pages, Netlify, itch.io) and open it on a phone;\n"
        "phones only let a page use the camera over https. Print markers.png at 100% and point the camera at a marker.\n"
        "On this PC: Diorama's File > Build And Run serves it on http://localhost (the browser allows the webcam there).\n";
    lastBuild = out;
    return true;
}
static void playInBrowser() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / "Diorama Play";
    std::string err;
    if (!buildGame(tmp.u8string(), err)) { say(err); return; }
    if (!Serve::start(tmp.u8string(), 8765)) { say("Couldn't start the test server on port 8765"); return; }
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", L"http://localhost:8765/", nullptr, nullptr, SW_SHOWNORMAL);
#endif
    say("Playing in the browser: allow the webcam and hold up the printed marker (File > Build Game to keep it)");
}
static void buildDialog(bool run) {
    std::string out = lastBuild;
    if (out.empty() || !run) {
        const std::string base = folder.empty() ? (fs::u8path(documents()) / "Diorama" / "Builds").u8string() : (fs::u8path(folder) / "Build").u8string();
        std::error_code ec; fs::create_directories(fs::u8path(base), ec);
        if (!run || lastBuild.empty()) {
            const std::string pick = pickFolder();
            if (pick.empty()) return;
            out = (fs::u8path(pick) / fs::u8path(slugOf(proj.title))).u8string();
        }
    }
    std::string err;
    if (!buildGame(out, err)) { say(err); return; }
    if (!run) { say("Built: " + out + " (a static website: upload the folder to any https host)"); openInExplorer(out); return; }
    const int port = 8765;
    if (!Serve::start(out, port)) { say("Couldn't start the test server on port 8765"); return; }
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", (L"http://localhost:" + std::to_wstring(port) + L"/").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    say("Running on http://localhost:" + std::to_string(port) + "/ : allow the webcam, hold up the printed marker");
}

// ---------- the menus ----------
static void gameObjectItems() {
    if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) create(EMPTY);
    if (ImGui::BeginMenu("3D Object")) {
        for (int k : {PLANE, CUBE, SPHERE, CAPSULE, CYLINDER, QUAD}) if (ImGui::MenuItem(KIND_NAMES[k])) create(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("AR")) {
        if (ImGui::MenuItem("Picture...")) create(PICTURE);
        if (ImGui::MenuItem("Model...")) create(MODEL);
        if (ImGui::MenuItem("Sprites...")) create(SPRITES);
        if (ImGui::MenuItem("Light rays")) create(RAYS);
        if (ImGui::MenuItem("Words")) create(WORDS);
        ImGui::Separator();
        if (ImGui::MenuItem("AR Marker")) addMarker();
        ImGui::EndMenu();
    }
}
static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New")) newFromTemplate(0);
        if (ImGui::BeginMenu("New from template")) {
            if (ImGui::MenuItem("Art gallery")) newFromTemplate(1);
            if (ImGui::MenuItem("A cube on Hiro")) newFromTemplate(0);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) { const std::string f = openDialog(L"Diorama project\0*.diorama\0\0"); if (!f.empty()) openProject(f); }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject(false);
        if (ImGui::MenuItem("Save As...")) saveProject(true);
        if (!folder.empty() && ImGui::MenuItem("Show the project folder")) openInExplorer(folder);
        ImGui::Separator();
        if (ImGui::MenuItem("Play in the browser", "Ctrl+P")) playInBrowser();
        if (ImGui::MenuItem("Build Game...", "Ctrl+B")) buildDialog(false);
        if (ImGui::MenuItem("Build And Run", "Ctrl+Shift+B")) buildDialog(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        Marker* m = curMarker();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m && selObject >= 0)) selObject = duplicate(proj, *m, selObject);
        if (ImGui::MenuItem("Delete", "Del", false, m != nullptr)) {
            if (selObject >= 0) { remove(*m, selObject); selObject = -1; }
            else { proj.markers.erase(proj.markers.begin() + selMarker); selMarker = std::max(0, selMarker - 1); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) { gameObjectItems(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Help")) { if (ImGui::MenuItem("About Diorama")) showAbout = true; ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}

// ---------- the Hierarchy ----------
static void objectNode(Scene::Marker& m, int mi, int index, int& moveId, int& moveTo) {
    Object& o = m.objects[index];
    const auto kids = childrenOf(m, o.id);
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
    if (selMarker == mi && selObject == o.id) fl |= ImGuiTreeNodeFlags_Selected;
    ImGui::SetNextItemOpen(o.expanded);
    if (!o.active) ImGui::PushStyleColor(ImGuiCol_Text, col4(C_DIM));
    const bool open = ImGui::TreeNodeEx((void*)(intptr_t)o.id, fl, "%s", o.name.c_str());
    if (!o.active) ImGui::PopStyleColor();
    o.expanded = open;
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selMarker = mi; selObject = o.id; }
    if (ImGui::BeginDragDropSource()) { const int payload[2] = {mi, o.id}; ImGui::SetDragDropPayload("OBJ", payload, sizeof payload); ImGui::Text("%s", o.name.c_str()); ImGui::EndDragDropSource(); }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("OBJ")) { const int* d = (const int*)pl->Data; if (d[0] == mi && d[1] != o.id) { moveId = d[1]; moveTo = o.id; } }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        selMarker = mi; selObject = o.id;
        gameObjectItems();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate")) selObject = duplicate(proj, m, o.id);
        if (ImGui::MenuItem("Delete")) { moveId = o.id; moveTo = -99; }
        ImGui::EndPopup();
    }
    if (open) {
        for (int k : kids) objectNode(m, mi, k, moveId, moveTo);
        ImGui::TreePop();
    }
}
static void hierarchy() {
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Hierarchy"); ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("create");
    if (ImGui::BeginPopup("create")) { gameObjectItems(); ImGui::EndPopup(); }
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    for (int mi = 0; mi < (int)proj.markers.size(); ++mi) {
        Scene::Marker& m = proj.markers[mi];
        ImGui::PushID(m.id);
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (selMarker == mi && selObject < 0) fl |= ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(m.expanded);
        ImGui::PushStyleColor(ImGuiCol_Text, col4(IM_COL32(0x34, 0xa9, 0xe3, 255)));
        const bool open = ImGui::TreeNodeEx("##m", fl, "\xe2\x96\xa3 %s", m.name.c_str());   // a square: the marker
        ImGui::PopStyleColor();
        m.expanded = open;
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selMarker = mi; selObject = -1; }
        int moveId = -1, moveTo = -1;
        if (ImGui::BeginDragDropTarget()) {   // dropped on the marker: back to the top level
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("OBJ")) { const int* d = (const int*)pl->Data; if (d[0] == mi) { moveId = d[1]; moveTo = -1; } }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) { selMarker = mi; selObject = -1; gameObjectItems(); ImGui::EndPopup(); }
        if (open) {
            for (int k : childrenOf(m, -1)) objectNode(m, mi, k, moveId, moveTo);
            ImGui::TreePop();
        }
        if (moveId >= 0) {
            if (moveTo == -99) { remove(m, moveId); if (selObject == moveId) selObject = -1; }
            else reparent(m, moveId, moveTo);
        }
        ImGui::PopID();
    }
    if (proj.markers.empty()) ImGui::TextDisabled("No markers: GameObject > AR > AR Marker.");
    ImGui::EndChild();
}

// ---------- the Inspector ----------
static void tip(const char* s) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(360); ImGui::TextUnformatted(s); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); } }
static bool section(const char* name) {
    ImGui::Spacing();
    ImGui::PushFont(boldFont);
    const bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    return open;
}
// Unity's Transform row: a label, then X Y Z fields with their coloured letters; drag them or type
static void vec3Row(const char* label, float* v, float speed) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
    const float labelW = 70, w = (ImGui::GetContentRegionAvail().x - labelW - 3 * 18) / 3;
    const ImU32 cols[3] = {IM_COL32(0xd0, 0x4a, 0x4a, 255), IM_COL32(0x5a, 0xb0, 0x4a, 255), IM_COL32(0x3a, 0x7a, 0xd0, 255)};
    const char* names[3] = {"X", "Y", "Z"};
    for (int k = 0; k < 3; ++k) {
        ImGui::SameLine(k == 0 ? labelW : 0, k == 0 ? -1 : 4);
        ImGui::PushStyleColor(ImGuiCol_Text, col4(cols[k])); ImGui::TextUnformatted(names[k]); ImGui::PopStyleColor();
        ImGui::SameLine(0, 3); ImGui::SetNextItemWidth(w);
        ImGui::PushID(k);
        ImGui::DragFloat("##v", &v[k], speed, 0, 0, "%.3f");
        // up/down drags too (up: more), as well as ImGui's sideways drag
        if (ImGui::IsItemActive() && !ImGui::GetIO().WantTextInput && ImGui::GetIO().MouseDelta.y != 0) v[k] -= ImGui::GetIO().MouseDelta.y * speed;
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImGui::PopID();
    }
    ImGui::PopID();
}
static bool fileRow(const char* label, std::string& file, const wchar_t* filter) {
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(90);
    const std::string shown = file.empty() ? "(none)" : fs::u8path(file).filename().u8string();
    ImGui::PushID(label);
    bool changed = false;
    if (ImGui::Button(shown.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 30, 0))) {
        const std::string f = openDialog(filter);
        if (!f.empty()) { file = bring(f); changed = true; }
    }
    ImGui::SameLine(); if (ImGui::Button("x", ImVec2(-1, 0))) { file.clear(); changed = true; }
    ImGui::PopID();
    return changed;
}
static void labelled(const char* l) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(90); ImGui::SetNextItemWidth(-1); }

static void markerInspector(Scene::Marker& m) {
    char name[96]; snprintf(name, sizeof name, "%s", m.name.c_str());
    ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##name", name, sizeof name)) m.name = name;
    if (section("AR Marker")) {
        labelled("Type");
        const char* types[] = {"Pattern (like Hiro)", "Barcode (3x3)"};
        ImGui::Combo("##type", &m.type, types, 2);
        if (m.type == MK_PATTERN) {
            labelled("Pattern");
            const char* cur = m.preset == "hiro" ? "Hiro" : m.preset == "kanji" ? "Kanji" : !m.letter.empty() ? "A letter or word" : "A picture";
            if (ImGui::BeginCombo("##pat", cur)) {
                if (ImGui::Selectable("Hiro", m.preset == "hiro")) { m.preset = "hiro"; m.letter.clear(); }
                if (ImGui::Selectable("Kanji", m.preset == "kanji")) { m.preset = "kanji"; m.letter.clear(); }
                if (ImGui::Selectable("A letter or word", m.preset.empty() && !m.letter.empty())) { m.preset.clear(); if (m.letter.empty()) m.letter = "A"; }
                if (ImGui::Selectable("A picture...", m.preset.empty() && m.letter.empty())) {
                    const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg\0\0");
                    if (!f.empty()) { m.picture = bring(f); m.preset.clear(); m.letter.clear(); }
                }
                ImGui::EndCombo();
            }
            if (m.preset.empty() && !m.letter.empty()) {
                char l[16]; snprintf(l, sizeof l, "%s", m.letter.c_str());
                labelled("Letters"); if (ImGui::InputText("##letter", l, sizeof l) && l[0]) m.letter = l;
            }
            ImGui::TextDisabled("Pattern markers are made from the picture: no webcam needed.");
        } else {
            labelled("Number"); ImGui::SliderInt("##code", &m.barcode, 0, 63);
            ImGui::TextDisabled("64 barcodes (0..63) can be on the table at once.");
        }
        if (ImGui::Button("Save the printable marker...", ImVec2(-1, 0))) {
            const std::string f = saveDialog(L"PNG picture\0*.png\0\0", L"png", m.name + ".png");
            if (!f.empty()) say(Markers::savePng(f, printable(m, 1024)) ? "Saved " + fs::u8path(f).filename().u8string() + ": print it at 100%" : "Couldn't save it");
        }
    }
    if (section("Lighting")) {
        labelled("Ambient"); ImGui::ColorEdit3("##amb", m.ambient, ImGuiColorEditFlags_NoInputs); tip("The light from everywhere: the colour of the shadows");
        labelled("Sun"); ImGui::ColorEdit3("##sun", m.sun, ImGuiColorEditFlags_NoInputs);
        labelled("Strength"); ImGui::SliderFloat("##str", &m.sunStrength, 0, 2, "%.2f");
        labelled("Direction"); ImGui::SliderFloat("##yaw", &m.sunYaw, -180, 180, "%.0f\xc2\xb0 round");
        labelled("Height"); ImGui::SliderFloat("##pitch", &m.sunPitch, 5, 90, "%.0f\xc2\xb0 up");
    }
    if (section("Window into the marker")) {
        ImGui::Checkbox("Show what's under the paper only through an opening", &m.portal);
        tip("For scenes that go down into the marker, like the art gallery: the opening is a hole in the table.");
        if (m.portal) { labelled("Opening"); ImGui::DragFloat2("##ps", m.portalSize, 0.01f, 0.1f, 10, "%.2f"); }
    }
}

// a component's header, Unity's way: open/close, its tick (when it has one), its name, and a menu (Reset, Remove)
enum { C_KEEP, C_RESET, C_REMOVE };
static int componentHeader(const char* name, bool* on, bool removable) {
    ImGui::Spacing();
    ImGui::PushID(name);
    ImGui::PushFont(boldFont);
    const bool open = ImGui::CollapsingHeader("##hdr", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 after = ImGui::GetCursorScreenPos();
    float x = a.x + 26;
    if (on) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImGui::Checkbox("##on", on);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("On / off");
        x += 24;
    }
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, a.y + (b.y - a.y - ImGui::GetFontSize()) / 2), on && !*on ? C_DIM : C_TEXT, name);
    ImGui::PopFont();
    int act = C_KEEP;
    ImGui::SetCursorScreenPos(ImVec2(b.x - 30, a.y + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("menu");
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("menu")) {
        if (ImGui::MenuItem("Reset")) act = C_RESET;
        if (ImGui::MenuItem("Remove Component", nullptr, false, removable)) act = C_REMOVE;
        if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Every object has a Transform");
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(after);
    ImGui::PopID();
    return open ? act | 0x100 : act;   // 0x100: open
}

// a look for the object: a mesh, a picture (asks which), a model (asks which), sprites, rays or words
static bool giveLook(Object& o, int kind) {
    const Object fresh = make(proj, kind, o.parent);
    std::string file;
    if (kind == PICTURE || kind == SPRITES) { file = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg\0\0"); if (file.empty()) return false; file = bring(file); }
    if (kind == MODEL) { file = openDialog(L"3D models (glTF)\0*.glb;*.gltf\0\0"); if (file.empty()) return false; file = bring(file); }
    o.kind = kind; o.lookOn = true; o.file = file;
    memcpy(o.colour, fresh.colour, sizeof o.colour); o.lit = fresh.lit; o.glow = fresh.glow; o.billboard = fresh.billboard;
    if (kind == MODEL) { const auto c = Viewer::clipsOf(o.file); o.clip = c.empty() ? "" : c[0]; }
    return true;
}
static std::string scriptsDir() {
    const fs::path d = folder.empty() ? fs::u8path(documents()) / "Diorama" / "scripts" : fs::u8path(folder) / "assets" / "scripts";
    std::error_code ec; fs::create_directories(d, ec);
    return d.u8string();
}
static void newScript(Object& o) {
    std::string base = o.name; for (auto& c : base) if (!isalnum((unsigned char)c)) c = '_';
    fs::path f = fs::u8path(scriptsDir()) / (base + ".js");
    for (int k = 2; fs::exists(f); ++k) f = fs::u8path(scriptsDir()) / (base + std::to_string(k) + ".js");
    std::ofstream(f, std::ios::binary) <<
        "// " << o.name << ": runs on this object, in Diorama and on the phone.\n"
        "// this.position, this.rotation, this.scale: its Transform (arrays of 3). time(): seconds since it started.\n\n"
        "function start() {\n  // once, when the marker is first seen\n}\n\n"
        "function update(dt) {\n  // every frame; dt: seconds since the last one\n}\n\n"
        "function onTap() {\n  // when it's tapped\n}\n";
    o.scripts.push_back({f.u8string(), true});
}
static void addComponentMenu(Object& o) {
    const bool hasLook = o.kind != EMPTY;
    if (ImGui::BeginMenu("Mesh", !hasLook)) {
        for (int k : {PLANE, CUBE, SPHERE, CAPSULE, CYLINDER, QUAD}) if (ImGui::MenuItem(KIND_NAMES[k])) giveLook(o, k);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Picture...", nullptr, false, !hasLook)) giveLook(o, PICTURE);
    if (ImGui::MenuItem("Model...", nullptr, false, !hasLook)) giveLook(o, MODEL);
    if (ImGui::MenuItem("Sprites...", nullptr, false, !hasLook)) giveLook(o, SPRITES);
    if (ImGui::MenuItem("Light rays", nullptr, false, !hasLook)) giveLook(o, RAYS);
    if (ImGui::MenuItem("Words", nullptr, false, !hasLook)) giveLook(o, WORDS);
    if (hasLook) ImGui::TextDisabled("(it has a look already: remove it to pick another)");
    ImGui::Separator();
    if (ImGui::MenuItem("Animate", nullptr, false, !o.hasAnim)) { o.hasAnim = true; o.animOn = true; if (o.anim == A_STILL) o.anim = A_FLOAT; }
    if (ImGui::BeginMenu("Script")) {
        if (ImGui::MenuItem("New Script")) newScript(o);
        if (ImGui::MenuItem("Existing Script...")) { const std::string f = openDialog(L"JavaScript\0*.js\0\0"); if (!f.empty()) o.scripts.push_back({bring(f), true}); }
        ImGui::EndMenu();
    }
}

static void objectInspector(Scene::Marker& m, Object& o) {
    ImGui::Checkbox("##active", &o.active); tip("Active: off hides it and everything under it");
    ImGui::SameLine();
    char name[96]; snprintf(name, sizeof name, "%s", o.name.c_str());
    ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##name", name, sizeof name)) o.name = name;
    // Transform: every object has one
    int h = componentHeader("Transform", nullptr, false);
    if ((h & 0xff) == C_RESET) for (int k = 0; k < 3; ++k) { o.pos[k] = 0; o.rot[k] = 0; o.scl[k] = 1; }
    if (h & 0x100) { vec3Row("Position", o.pos, 0.005f); vec3Row("Rotation", o.rot, 0.5f); vec3Row("Scale", o.scl, 0.005f); }

    const wchar_t* PICS = L"Pictures\0*.png;*.jpg;*.jpeg\0\0";
    // the look, with its material
    if (o.kind != EMPTY) {
        const std::string title = isPrimitive(o.kind) ? std::string("Mesh: ") + KIND_NAMES[o.kind] : KIND_NAMES[o.kind];
        h = componentHeader(title.c_str(), &o.lookOn, true);
        if ((h & 0xff) == C_REMOVE) { o.kind = EMPTY; o.file.clear(); }
        else {
            if ((h & 0xff) == C_RESET) { const int k = o.kind; const std::string f = o.file; const Object fresh = make(proj, k, o.parent);
                memcpy(o.colour, fresh.colour, sizeof o.colour); o.lit = fresh.lit; o.glow = fresh.glow; o.billboard = fresh.billboard; o.file = f;
                o.count = fresh.count; o.spriteSize = fresh.spriteSize; o.flap = fresh.flap; o.rays = fresh.rays; o.text = fresh.text; o.clipSpeed = 1; o.clipLoop = true; }
            if (h & 0x100) {
                if (o.kind == MODEL) {
                    if (fileRow("File", o.file, L"3D models (glTF)\0*.glb;*.gltf\0\0")) { Viewer::reload(o.file); const auto c = Viewer::clipsOf(o.file); o.clip = c.empty() ? "" : c[0]; }
                    const auto clips = o.file.empty() ? std::vector<std::string>() : Viewer::clipsOf(o.file);
                    labelled("Animation");
                    if (ImGui::BeginCombo("##clip", o.clip.empty() ? "(none)" : o.clip.c_str())) {
                        if (ImGui::Selectable("(none)", o.clip.empty())) o.clip.clear();
                        for (const auto& c : clips) if (ImGui::Selectable(c.c_str(), o.clip == c)) o.clip = c;
                        ImGui::EndCombo();
                    }
                    if (clips.empty() && !o.file.empty()) ImGui::TextDisabled("This model has no animations.");
                    labelled("Speed"); ImGui::SliderFloat("##cs", &o.clipSpeed, 0, 3, "%.2f");
                    ImGui::Checkbox("Loop", &o.clipLoop);
                }
                if (o.kind == PICTURE) { fileRow("File", o.file, PICS); ImGui::TextDisabled("Lies on the marker (Rotation X 90 stands it up)."); }
                if (o.kind == SPRITES) {
                    fileRow("Picture", o.file, PICS);
                    labelled("How many"); ImGui::SliderInt("##cnt", &o.count, 1, 100);
                    labelled("Size"); ImGui::SliderFloat("##ss", &o.spriteSize, 0.01f, 1, "%.2f");
                    labelled("Flapping"); ImGui::SliderFloat("##flap", &o.flap, 0, 3, "%.2f"); tip("0: still pictures; 1: butterflies");
                    labelled("Speed"); ImGui::SliderFloat("##spd", &o.animSpeed, 0, 3, "%.2f");
                    ImGui::TextDisabled("They fly round inside the object's box (its scale).");
                }
                if (o.kind == RAYS) { labelled("How many"); ImGui::SliderInt("##rays", &o.rays, 1, 24); }
                if (o.kind == WORDS) { char t[256]; snprintf(t, sizeof t, "%s", o.text.c_str()); ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##text", t, sizeof t)) o.text = t; }
                // its material
                ImGui::SeparatorText("Material");
                labelled("Colour"); ImGui::ColorEdit4("##col", o.colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                if (isPrimitive(o.kind)) fileRow("Texture", o.file, PICS);
                ImGui::Checkbox("Lit", &o.lit); tip("Shaded by the marker's lights");
                ImGui::SameLine(); ImGui::Checkbox("Glow", &o.glow); tip("Added on as light (god rays, sparks)");
                if (o.kind == QUAD || o.kind == PICTURE || o.kind == WORDS) { ImGui::SameLine(); ImGui::Checkbox("Faces the phone", &o.billboard); }
            }
        }
    }
    // Animate
    if (o.hasAnim) {
        h = componentHeader("Animate", &o.animOn, true);
        if ((h & 0xff) == C_REMOVE) { o.hasAnim = false; o.anim = A_STILL; }
        else {
            if ((h & 0xff) == C_RESET) { o.anim = A_FLOAT; o.animSpeed = 1; o.animAmount = 0.1f; }
            if (h & 0x100) {
                labelled("Does"); ImGui::Combo("##anim", &o.anim, ANIM_NAMES, ANIM_COUNT);
                if (o.anim != A_STILL) {
                    labelled("Speed"); ImGui::SliderFloat("##asp", &o.animSpeed, 0, 3, "%.2f");
                    if (o.anim != A_SPIN) { labelled("Amount"); ImGui::SliderFloat("##aam", &o.animAmount, 0, 1, "%.3f"); }
                }
            }
        }
    }
    // Scripts
    for (size_t si = 0; si < o.scripts.size(); ++si) {
        ImGui::PushID((int)si);
        Script& sc = o.scripts[si];
        const std::string title = "Script: " + fs::u8path(sc.file).filename().u8string();
        h = componentHeader(title.c_str(), &sc.on, true);
        if ((h & 0xff) == C_REMOVE) { o.scripts.erase(o.scripts.begin() + si); ImGui::PopID(); break; }
        if (h & 0x100) {
            fileRow("File", sc.file, L"JavaScript\0*.js\0\0");
            ImGui::TextDisabled("The script editor and running scripts come next.");
        }
        ImGui::PopID();
    }
    // Add Component
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    const float bw = 200;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - bw) / 2 + ImGui::GetCursorPosX());
    if (ImGui::Button("Add Component", ImVec2(bw, 28))) ImGui::OpenPopup("addcomp");
    if (ImGui::BeginPopup("addcomp")) { addComponentMenu(o); ImGui::EndPopup(); }
    (void)m;
}

static void inspector() {
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Inspector"); ImGui::PopFont();
    ImGui::Separator();
    ImGui::BeginChild("##insp");
    ImGui::PushItemWidth(-1);
    Scene::Marker* m = curMarker();
    if (Object* o = curObject()) objectInspector(*m, *o);
    else if (m) markerInspector(*m);
    else ImGui::TextDisabled("Nothing selected.");
    ImGui::PopItemWidth();
    ImGui::EndChild();
}


// ---------- the editing handles (Unity's Q W E R): move arrows, rotate rings, scale boxes ----------
static int tool = 1;                  // 0 hand (just the camera), 1 move, 2 rotate, 3 scale
static int dragAxis = -1;             // the handle being dragged: 0 x, 1 y, 2 z, 3 all (scale's centre)
static float dragStart[3];            // the value when the drag began
static ImVec2 dragMouse;
static bool handles(const ImVec2& origin, const ImVec2& size) {   // true while the handles have the mouse
    Object* o = curObject();
    if (!o || tool == 0) { dragAxis = -1; return false; }
    float world[16], parent[16];
    if (!Viewer::matrices(o->id, world, parent)) return false;
    const float c[3] = {world[12], world[13], world[14]};
    float cx, cy;
    if (!Viewer::toScreen(c, cx, cy)) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const ImVec2 C(origin.x + cx, origin.y + cy);
    // the handle's length: about 90 pixels on screen whatever the zoom
    float eye[3]; Viewer::eyePos(eye);
    const float dist = std::sqrt((eye[0] - c[0]) * (eye[0] - c[0]) + (eye[1] - c[1]) * (eye[1] - c[1]) + (eye[2] - c[2]) * (eye[2] - c[2]));
    const float len = dist * 0.16f;
    // the axes: the parent's (so the numbers in the Inspector move with the handle)
    float ax[3][3];
    for (int k = 0; k < 3; ++k) {
        float l = std::sqrt(parent[k * 4] * parent[k * 4] + parent[k * 4 + 1] * parent[k * 4 + 1] + parent[k * 4 + 2] * parent[k * 4 + 2]);
        if (l < 1e-6f) l = 1;
        for (int r = 0; r < 3; ++r) ax[k][r] = parent[k * 4 + r] / l;
    }
    if (tool == 3)   // scale: the object's own axes
        for (int k = 0; k < 3; ++k) {
            float l = std::sqrt(world[k * 4] * world[k * 4] + world[k * 4 + 1] * world[k * 4 + 1] + world[k * 4 + 2] * world[k * 4 + 2]);
            if (l < 1e-6f) l = 1;
            for (int r = 0; r < 3; ++r) ax[k][r] = world[k * 4 + r] / l;
        }
    const ImU32 cols[3] = {IM_COL32(0xe0, 0x50, 0x50, 255), IM_COL32(0x6c, 0xc8, 0x4c, 255), IM_COL32(0x40, 0x8c, 0xe8, 255)};
    const ImU32 hot = IM_COL32(0xff, 0xe0, 0x40, 255);
    const ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 tips[3]; bool tipOk[3];
    for (int k = 0; k < 3; ++k) {
        const float p[3] = {c[0] + ax[k][0] * len, c[1] + ax[k][1] * len, c[2] + ax[k][2] * len};
        float x, y; tipOk[k] = Viewer::toScreen(p, x, y); tips[k] = ImVec2(origin.x + x, origin.y + y);
    }
    auto distToSeg = [](ImVec2 p, ImVec2 a, ImVec2 b) {
        const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0; t = std::clamp(t, 0.0f, 1.0f);
        const float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y; return std::sqrt(ex * ex + ey * ey);
    };
    // the rotate rings: each axis's circle, as points on screen
    std::vector<ImVec2> ring[3];
    if (tool == 2)
        for (int k = 0; k < 3; ++k) {
            const int u = (k + 1) % 3, v = (k + 2) % 3;
            for (int i = 0; i <= 48; ++i) {
                const float a = i * 6.2831853f / 48;
                const float p[3] = {c[0] + (ax[u][0] * std::cos(a) + ax[v][0] * std::sin(a)) * len, c[1] + (ax[u][1] * std::cos(a) + ax[v][1] * std::sin(a)) * len,
                                    c[2] + (ax[u][2] * std::cos(a) + ax[v][2] * std::sin(a)) * len};
                float x, y; if (Viewer::toScreen(p, x, y)) ring[k].push_back(ImVec2(origin.x + x, origin.y + y));
            }
        }
    // which handle is under the mouse
    int over = -1; float best = 9;
    if (dragAxis < 0) {
        for (int k = 0; k < 3; ++k) {
            float d = 1e9f;
            if (tool == 2) { for (size_t i = 0; i + 1 < ring[k].size(); ++i) d = std::min(d, distToSeg(mouse, ring[k][i], ring[k][i + 1])); }
            else if (tipOk[k]) d = distToSeg(mouse, C, tips[k]);
            if (d < best) { best = d; over = k; }
        }
        if (tool == 3 && std::fabs(mouse.x - C.x) < 8 && std::fabs(mouse.y - C.y) < 8) over = 3;
    }
    const bool inView = mouse.x >= origin.x && mouse.y >= origin.y && mouse.x < origin.x + size.x && mouse.y < origin.y + size.y;
    if (over >= 0 && inView && ImGui::IsMouseClicked(0)) {
        dragAxis = over; dragMouse = mouse;
        const float* src = tool == 1 ? o->pos : tool == 2 ? o->rot : o->scl;
        memcpy(dragStart, src, sizeof dragStart);
        beginEdit();
    }
    if (dragAxis >= 0) {
        if (!ImGui::IsMouseDown(0)) dragAxis = -1;
        else {
            const ImVec2 d(mouse.x - dragMouse.x, mouse.y - dragMouse.y);
            const int k = dragAxis == 3 ? 0 : dragAxis;
            ImVec2 sd(tips[k].x - C.x, tips[k].y - C.y);
            const float sl = std::max(1.0f, std::sqrt(sd.x * sd.x + sd.y * sd.y));
            const float along = (d.x * sd.x + d.y * sd.y) / sl;   // pixels along the handle on screen
            if (tool == 1) {   // move: as far along the axis as the mouse went along the arrow
                const float units = along / sl * len;
                // into the parent's own units (its scale)
                float pl = std::sqrt(parent[k * 4] * parent[k * 4] + parent[k * 4 + 1] * parent[k * 4 + 1] + parent[k * 4 + 2] * parent[k * 4 + 2]);
                if (pl < 1e-6f) pl = 1;
                memcpy(o->pos, dragStart, sizeof dragStart);
                o->pos[k] = dragStart[k] + units / pl;
                if (ImGui::GetIO().KeyCtrl) o->pos[k] = std::round(o->pos[k] / 0.05f) * 0.05f;   // Ctrl snaps (0.05)
            } else if (tool == 2) {   // rotate: sideways and up/down both turn it
                memcpy(o->rot, dragStart, sizeof dragStart);
                o->rot[k] = dragStart[k] + (d.x - d.y) * 0.5f;
                if (ImGui::GetIO().KeyCtrl) o->rot[k] = std::round(o->rot[k] / 15) * 15;   // Ctrl snaps (15 degrees)
            } else {   // scale: the axis, or all three from the centre box
                const float f = std::max(0.01f, 1 + (dragAxis == 3 ? (d.x - d.y) : along) / 100.0f);
                memcpy(o->scl, dragStart, sizeof dragStart);
                if (dragAxis == 3) for (int q = 0; q < 3; ++q) o->scl[q] = dragStart[q] * f; else o->scl[k] = dragStart[k] * f;
            }
        }
    }
    // draw them
    for (int k = 0; k < 3; ++k) {
        const ImU32 col = (k == dragAxis || (dragAxis < 0 && k == over)) ? hot : cols[k];
        if (tool == 2) { if (ring[k].size() > 1) dl->AddPolyline(ring[k].data(), (int)ring[k].size(), col, 0, 2.5f); continue; }
        if (!tipOk[k]) continue;
        dl->AddLine(C, tips[k], col, 2.5f);
        if (tool == 1) {   // an arrowhead
            ImVec2 dir(tips[k].x - C.x, tips[k].y - C.y); const float l = std::max(1.0f, std::sqrt(dir.x * dir.x + dir.y * dir.y)); dir.x /= l; dir.y /= l;
            const ImVec2 nrm(-dir.y, dir.x);
            dl->AddTriangleFilled(ImVec2(tips[k].x + dir.x * 14, tips[k].y + dir.y * 14), ImVec2(tips[k].x + nrm.x * 6, tips[k].y + nrm.y * 6), ImVec2(tips[k].x - nrm.x * 6, tips[k].y - nrm.y * 6), col);
        } else dl->AddRectFilled(ImVec2(tips[k].x - 5, tips[k].y - 5), ImVec2(tips[k].x + 5, tips[k].y + 5), col);
    }
    if (tool == 3) dl->AddRectFilled(ImVec2(C.x - 6, C.y - 6), ImVec2(C.x + 6, C.y + 6), dragAxis == 3 || over == 3 ? hot : IM_COL32(0xd0, 0xd0, 0xd0, 255));
    dl->PopClipRect();
    return dragAxis >= 0 || (over >= 0 && inView);
}

// ---------- the Scene view ----------
static void sceneView() {
    Viewer::Look& lk = Viewer::look();
    ImGui::PushStyleColor(ImGuiCol_Button, col4(C_DEEP));
    if (ImGui::Button("\xe2\x96\xb6 Play", ImVec2(80, 0))) playInBrowser();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play it in the browser with the webcam (Ctrl+P)");
    ImGui::SameLine();
    if (ImGui::Button(lk.animate ? "Pause" : "Animate", ImVec2(80, 0))) lk.animate = !lk.animate;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The animations in this view");
    ImGui::SameLine(0, 14);
    const char* toolNames[4] = {"Hand (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
    const char* toolLabels[4] = {"Q", "W", "E", "R"};
    for (int k = 0; k < 4; ++k) {
        if (k) ImGui::SameLine(0, 2);
        const bool on = tool == k;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (ImGui::Button(toolLabels[k], ImVec2(28, 0))) tool = k;
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toolNames[k]);
    }
    ImGui::SameLine(0, 14); ImGui::Checkbox("Desk", &lk.desk);
    ImGui::SameLine(); ImGui::Checkbox("Axes", &lk.gizmo);
    ImGui::SameLine(); if (ImGui::Button("Front")) Viewer::frameMarker();
    ImGui::SameLine();
    if (proj.markers.size() > 1) {
        ImGui::SetNextItemWidth(180);
        if (ImGui::BeginCombo("##showm", curMarker() ? curMarker()->name.c_str() : "")) {
            for (int i = 0; i < (int)proj.markers.size(); ++i) if (ImGui::Selectable(proj.markers[i].name.c_str(), i == selMarker)) { selMarker = i; selObject = -1; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    ImGui::TextDisabled("Drag to turn round the marker, right-drag to move, wheel to zoom, click to pick");
    refreshMarkerPicture();
    const ImVec2 avail = ImGui::GetContentRegionAvail(), a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("view", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hov = ImGui::IsItemHovered() || ImGui::IsItemActive();
    // the handles first take the mouse when it's on them; else the view turns, and a click picks
    static bool handleHasMouse = false;
    int picked = -1;
    const unsigned tex = Viewer::render(proj, selMarker, selObject, (int)avail.x, (int)avail.y, hov && !handleHasMouse, picked);
    if (picked >= 0) selObject = picked; else if (picked == -2 && !handleHasMouse) selObject = -1;
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + avail.x, a.y + avail.y), ImVec2(0, 1), ImVec2(1, 0));
    handleHasMouse = handles(a, avail);
}

static void about() {
    if (showAbout) { ImGui::OpenPopup("About Diorama"); showAbout = false; }
    ImGui::SetNextWindowSize(ImVec2(Splash::W + 16, 0));
    if (ImGui::BeginPopupModal("About Diorama", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Splash::draw(ImGui::GetWindowDrawList(), at.x, at.y, nullptr, -1);
        ImGui::Dummy(ImVec2(Splash::W, Splash::H));
        if (ImGui::Button("Close", ImVec2(-1, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void keys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (!io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) tool = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) tool = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) tool = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) tool = 3;
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {   // frame the selected object
            float w[16], pa[16];
            if (Object* o = curObject()) { if (Viewer::matrices(o->id, w, pa)) { const float c[3] = {w[12], w[13], w[14]}; Viewer::focusOn(c, std::max({o->scl[0], o->scl[1], o->scl[2], 0.3f})); } }
            else Viewer::frameMarker();
        }
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
    if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) redo();
    Scene::Marker* m = curMarker();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveProject(false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) quit = true;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) { const std::string f = openDialog(L"Diorama project\0*.diorama\0\0"); if (!f.empty()) openProject(f); }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N)) create(EMPTY);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B)) buildDialog(io.KeyShift);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) playInBrowser();
    if (m && selObject >= 0 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) { beginEdit(); selObject = duplicate(proj, *m, selObject); }
    if (m && selObject >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) { beginEdit(); remove(*m, selObject); selObject = -1; }
}

static void frame() {
    frameStart = proj;
    keys();
    menuBar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = ImGui::GetFrameHeight(), status = 26, leftW = 280, rightW = 340;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const float H = vp->Size.y - top - status;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + top)); ImGui::SetNextWindowSize(ImVec2(leftW, H));
    ImGui::Begin("##hier", nullptr, fixed); hierarchy(); ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightW, vp->Pos.y + top)); ImGui::SetNextWindowSize(ImVec2(rightW, H));
    ImGui::Begin("##insp", nullptr, fixed); inspector(); ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + leftW, vp->Pos.y + top)); ImGui::SetNextWindowSize(ImVec2(vp->Size.x - leftW - rightW, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("##scene", nullptr, fixed); sceneView(); ImGui::End();
    ImGui::PopStyleColor();
    // the status line
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - status)); ImGui::SetNextWindowSize(ImVec2(vp->Size.x, status));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::Begin("##status", nullptr, fixed);
    if (!toast.empty() && now() < toastUntil) ImGui::TextUnformatted(toast.c_str());
    else ImGui::TextDisabled("%s  -  %s", proj.title.c_str(), folder.empty() ? "not saved yet" : folder.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    about();
    // a field, checkbox or menu item that just began an edit: undoable (the handles call beginEdit themselves)
    const ImGuiID active = ImGui::GetActiveID();
    static ImGuiID lastActive = 0;
    if (active != 0 && active != lastActive && dragAxis < 0 && ImGui::IsAnyItemActive()) beginEdit();
    lastActive = active;
    if (undoStack.size() > 1 && Scene::toJson(undoStack.back()) == Scene::toJson(proj) && active == 0) undoStack.pop_back();   // an edit that changed nothing
}

int main(int argc, char** argv) {
    std::string capture, tmpl = "gallery", openArg, buildArg; int selectArg = -1, pickX = -1, pickY = -1;
    std::vector<std::string> args;
#ifdef _WIN32
    { int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n); if (wa) { for (int i = 1; i < n; ++i) args.push_back(fs::path(wa[i]).u8string()); LocalFree(wa); } }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#else
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
#endif
    (void)argc; (void)argv;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--capture" && i + 1 < args.size()) capture = args[++i];
        else if (args[i] == "--template" && i + 1 < args.size()) tmpl = args[++i];
        else if (args[i] == "--select" && i + 1 < args.size()) selectArg = atoi(args[++i].c_str());
        else if (args[i] == "--build" && i + 1 < args.size()) buildArg = args[++i];
        else if (args[i] == "--pick" && i + 2 < args.size()) { pickX = atoi(args[i + 1].c_str()); pickY = atoi(args[i + 2].c_str()); i += 2; }
        else openArg = args[i];
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    window = SDL_CreateWindow("Diorama", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 950, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 1100, 650);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    { char* bp = SDL_GetBasePath(); exeDir = bp ? bp : "."; if (bp) SDL_free(bp); }
    SDL_Surface* icon = SDL_LoadBMP((exeDir + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(window, icon);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    loadFont();
    applyStyle();

    SDL_Window* splashWin = nullptr;
    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Diorama", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(window, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    if (splashWin) {
        const Uint32 start = SDL_GetTicks();
        for (;;) {
            SDL_Event e; bool skip = false;
            while (SDL_PollEvent(&e)) { ImGui_ImplSDL2_ProcessEvent(&e); if (e.type == SDL_QUIT) quit = true; if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_KEYDOWN) skip = true; }
            const Uint32 t = SDL_GetTicks() - start;
            if (skip || quit || t >= 1300) break;
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, "Setting the stage...", t / 1300.0f);
            ImGui::Render();
            int sw, sh; SDL_GL_GetDrawableSize(splashWin, &sw, &sh);
            glViewport(0, 0, sw, sh); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(splashWin);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        SDL_GL_MakeCurrent(window, gl);
        ImGui_ImplSDL2_InitForOpenGL(window, gl);
    }
    Viewer::init(exeDir);
    if (!openArg.empty()) openProject(openArg); else newFromTemplate(tmpl == "basic" ? 0 : 1);
    if (!buildArg.empty()) { std::string err; const bool ok = buildGame(buildArg, err); printf("%s\n", ok ? "built" : err.c_str()); return ok ? 0 : 1; }
    if (selectArg >= 0 && curMarker() && selectArg < (int)curMarker()->objects.size()) selObject = curMarker()->objects[selectArg].id;
    if (capture.empty()) { SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window); }

    int frames = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) { const std::string f = e.drop.file; SDL_free(e.drop.file); if (fs::u8path(f).extension() == ".diorama") openProject(f); }
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        frame();
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (pickX >= 0 && frames == 20) Viewer::clickAt(pickX, pickY);
        if (!capture.empty() && ++frames >= 40) {
            if (pickX >= 0) { std::ofstream(fs::u8path(capture + ".txt")) << (curObject() ? curObject()->name : std::string("(nothing)")) << "\n"; }
            std::vector<unsigned char> px((size_t)w * h * 4);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::vector<unsigned char> flip(px.size());
            for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
            SDL_SaveBMP(s, capture.c_str());
            SDL_FreeSurface(s);
            break;
        }
        SDL_GL_SwapWindow(window);
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
