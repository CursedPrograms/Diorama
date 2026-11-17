// Diorama - Atelier's AR engine: scenes on markers, laid out like Unity (Hierarchy, Scene, Inspector), markers made
// without a webcam, exported as jsartoolkit5 web packages that run in a phone's browser.
//
//   Diorama [project folder]
//   Diorama --capture out.bmp [--template gallery|basic] [--select N]   a screenshot from a hidden window, then quit
#include "marker.h"
#include "model.h"
#include "physics.h"
#include "scene.h"
#include "uifonts.h"
#include "splash.h"
#include "serve.h"
#include "viewer.h"
#include "TextEditor.h"
#include "json.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>   // the dock builder (the default layout)
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
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include "stb_image.h"
#include <commdlg.h>
#include <shellapi.h>
#include <psapi.h>   // the Profiler: memory
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
static ImFont* monoFont = nullptr;   // the Script Editor's
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
    if (fileExists("C:/Windows/Fonts/consola.ttf")) monoFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 16.0f);
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
static bool showAbout = false, showSettings = false;
static void rememberProject();
static void sceneToFile(const Project& p, const std::string& name);
static bool sceneFromFile(const std::string& name, std::vector<Scene::Marker>& out, bool bumpIds = true, Project* whole = nullptr);
static std::vector<Scene::Marker> sceneMarkers(const std::string& name);
static void copyCamera(Project& to, const Project& from);
static void attachPlaceCubes();
static void attachPhysicsScripts();
// where projects live: Documents/Diorama, or the home folder's Diorama when Windows guards Documents
// (Controlled Folder Access refuses unknown apps there, without a word)
static bool writable(const fs::path& d) {
    std::error_code ec; fs::create_directories(d, ec);
    const fs::path probe = d / ".diorama-probe";
    { std::ofstream f(probe, std::ios::binary); if (!f) return false; f << "ok"; if (!f.good()) return false; }
    const bool ok = fs::exists(probe, ec); fs::remove(probe, ec); return ok;
}
static std::string projectsHome() {
    static std::string home;
    if (!home.empty()) return home;
    const char* up = getenv("USERPROFILE");
    for (const fs::path& d : {fs::u8path(documents()) / "Diorama", fs::u8path(up ? up : ".") / "Diorama", fs::u8path(exeDir.empty() ? "." : exeDir) / "Projects"})
        if (writable(d)) { home = d.u8string(); return home; }
    home = (fs::u8path(documents()) / "Diorama").u8string();
    return home;
}

static Marker* curMarker() {   // the selected anchor; -2: the Main Camera (its objects ride with the phone)
    if (selMarker == -2) return &proj.camera;
    return selMarker >= 0 && selMarker < (int)proj.markers.size() ? &proj.markers[selMarker] : nullptr;
}
static int ownerOf(int id) {   // which anchor (or -2: the camera) has this object
    for (const auto& o : proj.camera.objects) if (o.id == id) return -2;
    for (int i = 0; i < (int)proj.markers.size(); ++i) for (const auto& o : proj.markers[i].objects) if (o.id == id) return i;
    return selMarker;
}
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
// ---------- Simulate: the physics in the Scene view (Unity's Play mode, for physics): the Rigidbodies fall, bounce and
// push; when it stops everything goes back where it was (edits made while it runs go too, as in Unity)
static Physics::World* simWorld = nullptr;
static Project simBefore;
static size_t simUndo = 0;
static void stopSimulate() {
    if (!simWorld) return;
    Physics::destroy(simWorld); simWorld = nullptr;
    proj = simBefore; frameStart = proj;
    if (undoStack.size() > simUndo) undoStack.resize(simUndo);
    redoStack.clear();
    if (!curObject()) selObject = -1;
}
static void startSimulate() {
    stopSimulate();
    simBefore = proj; simUndo = undoStack.size();
    simWorld = Physics::create(proj);
}
static void undo() { stopSimulate(); if (undoStack.empty()) return; redoStack.push_back(proj); proj = undoStack.back(); undoStack.pop_back(); if (!curObject()) selObject = -1; }
static void redo() { stopSimulate(); if (redoStack.empty()) return; undoStack.push_back(proj); proj = redoStack.back(); redoStack.pop_back(); if (!curObject()) selObject = -1; }

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
    if (proj.tracking == TRACK_ARUCO) return Markers::printableAruco(proj.arucoDict, m.arucoId, size);
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
static const Scene::FacePoint& facePointOf(const Scene::Marker& m) {
    for (int i = 0; i < FACE_POINT_COUNT; ++i) if (FACE_POINTS[i].landmark == m.facePoint) return FACE_POINTS[i];
    return FACE_POINTS[0];
}
static void refreshMarkerPicture() {
    const Scene::Marker* m = curMarker();
    const std::string key = m ? (std::to_string(proj.tracking) + "|" + std::to_string(m->type) + m->preset + "|" + m->picture + "|" + m->letter + "|" +
                                 std::to_string(m->barcode) + "|" + m->target + "|" + std::to_string(m->facePoint) + "|" + std::to_string(m->arucoId) + "|" + std::to_string(proj.arucoDict)) : "";
    if (selMarker == shownMarker && key == shownKey) return;
    shownMarker = selMarker; shownKey = key;
    if (!m) { Viewer::setMarkerPicture({}, 0, 0); Viewer::setAnchorMode(0, nullptr); return; }
    if (proj.tracking == TRACK_FACES) { Viewer::setAnchorMode(2, facePointOf(*m).head); return; }
    if (proj.tracking == TRACK_SURFACES) { Viewer::setAnchorMode(3, nullptr); return; }
    if (proj.tracking == TRACK_IMAGES) {
        Markers::Image im;
        if (m->target.empty() || !Markers::load(m->target, im)) {   // no picture yet: a grey card
            im.w = 512; im.h = 384; im.px.assign((size_t)im.w * im.h * 4, 150);
        }
        Viewer::setAnchorMode(1, nullptr);
        Viewer::setMarkerPicture(im.px, im.w, im.h);
        return;
    }
    Viewer::setAnchorMode(0, nullptr);
    const Markers::Image im = printable(*m, 512);
    Viewer::setMarkerPicture(im.px, im.w, im.h);
}

// ---------- files: kept absolute while editing, relative to the project in the file ----------
static std::string bring(const std::string& file) {   // a file into the project's assets folder (when it has one)
    if (folder.empty() || file.empty()) return file;
    std::error_code ec;
    const fs::path src = fs::u8path(file), assets = fs::u8path(folder) / "assets";
    { const std::string a = assets.lexically_normal().u8string(), f = src.lexically_normal().u8string();
      if (f.size() > a.size() && f.compare(0, a.size(), a) == 0) return file; }
    fs::create_directories(assets, ec);
    fs::path dst = assets / src.filename();
    if (!fs::exists(dst, ec)) fs::copy_file(src, dst, ec);
    return fs::exists(dst, ec) ? dst.u8string() : file;   // couldn't copy it: keep using it where it is
}
static void forEachFile(Project& p, const std::function<void(std::string&)>& f) {
    f(p.icon); f(p.splash);
    for (auto& o : p.camera.objects) { f(o.file); f(o.audioFile); f(o.particleFile); f(o.videoFile); f(o.uiFile); f(o.uiFontFile); f(o.canvasCss); for (auto& sc : o.scripts) f(sc.file); }
    for (auto& m : p.markers) { f(m.picture); f(m.target); for (auto& o : m.objects) { f(o.file); f(o.audioFile); f(o.particleFile); f(o.videoFile); f(o.uiFile); f(o.uiFontFile); f(o.canvasCss); for (auto& sc : o.scripts) f(sc.file); } }
}
static bool saveProject(bool askFolder) {
    stopSimulate();   // what's saved is the scene as made, not where things fell
    if (folder.empty() || askFolder) {
        const std::string base = (fs::u8path(projectsHome())).u8string();
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
    sceneToFile(proj, proj.scene);
    rememberProject();
    say("Saved " + (fs::u8path(folder) / "project.diorama").u8string());
    return true;
}
static void openProject(const std::string& path) {
    fs::path p = fs::u8path(path);
    if (p.extension() == ".diorama") p = p.parent_path();
    Project np; std::string err;
    if (!Scene::load(np, p.u8string(), err)) { say(err); return; }
    forEachFile(np, [&](std::string& s) { if (!s.empty() && fs::u8path(s).is_relative()) s = (p / fs::u8path(s)).u8string(); });
    stopSimulate();
    proj = np; folder = p.u8string(); selMarker = 0; selObject = -1; shownMarker = -1;
    { std::vector<Scene::Marker> ms; if (sceneFromFile(proj.scene, ms)) proj.markers = ms; }
    rememberProject();
    say("Opened " + proj.title);
}
static std::string lastFile() {   // remembers the project that was open last
    const char* ad = getenv("APPDATA");
    const fs::path d = fs::u8path(ad ? ad : ".") / "Diorama";
    std::error_code ec; fs::create_directories(d, ec);
    return (d / "last.txt").u8string();
}
static bool autosaveFolders = true;   // off for --capture / --build runs
// Edit > Preferences, kept in %APPDATA%\Diorama\preferences.txt ("key value" lines)
static struct Prefs { bool reopenLast = false; } prefs;   // reopenLast: the last project at the start (else a new scene)
static bool showPrefs = false;
static std::string prefsFile() { return (fs::u8path(lastFile()).parent_path() / "preferences.txt").u8string(); }
static void loadPrefs() {
    std::ifstream f(fs::u8path(prefsFile())); std::string k; int v;
    while (f >> k >> v) if (k == "reopenLast") prefs.reopenLast = v != 0;
}
static void savePrefs() { std::ofstream(fs::u8path(prefsFile())) << "reopenLast " << (prefs.reopenLast ? 1 : 0) << "\n"; }
static std::string lastProject() {
    std::string last; { std::ifstream lf(fs::u8path(lastFile())); std::getline(lf, last); }
    return !last.empty() && fs::exists(fs::u8path(last) / "project.diorama") ? last : "";
}
static void rememberProject() { if (!folder.empty() && autosaveFolders) std::ofstream(fs::u8path(lastFile())) << folder; }
// the Plane detection demo's script: a tap on a plane puts a cube there (written beside the projects, then saved into the project)
static std::string projectsHome();
static void attachPlaceCubes() {
    const fs::path d = fs::u8path(projectsHome()) / "scripts";
    std::error_code ec; fs::create_directories(d, ec);
    const fs::path f = d / "PlaceCubes.js";
    std::ofstream(f, std::ios::binary) << R"JS(// PlaceCubes: tap a plane (the dotted surfaces) and a cube lands there.
// planes.raycast(x, y): where a tap meets the planes found ([] when it misses them); spawn("Cube", { worldPosition }) copies the Cube.
var cubes = [];

function start() {
  find("Cube").active = false;            // the cube to copy, hidden itself
  var self = this;
  this.clear = function () {               // the Clear button: find('Spawner').clear()
    cubes.forEach(function (c) { destroy(c); });
    cubes = [];
    self.show();
  };
  this.show = function () {
    var t = find("Canvas/Count");
    if (t) t.getComponent("Text").text = cubes.length ? "Cubes: " + cubes.length : "Tap a plane to place a cube";
  };
  this.show();
}

function update(dt) {
  if (!input.tapped) return;
  var hits = planes.raycast(input.x, input.y);
  if (!hits.length) return;                // not on a plane
  var size = 0.1, p = hits[0].position;
  var c = spawn("Cube", { worldPosition: { x: p.x, y: p.y + size / 2, z: p.z } });
  if (!c) return;
  c.active = true;
  c.rotation.y = Math.random() * 360;
  c.getComponent("Material").colour.set(0.2 + Math.random() * 0.8, 0.2 + Math.random() * 0.8, 0.2 + Math.random() * 0.8);
  cubes.push(c);
  this.show();
}

function onPlanesChanged(e) {
  if (e.added.length) log("planes found:", e.added.map(function (p) { return p.alignment; }).join(", "));
}
)JS";
    for (auto& m : proj.markers) for (auto& o : m.objects) if (o.name == "Spawner") o.scripts.push_back({f.u8string(), true});
}
// the Physics demo's scripts: a tap drops a ball; the zone glows while something's in it
static void attachPhysicsScripts() {
    const fs::path d = fs::u8path(projectsHome()) / "scripts";
    std::error_code ec; fs::create_directories(d, ec);
    const fs::path drop = d / "Drop.js", zone = d / "Zone.js";
    std::ofstream(drop, std::ios::binary) << R"JS(// Drop: a tap anywhere drops a ball from above (a copy of "Ball": its Rigidbody, its Collider and all).
function update(dt) {
  if (!input.tapped) return;
  var x = (Math.random() - 0.5) * 0.6, z = (Math.random() - 0.5) * 0.6;
  var ball = spawn("Ball", { position: { x: x, y: 1.2, z: z } });
  if (ball) ball.getComponent("Rigidbody").velocity = { x: 0, y: -0.5, z: 0 };
}
)JS";
    std::ofstream(zone, std::ios::binary) << R"JS(// Zone: a trigger. It glows orange while something's inside, and says what came and went.
var inside = 0;
function onTriggerEnter(e) {
  inside++;
  gameObject.getComponent("Material").colour.set(1, 0.55, 0.1);
  log("in:", e.gameObject ? e.gameObject.name : "?");
}
function onTriggerExit(e) {
  inside = Math.max(0, inside - 1);
  if (!inside) gameObject.getComponent("Material").colour.set(0.2, 0.9, 0.5);
  log("out:", e.gameObject ? e.gameObject.name : "?");
}
)JS";
    for (auto& m : proj.markers) for (auto& o : m.objects) {
        if (o.name == "Spawner") o.scripts.push_back({drop.u8string(), true});
        if (o.name == "Zone") o.scripts.push_back({zone.u8string(), true});
    }
}
static void newFromTemplate(int which) {
    const std::string dir = (fs::u8path(exeDir) / "templates" / "gallery").u8string() + "/";
    stopSimulate();
    proj = which == 4 ? physicsTemplate() : which == 3 ? planeDemoTemplate() : which == 2 ? navigationTemplate() : which == 1 ? galleryTemplate(dir) : basicTemplate();
    folder.clear(); selMarker = 0; selObject = -1; shownMarker = -1;
    if (which == 3) attachPlaceCubes();
    if (which == 4) attachPhysicsScripts();
    Viewer::frameMarker();
    if (!autosaveFolders) return;
    // its own folder at once (Documents/Diorama/<title>), like a Unity project: the Project window needs one
    std::error_code ec;
    const fs::path base = fs::u8path(projectsHome());
    fs::path f = base / fs::u8path(proj.title);
    for (int k = 2; fs::exists(f, ec); ++k) f = base / fs::u8path(proj.title + " " + std::to_string(k));
    fs::create_directories(f / "assets" / "scripts", ec);
    if (!fs::is_directory(f / "assets", ec)) { say("Couldn't make the project's folder in " + base.u8string()); return; }
    folder = f.u8string();
    saveProject(false);
    rememberProject();
}

// ---------- creating ----------
static void create(int kind) {
    Marker* m = curMarker();
    if (!m) { Scene::Marker k; k.id = newId(proj); k.name = "AR Marker"; proj.markers.push_back(k); selMarker = 0; m = curMarker(); }
    // under the selected object, like Unity's "Create Empty Child" when something's selected
    const int parent = curObject() ? selObject : -1;
    Object o = make(proj, kind, parent);
    if (kind == PICTURE || kind == SPRITES) {
        const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0");
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
static const char* anchorWord() { return proj.tracking == TRACK_IMAGES ? "Picture" : proj.tracking == TRACK_FACES ? "Face" : proj.tracking == TRACK_SURFACES ? "Surface" : proj.tracking == TRACK_ARUCO ? "ArUco" : proj.tracking == TRACK_HANDS ? "Hand" : "Marker"; }
static const char* anchorIcon() {   // what the Hierarchy shows before an anchor's name
    return proj.tracking == TRACK_IMAGES ? "\xe2\x96\xa8" : proj.tracking == TRACK_FACES ? "\xe2\x98\xba" : proj.tracking == TRACK_SURFACES ? "\xe2\x96\xad" : proj.tracking == TRACK_HANDS ? "\xe2\x9c\x8b" : "\xe2\x96\xa3";
}
static void addMarker() {
    Scene::Marker k; k.id = newId(proj);
    int n = (int)proj.markers.size();
    k.name = std::string(anchorWord()) + " " + std::to_string(n + 1);
    k.type = n == 0 ? MK_PATTERN : MK_BARCODE; k.preset = n == 0 ? "hiro" : ""; k.barcode = n; k.arucoId = n;
    if (proj.tracking == TRACK_FACES) { k.facePoint = FACE_POINTS[std::min(n, FACE_POINT_COUNT - 1)].landmark; k.name = FACE_POINTS[std::min(n, FACE_POINT_COUNT - 1)].name; }
    proj.markers.push_back(k);
    selMarker = (int)proj.markers.size() - 1; selObject = -1;
}



static void playInBrowser();
static bool savePrefab(const Scene::Marker& m, int rootId, const std::string& dir);
static bool loadPrefab(const std::string& file, std::vector<Object>& out);
// ---------- the Console: what Play's scripts said (log, errors) ----------
struct ConsoleLine { std::string level, file, text; int line = 0; };
static std::vector<ConsoleLine> consoleLines;
static bool clearOnPlay = true;

// ---------- the Script Editor: the project's JavaScript, coloured, with errors on their lines ----------
struct OpenScript { std::string file; TextEditor ed; bool dirty = false; };
static std::vector<std::unique_ptr<OpenScript>> scripts;
static int scriptTab = -1, gotoLine = -1;
static bool showScripts = false, scriptFocused = false;
static const TextEditor::LanguageDefinition& jsLanguage() {
    static TextEditor::LanguageDefinition L;
    static bool made = false;
    if (made) return L;
    made = true;
    L = TextEditor::LanguageDefinition::CPlusPlus();   // the C-like tokenising, with JavaScript's words
    L.mName = "JavaScript";
    L.mKeywords.clear();
    for (const char* k : {"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends",
                          "finally", "for", "function", "if", "import", "in", "instanceof", "let", "new", "of", "return", "super", "switch", "this",
                          "throw", "try", "typeof", "var", "void", "while", "with", "yield", "async", "await", "true", "false", "null", "undefined"})
        L.mKeywords.insert(k);
    L.mIdentifiers.clear();
    const char* api[][2] = {{"gameObject", "This object"}, {"find", "find(\"Name\" or \"Parent/Child\"): an object"}, {"spawn", "spawn(\"Name\" or a prefab, { position }): a copy, made now"},
                            {"destroy", "destroy(obj): gone"}, {"log", "log(...): to the Console"}, {"time", "time(): seconds since the game started"},
                            {"camera", "camera.position: the phone"}, {"input", "input.tapped, input.x, input.y"}, {"start", "Once, before the first update"},
                            {"update", "update(dt): every frame"}, {"onTap", "The player tapped this object"}, {"onFound", "Its anchor was found"},
                            {"onLost", "Its anchor was lost"}, {"Math", "JavaScript's maths"}};
    for (auto& a : api) { TextEditor::Identifier id; id.mDeclaration = a[1]; L.mIdentifiers.insert({a[0], id}); }
    return L;
}
static TextEditor::Palette atelierPalette() {
    TextEditor::Palette pal = TextEditor::GetDarkPalette();
    pal[(int)TextEditor::PaletteIndex::Background] = IM_COL32(0x14, 0x14, 0x14, 255);
    pal[(int)TextEditor::PaletteIndex::Keyword] = IM_COL32(0x34, 0xa9, 0xe3, 255);
    pal[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(0x6c, 0xc8, 0x9c, 255);
    pal[(int)TextEditor::PaletteIndex::String] = IM_COL32(0xd6, 0xa8, 0x6a, 255);
    pal[(int)TextEditor::PaletteIndex::Comment] = IM_COL32(0x80, 0x80, 0x80, 255);
    pal[(int)TextEditor::PaletteIndex::MultiLineComment] = IM_COL32(0x80, 0x80, 0x80, 255);
    pal[(int)TextEditor::PaletteIndex::Number] = IM_COL32(0xb5, 0xce, 0xa8, 255);
    pal[(int)TextEditor::PaletteIndex::LineNumber] = IM_COL32(0x60, 0x60, 0x60, 255);
    pal[(int)TextEditor::PaletteIndex::Cursor] = IM_COL32(0xe6, 0xe6, 0xe6, 255);
    pal[(int)TextEditor::PaletteIndex::Selection] = IM_COL32(0x00, 0x51, 0x84, 200);
    pal[(int)TextEditor::PaletteIndex::CurrentLineFill] = IM_COL32(0x22, 0x22, 0x22, 255);
    pal[(int)TextEditor::PaletteIndex::CurrentLineFillInactive] = IM_COL32(0x1c, 0x1c, 0x1c, 255);
    pal[(int)TextEditor::PaletteIndex::CurrentLineEdge] = IM_COL32(0x2a, 0x2a, 0x2a, 255);
    return pal;
}
static void openScript(const std::string& file, int line) {
    showScripts = true;
    for (size_t i = 0; i < scripts.size(); ++i) if (scripts[i]->file == file) { scriptTab = (int)i; gotoLine = line; return; }
    auto s = std::make_unique<OpenScript>();
    s->file = file;
    std::ifstream in(fs::u8path(file), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
    s->ed.SetLanguageDefinition(jsLanguage());
    s->ed.SetPalette(atelierPalette());
    s->ed.SetTabSize(2);
    s->ed.SetShowWhitespaces(false);
    s->ed.SetText(ss.str());
    scripts.push_back(std::move(s));
    scriptTab = (int)scripts.size() - 1; gotoLine = line;
}
static void openScript(const std::string& file) { openScript(file, -1); }
static void saveScript(OpenScript& s) {
    std::string t = s.ed.GetText();
    if (!t.empty() && t.back() == '\n') t.pop_back();   // (the editor adds one)
    std::ofstream(fs::u8path(s.file), std::ios::binary) << t << "\n";
    s.dirty = false;
    say("Saved " + fs::u8path(s.file).filename().u8string());
}
static void scriptEditor() {
    scriptFocused = false;
    if (!showScripts || scripts.empty()) return;
    ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(360, 120), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    if (!ImGui::Begin("Script Editor", &showScripts)) { ImGui::End(); ImGui::PopStyleVar(); return; }
    scriptFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ImGui::BeginTabBar("##scripts", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll)) {
        int close = -1;
        for (int i = 0; i < (int)scripts.size(); ++i) {
            OpenScript& sc = *scripts[i];
            bool open = true;
            const std::string label = fs::u8path(sc.file).filename().u8string() + (sc.dirty ? " *" : "") + "###" + sc.file;
            ImGuiTabItemFlags fl = (i == scriptTab && gotoLine != -2) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(label.c_str(), &open, fl)) {
                if (fl) { scriptTab = i; if (gotoLine > 0) sc.ed.SetCursorPosition(TextEditor::Coordinates(gotoLine - 1, 0)); gotoLine = -2; }
                if (ImGui::SmallButton("Save (Ctrl+S)")) saveScript(sc);
                ImGui::SameLine(); if (ImGui::SmallButton("Play (Ctrl+P)")) { saveScript(sc); playInBrowser(); }
                ImGui::SameLine(); ImGui::TextDisabled("%d lines   %s", sc.ed.GetTotalLines(), sc.file.c_str());
                ImGui::PushFont(monoFont ? monoFont : ImGui::GetFont());
                sc.ed.Render("##code");
                ImGui::PopFont();
                if (sc.ed.IsTextChanged()) sc.dirty = true;
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveScript(sc);
                ImGui::EndTabItem();
            }
            if (!open) close = i;
        }
        if (close >= 0) { if (scripts[close]->dirty) saveScript(*scripts[close]); scripts.erase(scripts.begin() + close); if (scriptTab >= (int)scripts.size()) scriptTab = (int)scripts.size() - 1; }
        ImGui::EndTabBar();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
// the errors Play reported, on their lines in the open scripts
static void markErrors() {
    for (auto& sc : scripts) {
        TextEditor::ErrorMarkers mk;
        for (const auto& l : consoleLines)
            if (l.level == "error" && l.line > 0 && (fs::u8path(l.file).filename() == fs::u8path(sc->file).filename())) mk[l.line] = l.text;
        sc->ed.SetErrorMarkers(mk);
    }
}
static void consolePanel() {
    for (const auto& l : Serve::takeLogs()) { consoleLines.push_back({l.level, l.file, l.text, l.line}); markErrors(); }
    if (ImGui::SmallButton("Clear")) { consoleLines.clear(); markErrors(); }
    ImGui::SameLine(); ImGui::Checkbox("Clear on Play", &clearOnPlay);
    int errors = 0; for (const auto& l : consoleLines) if (l.level == "error") ++errors;
    ImGui::SameLine(); ImGui::TextDisabled("%d messages, %d problems   (log() and errors from Play show here)", (int)consoleLines.size(), errors);
    ImGui::BeginChild("##log", ImVec2(0, 0), true);
    for (size_t i = 0; i < consoleLines.size(); ++i) {
        const auto& l = consoleLines[i];
        ImGui::PushID((int)i);
        const bool err = l.level == "error";
        if (err) ImGui::PushStyleColor(ImGuiCol_Text, col4(IM_COL32(0xf0, 0x70, 0x70, 255)));
        const std::string where = l.file.empty() ? "" : fs::u8path(l.file).filename().u8string() + (l.line ? ":" + std::to_string(l.line) : "") + "  ";
        if (ImGui::Selectable((std::string(err ? "! " : "  ") + where + l.text).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0) && !l.file.empty()) {
            // the script it came from (by its name in the project)
            std::error_code ec;
            for (const auto& e : fs::recursive_directory_iterator(fs::u8path(folder) / "assets", ec))
                if (e.path().filename() == fs::u8path(l.file).filename()) { openScript(e.path().u8string(), l.line); break; }
        }
        if (err) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// ---------- the Project window: the project's assets folder ----------
static std::string projectDir;   // the folder shown (inside assets)
static std::string selAsset;     // a file selected in the Project window ("" none): the Inspector shows it
static int selAssetMarker = 0, selAssetObject = -1;   // what was selected then (a new selection clears the file)
static std::string renaming; static char renameBuf[128];
static const char* assetKind(const fs::path& p) {
    std::string e = p.extension().u8string(); for (auto& c : e) c = (char)tolower((unsigned char)c);
    if (e == ".js") return "Script";
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".jfif") return "Picture";
    if (e == ".glb" || e == ".gltf") return "Model";
    if (e == ".prefab") return "Prefab";
    if (e == ".material") return "Material";
    if (e == ".mp3" || e == ".ogg" || e == ".wav") return "Audio";
    if (e == ".mp4" || e == ".webm" || e == ".mov" || e == ".m4v" || e == ".mkv" || e == ".avi" || e == ".gif" || e == ".apng") return "Video";
    if (e == ".patt" || e == ".marker") return "Marker";
    return "File";
}
static const char* assetIcon(const char* kind) {
    return !strcmp(kind, "Script") ? "{ }" : !strcmp(kind, "Picture") ? "\xe2\x96\xa8" : !strcmp(kind, "Model") ? "\xe2\x97\x86" : !strcmp(kind, "Prefab") ? "\xe2\x97\x87"
         : !strcmp(kind, "Material") ? "\xe2\x97\x8f" : !strcmp(kind, "Audio") ? "\xe2\x99\xab" : !strcmp(kind, "Video") ? "\xe2\x96\xb6" : !strcmp(kind, "Marker") ? "\xe2\x96\xa3" : "\xe2\x96\xa1";
}
// ---------- materials: a .material file (JSON) holds a look's material; objects linked to it get its values ----------
static std::string relToProject(const std::string& f) {
    if (f.empty() || folder.empty()) return f;
    std::error_code ec; const fs::path r = fs::relative(fs::u8path(f), fs::u8path(folder), ec);
    return !ec && r.u8string().rfind("..", 0) != 0 ? r.generic_u8string() : f;
}
static std::string fromProject(const std::string& f) {
    if (f.empty() || folder.empty() || !fs::u8path(f).is_relative()) return f;
    return (fs::u8path(folder) / fs::u8path(f)).u8string();
}
static bool saveMaterial(const std::string& file, const Object& o) {
    std::ofstream f(fs::u8path(file), std::ios::binary);
    auto arr = [](const float* v, int n) { std::string s = "["; char b[32]; for (int i = 0; i < n; ++i) { snprintf(b, sizeof b, "%s%g", i ? ", " : "", v[i]); s += b; } return s + "]"; };
    f << "{\n  \"diorama-material\": 1,\n  \"shader\": \"" << (o.lit ? "Standard" : "Unlit") << "\",\n  \"colour\": " << arr(o.colour, 4)
      << ",\n  \"texture\": " << Json::quote(relToProject(o.file)) << ",\n  \"metallic\": " << o.metallic << ", \"roughness\": " << o.roughness
      << ",\n  \"emissive\": " << arr(o.emissive, 3) << ", \"emissiveStrength\": " << o.emissiveStrength
      << ",\n  \"glow\": " << (o.glow ? "true" : "false") << ", \"castShadows\": " << (o.castShadows ? "true" : "false") << ", \"receiveShadows\": " << (o.receiveShadows ? "true" : "false") << "\n}\n";
    return (bool)f;
}
static bool loadMaterial(const std::string& file, Object& o) {
    std::ifstream in(fs::u8path(file), std::ios::binary); if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    Json::Value v; if (!Json::parse(ss.str(), v)) return false;
    auto rd = [&](const char* k, float* out, int n) { const Json::Value* a = v.get(k); if (a && a->type == Json::Value::ARR) for (int i = 0; i < n && i < (int)a->a.size(); ++i) out[i] = (float)a->a[i].n; };
    auto rb = [&](const char* k, bool d) { const Json::Value* b = v.get(k); return b && b->type == Json::Value::BOOL ? b->b : d; };
    o.lit = v.str("shader", "Standard") != "Unlit";
    rd("colour", o.colour, 4); rd("emissive", o.emissive, 3);
    o.file = fromProject(v.str("texture"));
    o.metallic = (float)v.num("metallic", 0); o.roughness = (float)v.num("roughness", 0.6); o.emissiveStrength = (float)v.num("emissiveStrength", 0);
    o.glow = rb("glow", false); o.castShadows = rb("castShadows", true); o.receiveShadows = rb("receiveShadows", true);
    return true;
}
// a material's values onto an object (a picture or model keeps its own file; primitives take the texture)
static void applyMaterial(const Object& m, Object& o) {
    for (int k = 0; k < 4; ++k) o.colour[k] = m.colour[k];
    for (int k = 0; k < 3; ++k) o.emissive[k] = m.emissive[k];
    o.lit = m.lit; o.glow = m.glow; o.metallic = m.metallic; o.roughness = m.roughness; o.emissiveStrength = m.emissiveStrength;
    o.castShadows = m.castShadows; o.receiveShadows = m.receiveShadows;
    if (o.kind != PICTURE && o.kind != MODEL && o.kind != SPRITES) o.file = m.file;
}
static int applyMaterialEverywhere(const std::string& file) {   // every object linked to it
    Object m; if (!loadMaterial(file, m)) return 0;
    int n = 0;
    for (int a = -1; a < (int)proj.markers.size(); ++a) for (auto& o : (a < 0 ? proj.camera : proj.markers[a]).objects) if (o.material == file) { applyMaterial(m, o); ++n; }
    return n;
}
static void importFiles(const std::vector<std::string>& files) {
    if (folder.empty()) return;
    const fs::path into = fs::u8path(projectDir.empty() ? (fs::u8path(folder) / "assets").u8string() : projectDir);
    std::error_code ec; fs::create_directories(into, ec);
    int n = 0;
    for (const auto& f : files) { fs::copy(fs::u8path(f), into / fs::u8path(f).filename(), fs::copy_options::recursive | fs::copy_options::skip_existing, ec); if (!ec) ++n; }
    say("Imported " + std::to_string(n) + " into " + into.filename().u8string());
}
static void projectPanel() {
    if (folder.empty()) { ImGui::TextDisabled("Save the project (File > Save) to give it a folder for its assets."); return; }
    const fs::path assets = fs::u8path(folder) / "assets";
    std::error_code ec; fs::create_directories(assets, ec);
    if (projectDir.empty() || projectDir.find(assets.u8string()) != 0) projectDir = assets.u8string();
    // the path, as buttons back up
    const fs::path cur = fs::u8path(projectDir);
    std::vector<fs::path> crumbs;
    for (fs::path q = cur; ; q = q.parent_path()) { crumbs.insert(crumbs.begin(), q); if (q == assets || q.parent_path() == q) break; }
    for (size_t i = 0; i < crumbs.size(); ++i) {
        if (i) { ImGui::SameLine(0, 2); ImGui::TextDisabled(">"); ImGui::SameLine(0, 2); }
        ImGui::PushID((int)i);
        if (ImGui::SmallButton(i == 0 ? "Assets" : crumbs[i].filename().u8string().c_str())) projectDir = crumbs[i].u8string();
        ImGui::PopID();
    }
    ImGui::SameLine(0, 20); if (ImGui::SmallButton("Import...")) { const std::string f = openDialog(L"Assets\0*.png;*.jpg;*.jpeg;*.glb;*.gltf;*.js;*.mp3;*.ogg;*.wav\0All files\0*.*\0\0"); if (!f.empty()) importFiles({f}); }
    ImGui::SameLine(); if (ImGui::SmallButton("Show in Explorer")) openInExplorer(projectDir);
    ImGui::SameLine(); ImGui::TextDisabled("drag files onto the Hierarchy or an object; an object here makes a prefab; drop files from Explorer to import");
    ImGui::BeginChild("##files", ImVec2(0, 0), true);
    std::vector<fs::directory_entry> items;
    for (const auto& e : fs::directory_iterator(cur, ec)) { const std::string fn = e.path().filename().u8string(); if (fn.size() > 11 && (fn.compare(fn.size() - 11, 11, ".poster.jpg") == 0 || fn.compare(fn.size() - 11, 11, ".poster.png") == 0)) continue; items.push_back(e); }   // video posters are the editor's own
    std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory();
        return a.path().filename().u8string() < b.path().filename().u8string();
    });
    const float cell = 92, avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)(avail / cell));
    int col = 0;
    for (const auto& e : items) {
        const fs::path pth = e.path();
        const bool dir = e.is_directory();
        const char* kind = dir ? "Folder" : assetKind(pth);
        ImGui::PushID(pth.u8string().c_str());
        if (col) ImGui::SameLine();
        ImGui::BeginGroup();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##it", ImVec2(cell - 8, 70));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1)) { selAsset = pth.u8string(); selAssetMarker = selMarker; selAssetObject = selObject; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool picked = selAsset == pth.u8string();
        if (picked) dl->AddRectFilled(a, ImVec2(a.x + cell - 8, a.y + 70), IM_COL32(0x00, 0x5c, 0x8a, 255), 4);
        else if (hov) dl->AddRectFilled(a, ImVec2(a.x + cell - 8, a.y + 70), IM_COL32(0x2e, 0x2e, 0x2e, 255), 4);
        const char* icon = dir ? "\xe2\x96\xb0" : assetIcon(kind);
        ImGui::PushFont(boldFont);
        const ImVec2 is = ImGui::CalcTextSize(icon);
        dl->AddText(boldFont, 30, ImVec2(a.x + (cell - 8 - is.x * 1.8f) / 2, a.y + 6), dir ? IM_COL32(0xd6, 0xb0, 0x5a, 255) : C_BLUE, icon);
        ImGui::PopFont();
        std::string nm = pth.filename().u8string(); if (nm.size() > 13) nm = nm.substr(0, 12) + "\xe2\x80\xa6";
        const ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
        dl->AddText(ImVec2(a.x + (cell - 8 - ts.x) / 2, a.y + 48), C_TEXT, nm.c_str());
        if (hov) ImGui::SetTooltip("%s\n%s", pth.filename().u8string().c_str(), kind);
        if (hov && ImGui::IsMouseDoubleClicked(0)) {
            if (dir) projectDir = pth.u8string();
            else if (!strcmp(kind, "Script")) openScript(pth.u8string());
            else openInExplorer(pth.u8string());
        }
        if (!dir && ImGui::BeginDragDropSource()) {
            const std::string f = pth.u8string();
            ImGui::SetDragDropPayload("ASSET", f.c_str(), f.size() + 1);
            ImGui::Text("%s %s", assetIcon(kind), pth.filename().u8string().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (!dir && !strcmp(kind, "Script") && ImGui::MenuItem("Edit")) openScript(pth.u8string());
            if (ImGui::MenuItem("Show in Explorer")) openInExplorer(pth.parent_path().u8string());
            if (ImGui::MenuItem("Rename")) { renaming = pth.u8string(); snprintf(renameBuf, sizeof renameBuf, "%s", pth.filename().u8string().c_str()); }
            if (ImGui::MenuItem("Delete")) { std::error_code e2; fs::remove_all(pth, e2); say(e2 ? "Couldn't delete it" : "Deleted " + pth.filename().u8string()); if (selAsset == pth.u8string()) selAsset.clear(); }
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::PopID();
        if (++col >= cols) col = 0;
    }
    if (items.empty()) ImGui::TextDisabled("Empty. Import files, drop them here, or right-click to make a script or folder.");
    if (ImGui::BeginPopupContextWindow("##empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Folder")) { fs::path f = cur / "New Folder"; for (int k = 2; fs::exists(f, ec); ++k) f = cur / ("New Folder " + std::to_string(k)); fs::create_directory(f, ec); }
            if (ImGui::MenuItem("Script")) {
                fs::path f = cur / "NewScript.js"; for (int k = 2; fs::exists(f, ec); ++k) f = cur / ("NewScript" + std::to_string(k) + ".js");
                std::ofstream(f, std::ios::binary) << "// A behaviour: drag it onto an object.\n\nfunction start() {\n}\n\nfunction update(dt) {\n}\n";
                openScript(f.u8string());
            }
            if (ImGui::MenuItem("Material")) {   // Unity's: a material asset; drag it onto objects, edit it here and they all change
                fs::path f = cur / "New Material.material"; for (int k = 2; fs::exists(f, ec); ++k) f = cur / ("New Material " + std::to_string(k) + ".material");
                Object def; saveMaterial(f.u8string(), def);
                selAsset = f.u8string(); selAssetMarker = selMarker; selAssetObject = selObject;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Import...")) { const std::string f = openDialog(L"All files\0*.*\0\0"); if (!f.empty()) importFiles({f}); }
        if (ImGui::MenuItem("Show in Explorer")) openInExplorer(cur.u8string());
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget()) {   // an object from the Hierarchy: a prefab of it here
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("OBJ")) {
            const int* d = (const int*)pl->Data;
            if (d[0] >= 0 && d[0] < (int)proj.markers.size()) savePrefab(proj.markers[d[0]], d[1], cur.u8string());
        }
        ImGui::EndDragDropTarget();
    }
    if (!renaming.empty()) { ImGui::OpenPopup("Rename"); }
    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##rn", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("Rename", ImVec2(150, 0))) {
            std::error_code e2; const fs::path from = fs::u8path(renaming), to = from.parent_path() / fs::u8path(renameBuf);
            fs::rename(from, to, e2);
            // the objects using it follow it
            if (!e2) forEachFile(proj, [&](std::string& f) { if (f == from.u8string()) f = to.u8string(); });
            renaming.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(150, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { renaming.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
static int bottomTab = 0;
// the editor's dockable windows (Window menu): open or closed; Reset Layout puts them back as Unity has them
static bool showHierarchy = true, showInspector = true, showSceneWin = true, showProject = true, showConsole = true, resetLayout = false;
static bool showProfiler = false;
static void prefsWindow() {
    if (!showPrefs) return;
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Preferences", &showPrefs, ImGuiWindowFlags_NoDocking)) {
        if (ImGui::Checkbox("Open the last project at the start", &prefs.reopenLast)) savePrefs();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: Diorama starts on a new scene, and the last project is in File > Open recent");
    }
    ImGui::End();
}
// ---------- the Profiler (Window > Profiler): frame time, the Scene view's CPU and GPU time, what's drawn, memory ----------
static void profiler() {
    static float frameMs[240] = {0}, sceneMs[240] = {0}, gpuMs[240] = {0}; static int at = 0;
    const Viewer::Stats& s = Viewer::stats();
    const float dt = ImGui::GetIO().DeltaTime * 1000.0f;
    frameMs[at] = dt; sceneMs[at] = s.cpuMs; gpuMs[at] = std::max(0.0f, s.gpuMs); at = (at + 1) % 240;
    if (!showProfiler) return;
    ImGui::SetNextWindowSize(ImVec2(460, 470), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Profiler", &showProfiler)) { ImGui::End(); return; }
    float avg = 0, worst = 0; int seen = 0; for (float v : frameMs) if (v > 0) { avg += v; worst = std::max(worst, v); ++seen; } avg /= std::max(1, seen);
    ImGui::PushFont(boldFont); ImGui::Text("%.0f FPS", avg > 0 ? 1000.0f / avg : 0.0f); ImGui::PopFont();
    ImGui::SameLine(); ImGui::TextDisabled("frame %.2f ms on average, %.2f ms at worst (the last 4 seconds)", avg, worst);
    const float w = ImGui::GetContentRegionAvail().x;
    auto graph = [&](const char* label, const float* v, ImU32 c, float top) {
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImGui::ColorConvertU32ToFloat4(c));
        char ov[64]; snprintf(ov, sizeof ov, "%s %.2f ms", label, v[(at + 239) % 240]);
        ImGui::PlotLines("##g", v, 240, at, ov, 0, top, ImVec2(w, 70));
        ImGui::PopStyleColor();
    };
    const float top = std::max(16.7f, worst * 1.1f);
    ImGui::PushID("f"); graph("Frame", frameMs, IM_COL32(0xd0, 0xd0, 0xd0, 255), top); ImGui::PopID();
    ImGui::PushID("s"); graph("Scene view, CPU", sceneMs, C_BLUE, top); ImGui::PopID();
    ImGui::PushID("g"); graph(s.gpuMs < 0 ? "Scene view, GPU (no timer queries)" : "Scene view, GPU", gpuMs, IM_COL32(0x5a, 0xb0, 0x4a, 255), top); ImGui::PopID();
    ImGui::TextDisabled("16.7 ms = 60 frames a second. The editor waits for the screen (vsync), so the frame time is at least that.");
    ImGui::Separator();
    if (ImGui::BeginTable("##st", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const std::string& v) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k); ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str()); };
        int total = (int)proj.camera.objects.size(); for (const auto& m : proj.markers) total += (int)m.objects.size();
        row("Objects in the scene", std::to_string(total) + " (" + std::to_string(proj.markers.size()) + " anchor" + (proj.markers.size() == 1 ? "" : "s") + ")");
        row("Placed in the view", std::to_string(s.objects));
        row("Draws", std::to_string(s.draws) + " (" + std::to_string(s.culled) + " outside the view, not drawn)");
        row("Lights", std::to_string(s.lights) + " (of 16) + the sun");
        row("Shadow maps", std::to_string(s.shadowMaps) + (proj.shadows ? "" : " (shadows off: Build Settings > Graphics)"));
        row("Pictures loaded", std::to_string(s.textures));
        row("Models loaded", std::to_string(s.models));
        row("The view", std::to_string(s.width) + " x " + std::to_string(s.height) + " (HDR" + (proj.bloom ? " + bloom" : "") + ")");
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pm; pm.cb = sizeof pm;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm)) row("Diorama's memory", std::to_string(pm.WorkingSetSize / 1048576) + " MB");
#endif
        ImGui::EndTable();
    }
    ImGui::End();
}
static std::string consoleTitle() {
    int errors = 0; for (const auto& l : consoleLines) if (l.level == "error") ++errors;
    return errors ? "Console (" + std::to_string(errors) + ")###Console" : "Console###Console";
}

// ---------- Build: the game as a static website (jsartoolkit5 + three.js), for phones and any web host ----------
static std::string lastBuild;
static bool showScenes = false;   // the Scenes window
// a build runs on its own thread: the editor keeps drawing, shows its progress, and can cancel it
struct BuildJob {
    std::thread th;
    std::atomic<bool> running{false}, cancel{false}, finished{false};
    std::mutex mu;
    std::string status, error, out, platform;
    float progress = 0;
    bool ok = false, run = false;
#ifdef _WIN32
    HANDLE jobObject = nullptr;   // the tools it runs (Gradle): Cancel stops them all
#endif
};
static BuildJob job;
static void jobSay(const std::string& st, float pr) { std::lock_guard<std::mutex> l(job.mu); job.status = st; if (pr >= 0) job.progress = pr; }
static bool jobCancelled(std::string& err) { if (job.cancel) { err = "Build cancelled"; return true; } return false; }
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
static bool buildGame(const std::string& out, std::string& err, bool play = false) {
    std::error_code ec;
    const fs::path o = fs::u8path(out), web = fs::u8path(exeDir) / "assets" / "web";
    fs::create_directories(o / "assets", ec); fs::create_directories(o / "markers", ec);
    const bool mind = proj.tracking == TRACK_IMAGES || proj.tracking == TRACK_FACES;
    if (proj.tracking == TRACK_HANDS) { err = "Hand tracking isn't in yet: pick Markers, Images or Faces in Project Settings"; return false; }
    // only what this game's tracking needs: jsartoolkit5 (markers) or MindAR (pictures, faces)
    std::error_code rm; fs::remove_all(o / "lib", rm);
    const bool surfaces = proj.tracking == TRACK_SURFACES, aruco = proj.tracking == TRACK_ARUCO;
    bool copied = true;
    if (mind) copied = copyTree(web / "mindar", o / "lib" / "mindar");
    else {
        copied = copyTree(web / "three", o / "lib" / "three");
        if (surfaces) {   // the surface tracker for cameras without ARCore (OpenCV.js: loaded only when it's needed)
            std::error_code ce; fs::copy_file(web / "surfacetrack.js", o / "lib" / "surfacetrack.js", fs::copy_options::overwrite_existing, ce); if (ce) copied = false;
            copied = copied && copyTree(web / "opencv", o / "lib" / "opencv");
        }
        if (surfaces || aruco) {   // the camera's calibration page (OpenCV.js), and its board
            for (const char* f : {"calibrate.js", "calibrate-page.js", "calibrate.css"}) { std::error_code ce; fs::copy_file(web / f, o / "lib" / f, fs::copy_options::overwrite_existing, ce); if (ce) copied = false; }
            { std::error_code ce; fs::copy_file(web / "calibrate.html", o / "calibrate.html", fs::copy_options::overwrite_existing, ce); if (ce) copied = false; }
            Markers::savePng((o / "board.png").u8string(), Markers::printableCharuco(200));
        }
        if (aruco) {   // OpenCV.js finds the ArUco markers
            std::error_code ce; fs::copy_file(web / "arucotrack.js", o / "lib" / "arucotrack.js", fs::copy_options::overwrite_existing, ce); if (ce) copied = false;
            copied = copied && copyTree(web / "opencv", o / "lib" / "opencv");
        }
        if (!surfaces && !aruco) {   // ARToolKit (and its camera data)
            for (const char* f : {"artoolkit.min.js", "artoolkit.three.js", "LICENSE-jsartoolkit5.txt"}) { std::error_code ce; fs::copy_file(web / "lib" / f, o / "lib" / f, fs::copy_options::overwrite_existing, ce); if (ce) copied = false; }
            copied = copied && copyTree(web / "data", o / "data");
        }
    }
    if (!copied) {
        err = "Couldn't copy the player into " + out; return false;
    }
    fs::create_directories(o / "lib", ec);
    fs::copy_file(web / "diorama.js", o / "lib" / "diorama.js", fs::copy_options::overwrite_existing, ec);
    {   // physics: physics.js always (it does nothing without colliders); Jolt (3 MB of WASM) only when a scene has colliders
        fs::copy_file(web / "physics.js", o / "lib" / "physics.js", fs::copy_options::overwrite_existing, ec);
        bool phys = false;
        for (const auto& sn : proj.scenes) { Project t; t.markers = sceneMarkers(sn); if (Physics::any(t)) { phys = true; break; } }
        if (phys && !copyTree(web / "jolt", o / "lib" / "jolt")) { err = "Couldn't copy Jolt Physics into " + out; return false; }
    }
    // the files the scene uses, each once, under assets/
    std::map<std::string, std::string> done, scriptSrc;
    std::vector<std::pair<Markers::Image, std::string>> sheet;
    std::vector<std::string> pages;
    for (size_t si = 0; si < proj.scenes.size(); ++si) pages.push_back(si == 0 ? "index.html" : slugOf(proj.scenes[si]) + ".html");
    std::string scenesJs = "window.DIORAMA_SCENES = [";
    for (size_t si = 0; si < proj.scenes.size(); ++si) scenesJs += std::string(si ? ", " : "") + "{\"name\": " + Json::quote(proj.scenes[si]) + ", \"page\": " + Json::quote(pages[si]) + "}";
    scenesJs += "];\n";
    for (size_t si = 0; si < proj.scenes.size(); ++si) {   // ---- each scene, its own page ----
    if (jobCancelled(err)) return false;
    jobSay("Scene " + std::to_string(si) + ": " + proj.scenes[si], 0.1f + 0.5f * si / std::max<size_t>(1, proj.scenes.size()));
    Project b = proj; b.markers = sceneMarkers(proj.scenes[si]);
    if (proj.scenes[si] != proj.scene) { std::vector<Scene::Marker> ms; Project whole; if (sceneFromFile(proj.scenes[si], ms, false, &whole)) copyCamera(b, whole); }
    const std::string sceneSlug = si == 0 ? "" : slugOf(proj.scenes[si]) + "-";
    const std::string jsName = si == 0 ? "scene.js" : "scene-" + slugOf(proj.scenes[si]) + ".js";
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
    std::string splashFile = proj.splash; if (si == 0) take(splashFile);
    for (auto& ob : b.camera.objects) { take(ob.file); take(ob.audioFile); take(ob.particleFile); take(ob.videoFile); take(ob.uiFile); take(ob.uiFontFile); take(ob.canvasCss); for (auto& sc : ob.scripts) { scriptSrc[sc.file.empty() ? "" : sc.file] = sc.file; take(sc.file); } }
    // the markers: their pattern files (made from their pictures), their printable pictures, a sheet of them all
    for (size_t i = 0; i < b.markers.size(); ++i) {
        Scene::Marker& m = b.markers[i];
        const Scene::Marker orig = m;
        const std::string slug = sceneSlug + slugOf(m.name) + "-" + std::to_string(i + 1);
        for (auto& ob : m.objects) { take(ob.file); take(ob.audioFile); take(ob.particleFile); take(ob.videoFile); take(ob.uiFile); take(ob.uiFontFile); take(ob.canvasCss); for (auto& sc : ob.scripts) { scriptSrc[sc.file.empty() ? "" : sc.file] = sc.file; take(sc.file); } }
        if (proj.tracking == TRACK_IMAGES) {   // the picture to look for: in the build, and on the sheet to print
            if (m.target.empty()) { err = m.name + " has no picture to look for (its Inspector)"; return false; }
            Markers::Image im;
            if (Markers::load(m.target, im)) sheet.push_back({im, m.name});
            take(m.target);
            m.picture.clear();
            continue;
        }
        if (proj.tracking == TRACK_FACES || proj.tracking == TRACK_SURFACES) { m.picture.clear(); continue; }
        if (m.type == MK_PATTERN && proj.tracking == TRACK_MARKERS) {
            // every pattern file is written from the very picture that's printed, so the two always agree
            std::ofstream(o / "markers" / (slug + ".patt"), std::ios::binary) << Markers::pattFrom(insidePicture(m));
            m.patt = "markers/" + slug + ".patt";
        }
        m.picture.clear();
        const Markers::Image im = printable(orig, 1024);
        Markers::savePng((o / "markers" / (slug + ".png")).u8string(), im);
        sheet.push_back({im, m.name + (proj.tracking == TRACK_ARUCO ? "  (ArUco " + std::string(Markers::arucoName(proj.arucoDict)) + " #" + std::to_string(m.arucoId) + ")"
                                     : m.type == MK_BARCODE ? "  (barcode " + std::to_string(m.barcode) + ")" : "")});
    }
    if (si + 1 == proj.scenes.size() && !sheet.empty()) Markers::savePng((o / "markers.png").u8string(), Markers::sheet(sheet, fontFile));
    {   // the game's icon (Player Settings), else Diorama's
        Markers::Image ic;
        if (!proj.icon.empty() && Markers::load(proj.icon, ic)) Markers::savePng((o / "icon.png").u8string(), Markers::resized(ic, 512, 512));
        else fs::copy_file(fs::u8path(exeDir) / "assets" / "logo.png", o / "icon.png", fs::copy_options::overwrite_existing, ec);
    }
    {   // the scene as data: scene.json (scene 0), scene-<n>.json (the native player's loadScene)
        const std::string sj = si == 0 ? std::string("scene.json") : "scene-" + std::to_string(si) + ".json";
        std::ofstream f(o / sj, std::ios::binary); f << Scene::toJson(b); if (!f) { err = "Couldn't write " + sj; return false; }
    }
    {   // the scene and its scripts, inside the page (so they need no loading)
        std::ofstream f(o / fs::u8path(jsName), std::ios::binary);
        f << "// the scene and its scripts, inside the page (so they need no loading)\n";
        if (play) f << "window.DIORAMA_PLAY = true;   // played from the editor: logs go to its Console\n";
        f << scenesJs << "window.DIORAMA_SCENE_INDEX = " << si << ";\n";
        if (si == 0)   // Player Settings: the splash screen (the first page only)
            f << "window.DIORAMA_PLAYER = {\"title\": " << Json::quote(proj.title) << ", \"company\": " << Json::quote(proj.company)
              << ", \"splash\": " << Json::quote(splashFile.empty() ? "icon.png" : splashFile) << ", \"intro\": " << Json::quote(proj.intro)
              << ", \"skip\": " << (proj.skipSplash ? "true" : "false") << "};\n";
        f << "window.DIORAMA_SCENE = " << Scene::toJson(b) << ";\nwindow.DIORAMA_SCRIPTS = {";
        bool first = true;
        for (const auto& d : done) {
            if (fs::u8path(d.first).extension() != ".js") continue;
            std::ifstream in(fs::u8path(d.first), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
            f << (first ? "\n  " : ",\n  ") << Json::quote(d.second) << ": " << Json::quote(ss.str());
            first = false;
        }
        f << "\n};\nwindow.DIORAMA_PREFABS = {";
        // the project's prefabs, by name (spawn("Name")), their files in the build too
        bool firstP = true;
        std::error_code pe;
        if (!folder.empty())
            for (const auto& e : fs::recursive_directory_iterator(fs::u8path(folder) / "assets", pe)) {
                if (e.path().extension() != ".prefab") continue;
                std::vector<Object> objs;
                if (!loadPrefab(e.path().u8string(), objs)) continue;
                for (auto& o : objs) { take(o.file); take(o.audioFile); take(o.particleFile); take(o.videoFile); take(o.uiFile); take(o.uiFontFile); take(o.canvasCss); for (auto& sc : o.scripts) take(sc.file); }
                Project tmp; Scene::Marker k; k.objects = objs; tmp.markers.push_back(k);
                {   // and as a file (the native player's spawn("Name"))
                    std::error_code pd; fs::create_directories(o / "prefabs", pd);
                    std::ofstream(o / "prefabs" / fs::u8path(e.path().stem().u8string() + ".prefab"), std::ios::binary) << Scene::toJson(tmp);
                }
                f << (firstP ? "\n  " : ",\n  ") << Json::quote(e.path().stem().u8string()) << ": (" << Scene::toJson(tmp) << ").markers[0].objects";
                firstP = false;
            }
        f << "\n};\n";
        // scripts the prefabs brought in
        f << "(function (s) {";
        for (const auto& d : done) {
            if (fs::u8path(d.first).extension() != ".js") continue;
            std::ifstream in(fs::u8path(d.first), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
            f << " s[" << Json::quote(d.second) << "] = s[" << Json::quote(d.second) << "] || " << Json::quote(ss.str()) << ";";
        }
        f << " })(window.DIORAMA_SCRIPTS);\n";
        if (!f) { err = "Couldn't write " + jsName; return false; }
    }
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
        "  '.jpeg'='image/jpeg'; '.glb'='model/gltf-binary'; '.gltf'='model/gltf+json'; '.wasm'='application/wasm'; '.mp3'='audio/mpeg'; '.ogg'='audio/ogg'; '.wav'='audio/wav'; '.mp4'='video/mp4'; '.webm'='video/webm'; '.m4v'='video/mp4'; '.mov'='video/quicktime' }\r\n"
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
    std::ofstream h(o / fs::u8path(pages[si]), std::ios::binary);
    h << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"UTF-8\">\n"
         "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
         "  <title>" << title << "</title>\n"
         "  <style>\n    html { background: #111; }\n    body { margin: 0; width: 100vw; height: 100vh; color: #e6e6e6; font-family: 'Segoe UI', Arial, sans-serif; overflow: hidden; position: relative; }\n"
         "    canvas { display: block; margin: 0 auto; }\n"
         "    #hint { position: fixed; left: 0; right: 0; bottom: 14px; text-align: center; font-size: 14px; opacity: 0.8; }\n"
         "    #hint a { color: #34a9e3; }\n  </style>\n</head>\n<body>\n"
         "  <div id=\"hint\">" << (proj.tracking == TRACK_FACES ? "Look at the camera." : proj.tracking == TRACK_SURFACES ? "Move the phone slowly over the floor." : proj.tracking == TRACK_ARUCO ? "Point the camera at an ArUco marker." : proj.tracking == TRACK_IMAGES ? "Point the camera at the picture." : "Point the camera at the marker.")
      << (proj.tracking == TRACK_SURFACES ? " <a href=\"calibrate.html\">Calibrate</a>" : proj.tracking == TRACK_ARUCO ? " <a href=\"markers.png\" target=\"_blank\">Print the markers</a> <a href=\"calibrate.html\">Calibrate</a>" : proj.tracking == TRACK_FACES ? "" : proj.tracking == TRACK_IMAGES ? " <a href=\"markers.png\" target=\"_blank\">The pictures</a>" : " <a href=\"markers.png\" target=\"_blank\">Print the markers</a>")
      << "</div>\n";
    const std::string threeBuild = proj.renderer == 1 ? "three.webgpu.min.js" : "three.module.min.js";
    const std::string importMap = "  <script type=\"importmap\">{ \"imports\": { \"three\": \"./lib/three/" + threeBuild + "\", \"three/addons/\": \"./lib/three/addons/\" } }</script>\n";
    const char* gpu = proj.renderer == 1 ? "true" : "false";
    if (aruco)
        h << importMap <<
             "  <script src=\"lib/physics.js\"></script>\n  <script src=\"lib/diorama.js\"></script>\n  <script src=\"" << jsName << "\"></script>\n"
             "  <script type=\"module\">\n"
             "    import * as THREE from 'three';\n"
             "    import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';\n"
             "    Diorama.startAruco({ THREE: THREE, GLTFLoader: GLTFLoader, webgpu: " << gpu << ", dict: " << proj.arucoDict << " });\n"
             "  </script>\n  <!-- Made with Diorama (Cursed Entertainment). three.js: MIT; OpenCV.js: Apache 2.0 (see lib/). -->\n";
    else if (surfaces)
        h << importMap <<
             "  <script src=\"lib/physics.js\"></script>\n  <script src=\"lib/diorama.js\"></script>\n  <script src=\"" << jsName << "\"></script>\n"
             "  <script type=\"module\">\n"
             "    import * as THREE from 'three';\n"
             "    import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';\n"
             "    Diorama.startSurfaces({ THREE: THREE, GLTFLoader: GLTFLoader, webgpu: " << gpu << " });\n"
             "  </script>\n  <!-- Made with Diorama (Cursed Entertainment). three.js: MIT (see lib/three/). WebXR: the phone's own AR (ARCore). -->\n";
    else if (!mind)
        h << importMap <<
             "  <script src=\"lib/physics.js\"></script>\n  <script src=\"lib/diorama.js\"></script>\n  <script src=\"" << jsName << "\"></script>\n"
             "  <script type=\"module\">\n"
             "    import * as THREE from 'three';\n"
             "    import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';\n"
             "    Diorama.startMarkers({ THREE: THREE, GLTFLoader: GLTFLoader, webgpu: " << gpu << " });\n"
             "  </script>\n"
             "  <!-- Made with Diorama (Cursed Entertainment). jsartoolkit5: LGPL-3.0; three.js: MIT (see lib/). -->\n";
    else {
        const bool faces = proj.tracking == TRACK_FACES;
        h << "  <script type=\"importmap\">{ \"imports\": { \"three\": \"./lib/mindar/three/three.module.min.js\", \"three/addons/\": \"./lib/mindar/three/addons/\" } }</script>\n"
             "  <script src=\"lib/physics.js\"></script>\n  <script src=\"lib/diorama.js\"></script>\n  <script src=\"" << jsName << "\"></script>\n"
             "  <script type=\"module\">\n"
             "    import * as THREE from 'three';\n"
             "    import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';\n"
          << (faces ? "    import { MindARThree } from './lib/mindar/mindar-face-three.prod.js';\n"
                      "    Diorama.startMind({ THREE: THREE, GLTFLoader: GLTFLoader, MindARThree: MindARThree, mode: 'face' });\n"
                    : "    import { MindARThree } from './lib/mindar/mindar-image-three.prod.js';\n"
                      "    import { Compiler } from './lib/mindar/mindar-image.prod.js';\n"
                      "    Diorama.startMind({ THREE: THREE, GLTFLoader: GLTFLoader, MindARThree: MindARThree, Compiler: Compiler, mode: 'image' });\n")
          << "  </script>\n  <!-- Made with Diorama (Cursed Entertainment). MindAR: MIT; three.js: MIT (see lib/mindar/). -->\n";
    }
    h << "</body>\n</html>\n";
    }   // ---- each scene ----
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
    if (folder.empty() == false) saveProject(false);
    if (!buildGame(tmp.u8string(), err, true)) { say(err); return; }
    if (clearOnPlay) consoleLines.clear();
    bottomTab = 1;
    if (!Serve::start(tmp.u8string(), 8765)) { say("Couldn't start the test server on port 8765"); return; }
#ifdef _WIN32
    { const auto it = std::find(proj.scenes.begin(), proj.scenes.end(), proj.scene);
      const std::string page = it == proj.scenes.begin() || it == proj.scenes.end() ? "" : slugOf(proj.scene) + ".html";
      ShellExecuteW(nullptr, L"open", (L"http://localhost:8765/" + fs::u8path(page).wstring()).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#endif
    say("Playing in the browser: allow the webcam and hold up the printed marker (File > Build Game to keep it)");
}
static void buildDialog(bool run) {
    std::string out = lastBuild;
    if (out.empty() || !run) {
        const std::string base = folder.empty() ? (fs::u8path(projectsHome()) / "Builds").u8string() : (fs::u8path(folder) / "Build").u8string();
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

// ---------- Build for Windows: <Game>.exe (Diorama's player: WebView2 + a local server) beside the game's folder ----------
#ifdef _WIN32
// the exe's icon, from a picture: one 256 x 256 PNG icon (Windows reads PNG icons since Vista)
static bool setExeIcon(const std::string& exe, const Markers::Image& pic) {
    if (pic.px.empty()) return false;
    const Markers::Image im = Markers::resized(pic, 256, 256);
    std::vector<unsigned char> bytes;
    if (!stbi_write_png_to_func([](void* c, void* d, int n) { auto* v = (std::vector<unsigned char>*)c; v->insert(v->end(), (unsigned char*)d, (unsigned char*)d + n); },
                                &bytes, im.w, im.h, 4, im.px.data(), im.w * 4)) return false;
    unsigned char* png = bytes.data(); const int len = (int)bytes.size();
    // the language its icons were written in (an icon in another language would be a second one)
    WORD lang = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    if (HMODULE mod = LoadLibraryExW(fs::u8path(exe).wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
        EnumResourceLanguagesW(mod, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), [](HMODULE, LPCWSTR, LPCWSTR, WORD l, LONG_PTR p) -> BOOL { *(WORD*)p = l; return FALSE; }, (LONG_PTR)&lang);
        FreeLibrary(mod);
    }
#pragma pack(push, 2)
    struct Group { WORD reserved, type, count; BYTE w, h, colours, res; WORD planes, bits; DWORD bytes; WORD id; } g = {0, 1, 1, 0, 0, 0, 0, 1, 32, (DWORD)len, 1};
#pragma pack(pop)
    HANDLE u = BeginUpdateResourceW(fs::u8path(exe).wstring().c_str(), FALSE);
    bool ok = u && UpdateResourceW(u, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), lang, png, (DWORD)len) && UpdateResourceW(u, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), lang, &g, sizeof g);
    if (u) ok = EndUpdateResourceW(u, !ok) && ok;
    return ok;
}
#endif
static bool buildWindows(const std::string& out, std::string& err) {
#ifdef _WIN32
    const bool native = proj.windowsRuntime == 1;
    const fs::path o = fs::u8path(out), playerDir = fs::u8path(exeDir) / "player", player = playerDir / (native ? "DioramaNative.exe" : "DioramaGame.exe");
    std::error_code ec;
    if (!fs::exists(player, ec)) { err = "The Windows player (player/" + player.filename().u8string() + ") isn't beside Diorama: reinstall Diorama"; return false; }
    if (native && (proj.tracking == TRACK_IMAGES || proj.tracking == TRACK_FACES || proj.tracking == TRACK_HANDS)) {
        err = "The C++ runtime tracks markers, ArUco markers and surfaces: pick JavaScript for this game (Build Settings > Windows)"; return false;
    }
    if (!buildGame((o / "game").u8string(), err)) return false;
    std::string name = proj.title; for (auto& c : name) if (strchr("\\/:*?\"<>|", c)) c = '_';
    if (name.empty()) name = "Game";
    const fs::path exe = o / fs::u8path(name + ".exe");
    fs::copy_file(player, exe, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "Couldn't write " + exe.u8string() + " (is the game running?)"; return false; }
    { Markers::Image icon; if (Markers::load((o / "game" / "icon.png").u8string(), icon)) setExeIcon(exe.u8string(), icon); }
    std::ofstream(o / "game" / "game.json", std::ios::binary) << "{\"title\": " << Json::quote(proj.title) << ", \"made-with\": \"Diorama\"}\n";
    if (native) {   // Diorama's own player: its renderer and the C++ trackers (SDL2, OpenCV beside it)
        for (const char* dll : {"SDL2.dll", "opencv_world4100.dll"}) {
            fs::copy_file(playerDir / dll, o / dll, fs::copy_options::overwrite_existing, ec);
            if (ec) { err = std::string("Couldn't copy ") + dll + " (is the game running?)"; return false; }
        }
        std::ofstream(o / "README.txt", std::ios::binary) <<
            proj.title << ", made with Diorama.\r\n\r\n"
            "Double-click " << name << ".exe to play (keep the game folder and the DLLs beside it). It uses the webcam:\r\n" <<
            (proj.tracking == TRACK_SURFACES ? "point it at a table or the floor and press Space (or click) to lock.\r\n"
                                             : "print game\\markers.png at 100% and hold a marker up to the camera.\r\n") <<
            "F11: full screen. Esc: quit. Runs on Diorama's own renderer (OpenGL 3.3).\r\n";
        return true;
    }
    std::ofstream(o / "README.txt", std::ios::binary) <<
        proj.title << ", made with Diorama.\r\n\r\n"
        "Double-click " << name << ".exe to play (keep the game folder beside it). It uses the webcam: print\r\n"
        "game\\markers.png at 100% and hold a marker up to the camera. F11: full screen.\r\n"
        "It runs on Microsoft's WebView2 (part of Windows 11; for Windows 10: microsoft.com/edge/webview2).\r\n";
    return true;
#else
    (void)out; err = "Windows builds are made on Windows"; return false;
#endif
}
static void buildWindowsDialog(bool run) {
    static std::string lastWin;
    std::string out = lastWin;
    if (out.empty() || !run) {
        const std::string pick = pickFolder();
        if (pick.empty()) return;
        out = (fs::u8path(pick) / fs::u8path(slugOf(proj.title) + "-windows")).u8string();
    }
    std::string err;
    if (!buildWindows(out, err)) { say(err); return; }
    lastWin = out;
    std::string name = proj.title; for (auto& c : name) if (strchr("\\/:*?\"<>|", c)) c = '_';
    if (run) { ShellExecuteW(nullptr, L"open", (fs::u8path(out) / fs::u8path((name.empty() ? "Game" : name) + ".exe")).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); say("Running the Windows build"); }
    else { say("Built for Windows: " + out); openInExplorer(out); }
}


static void tip(const char* s); static void labelled(const char* l); static bool fileRow(const char* label, std::string& file, const wchar_t* filter);
// ---------- Build Settings: platforms (Web, Windows, Linux, Raspberry Pi, Android), each built on the job's thread ----------
struct PlatformInfo { const char* id; const char* name; const char* what; };
static const PlatformInfo PLATFORMS[] = {
    {"web", "Web", "A static website: any https host (GitHub Pages, Netlify, itch.io). Phones open it in their browser."},
    {"windows", "Windows", "<Game>.exe with the game folder beside it. Webcam allowed, works offline, F11 full screen."},
    {"linux", "Linux", "The game with a launcher (<game>.sh): a local server and Chromium (or the default browser)."},
    {"pi", "Raspberry Pi", "The game with a launcher for Raspberry Pi OS: full screen Chromium (kiosk), the Pi camera or a USB webcam; can start with the Pi."},
    {"android", "Android", "An app (.apk): the game inside, the camera asked for once. Install it, or Build And Run onto a phone on USB."},
};
static const PlatformInfo* platformOf(const std::string& id) { for (const auto& p : PLATFORMS) if (id == p.id) return &p; return &PLATFORMS[0]; }
static std::string gameFileName() { std::string n = proj.title; for (auto& c : n) if (strchr("\\/:*?\"<>|", c)) c = '_'; return n.empty() ? "Game" : n; }
static std::string packageIdOf() {
    if (!proj.packageId.empty()) return proj.packageId;
    std::string g; for (char c : proj.title) if (isalnum((unsigned char)c)) g += (char)tolower((unsigned char)c);
    if (g.empty() || isdigit((unsigned char)g[0])) g = "game" + g;
    return "com.cursedentertainment." + g;
}
static std::string buildFolderFor(const std::string& platform) {
    const fs::path base = folder.empty() ? fs::u8path(projectsHome()) / "Builds" / fs::u8path(slugOf(proj.title)) : fs::u8path(folder) / "Builds";
    return (base / fs::u8path(platformOf(platform)->name)).u8string();
}

#ifdef _WIN32
// a tool (Gradle, adb) on the job's thread: its output into a log, stopped (with everything it started) by Cancel
static int runTool(const std::wstring& cmd, const std::wstring& dir, const std::wstring& log, const std::vector<std::pair<std::wstring, std::wstring>>& env) {
    for (const auto& e : env) SetEnvironmentVariableW(e.first.c_str(), e.second.c_str());
    std::wstring line = L"cmd.exe /c \"" + cmd + L" > \"" + log + L"\" 2>&1\"";
    std::vector<wchar_t> buf(line.begin(), line.end()); buf.push_back(0);
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!job.jobObject) {
        job.jobObject = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{}; li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.jobObject, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) return -1;
    AssignProcessToJobObject(job.jobObject, pi.hProcess);
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    DWORD code = 1;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) { GetExitCodeProcess(pi.hProcess, &code); break; }
        if (job.cancel) { TerminateJobObject(job.jobObject, 1); code = (DWORD)-2; break; }
    }
    CloseHandle(pi.hProcess);
    return (int)code;
}
static std::string lastLines(const std::wstring& log, int n) {
    std::ifstream f(log, std::ios::binary); std::vector<std::string> lines; std::string l;
    while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); if (!l.empty()) lines.push_back(l); }
    std::string out; for (size_t i = lines.size() > (size_t)n ? lines.size() - n : 0; i < lines.size(); ++i) out += lines[i] + "\n";
    return out;
}
static std::wstring envOr(const wchar_t* k) { wchar_t b[2048]; const DWORD n = GetEnvironmentVariableW(k, b, 2048); return n && n < 2048 ? b : L""; }
static fs::path androidSdk() {
    for (const std::wstring& c : {envOr(L"ANDROID_HOME"), envOr(L"ANDROID_SDK_ROOT"), envOr(L"LOCALAPPDATA") + L"\\Android\\Sdk"})
        if (!c.empty() && fs::exists(fs::path(c) / "platforms")) return c;
    return {};
}
static fs::path javaHome() {   // Gradle needs Java 17 or newer
    std::vector<fs::path> c;
    if (!envOr(L"JAVA_HOME").empty()) c.push_back(envOr(L"JAVA_HOME"));
    std::error_code ec;
    for (const auto& base : {fs::path(envOr(L"USERPROFILE")) / ".jdks", fs::path(envOr(L"USERPROFILE")) / ".gradle" / "jdks", fs::path(L"C:\\Program Files\\Java"), fs::path(L"C:\\Program Files\\Eclipse Adoptium"), fs::path(L"C:\\Program Files\\Microsoft")})
        for (const auto& e : fs::directory_iterator(base, ec)) c.push_back(e.path());
    for (const auto& base : {fs::path(L"C:\\Program Files\\Android")}) for (const auto& e : fs::directory_iterator(base, ec)) c.push_back(e.path() / "jbr");
    for (const auto& j : c) {
        if (!fs::exists(j / "bin" / "java.exe", ec)) continue;
        const std::string n = j.u8string();   // jdk-1.8 / jdk8 are too old
        if (n.find("1.8") != std::string::npos || n.find("jdk8") != std::string::npos || n.find("jre") != std::string::npos) continue;
        return j;
    }
    return {};
}
#endif

static bool writeText(const fs::path& f, const std::string& text) { std::ofstream o(f, std::ios::binary); o << text; return (bool)o; }
static std::string replaceAll(std::string s, const std::string& a, const std::string& b) { for (size_t i = 0; (i = s.find(a, i)) != std::string::npos; i += b.size()) s.replace(i, a.size(), b); return s; }
static std::string fileText(const fs::path& f) { std::ifstream i(f, std::ios::binary); std::stringstream ss; ss << i.rdbuf(); return ss.str(); }

// Linux / Raspberry Pi: the game, a launcher and a desktop entry
static bool buildLinux(const std::string& out, bool pi, std::string& err) {
    const fs::path o = fs::u8path(out);
    if (!buildGame((o / "game").u8string(), err)) return false;
    const std::string slug = slugOf(proj.title), name = gameFileName();
    const std::string browser = pi
        ? "for b in chromium-browser chromium; do command -v $b >/dev/null && BROWSER=$b && break; done\n"
          "\"$BROWSER\" --kiosk --noerrdialogs --disable-infobars --check-for-update-interval=31536000 --user-data-dir=\"$HOME/.config/" + slug + "\" \\\n"
          "  --autoplay-policy=no-user-gesture-required --use-fake-ui-for-media-stream \"http://127.0.0.1:$PORT/\"\n"
        : "BROWSER=\"\"; for b in chromium chromium-browser google-chrome microsoft-edge brave-browser; do command -v $b >/dev/null && BROWSER=$b && break; done\n"
          "if [ -n \"$BROWSER\" ]; then\n"
          "  \"$BROWSER\" --app=\"http://127.0.0.1:$PORT/\" --user-data-dir=\"$HOME/.config/" + slug + "\" --autoplay-policy=no-user-gesture-required" + (proj.fullscreen ? " --start-fullscreen" : "") + "\n"
          "else\n  xdg-open \"http://127.0.0.1:$PORT/\"; echo \"Playing; press Enter to stop.\"; read _\nfi\n";
    writeText(o / (slug + ".sh"),
        "#!/usr/bin/env bash\n# " + name + ", made with Diorama: serves the game on this computer only and opens it.\n"
        "cd \"$(dirname \"$(readlink -f \"$0\")\")/game\"\n"
        "PORT=$(( 41000 + $(printf '%s' \"" + slug + "\" | cksum | cut -d' ' -f1) % 8000 ))\n"
        "python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1 &\nSERVER=$!\ntrap 'kill $SERVER 2>/dev/null' EXIT\nsleep 1\n" + browser);
    writeText(o / (slug + ".desktop"),
        "[Desktop Entry]\nType=Application\nName=" + name + "\nComment=Made with Diorama\nExec=bash -c 'cd \"$(dirname \"%k\")\" && ./" + slug + ".sh'\n"
        "Icon=" + (o / "game" / "icon.png").generic_u8string() + "\nTerminal=false\nCategories=Game;\n");
    if (pi) writeText(o / "start-with-the-pi.sh",
        "#!/usr/bin/env bash\n# Starts " + name + " when the Pi starts (its desktop). Run it once; delete ~/.config/autostart/" + slug + ".desktop to stop.\n"
        "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"\nmkdir -p ~/.config/autostart\n"
        "printf '[Desktop Entry]\\nType=Application\\nName=" + name + "\\nExec=%s/" + slug + ".sh\\n' \"$HERE\" > ~/.config/autostart/" + slug + ".desktop\n"
        "chmod +x \"$HERE/" + slug + ".sh\"\necho \"" + name + " will start with the Pi.\"\n");
    writeText(o / "README.txt", name + ", made with Diorama.\n\n"
        "Copy this folder to the " + std::string(pi ? "Raspberry Pi" : "Linux computer") + " and run:  bash " + slug + ".sh\n"
        "It needs python3 (to serve the game on the computer itself) and Chromium" + (pi ? " (Raspberry Pi OS has both)" : " or another browser") + ".\n"
        + (pi ? "To start it with the Pi: bash start-with-the-pi.sh\nA USB webcam, or the Pi camera (rpicam), works as the camera.\n" : "") +
        "Print game/markers.png at 100% and hold a marker up to the camera.\n");
    return true;
}

// Android: the player app (assets/player/android) with the game inside, built by Gradle
static bool buildAndroid(const std::string& out, std::string& err, std::string& apkOut) {
#ifdef _WIN32
    const fs::path sdk = androidSdk(), java = javaHome();
    if (sdk.empty()) { err = "Android builds need the Android SDK (Android Studio installs it)"; return false; }
    if (java.empty()) { err = "Android builds need Java 17 or newer (Android Studio has one; or set JAVA_HOME)"; return false; }
    const fs::path o = fs::u8path(out), proj_ = o / "android-project", tmpl = fs::u8path(exeDir) / "assets" / "player" / "android";
    std::error_code ec;
    fs::remove_all(proj_ / "app" / "src" / "main" / "assets", ec);
    if (!copyTree(tmpl, proj_)) { err = "Couldn't copy the Android player template"; return false; }
    jobSay("The game", 0.1f);
    if (!buildGame((proj_ / "app" / "src" / "main" / "assets" / "game").u8string(), err)) return false;
    if (jobCancelled(err)) return false;
    // its name, id, version, turning
    int code = 0; for (char c : proj.version) if (isdigit((unsigned char)c)) code = code * 10 + (c - '0');
    std::string esc; for (char c : proj.title) { if (c == '&') esc += "&amp;"; else if (c == '<') esc += "&lt;"; else if (c == '\'' || c == '"' || c == '\\' || c == '@' || c == '?') { esc += '\\'; esc += c; } else esc += c; }
    const fs::path gk = proj_ / "app" / "build.gradle.kts", mf = proj_ / "app" / "src" / "main" / "AndroidManifest.xml", sx = proj_ / "app" / "src" / "main" / "res" / "values" / "strings.xml";
    writeText(gk, replaceAll(replaceAll(replaceAll(fileText(gk), "{{PACKAGE}}", packageIdOf()), "{{VERSION_CODE}}", std::to_string(std::max(1, code))), "{{VERSION}}", proj.version));
    writeText(mf, replaceAll(fileText(mf), "{{ORIENTATION}}", proj.orientation == 1 ? "portrait" : proj.orientation == 2 ? "landscape" : "fullSensor"));
    writeText(sx, replaceAll(fileText(sx), "{{TITLE}}", esc));
    writeText(proj_ / "local.properties", "sdk.dir=" + replaceAll(sdk.generic_u8string(), ":", "\\:") + "\n");
    {   // the launcher icon
        Markers::Image ic; Markers::load((proj_ / "app" / "src" / "main" / "assets" / "game" / "icon.png").u8string(), ic);
        const std::pair<const char*, int> sizes[] = {{"mdpi", 48}, {"hdpi", 72}, {"xhdpi", 96}, {"xxhdpi", 144}, {"xxxhdpi", 192}};
        for (const auto& sz : sizes) {
            const fs::path d = proj_ / "app" / "src" / "main" / "res" / (std::string("mipmap-") + sz.first);
            fs::create_directories(d, ec);
            if (!ic.px.empty()) Markers::savePng((d / "ic_launcher.png").u8string(), Markers::resized(ic, sz.second, sz.second));
        }
    }
    // Gradle: offline first (quick, nothing downloaded), then online when it's missing something
    jobSay("Gradle: building the app (the first time takes a few minutes)", 0.65f);
    const std::wstring log = (o / "gradle.log").wstring();
    const std::vector<std::pair<std::wstring, std::wstring>> env = {{L"JAVA_HOME", java.wstring()}, {L"ANDROID_HOME", sdk.wstring()}};
    const std::wstring gradlew = L"\"" + (proj_ / "gradlew.bat").wstring() + L"\"";   // by its full path (cmd may not look in the current folder)
    int rc = runTool(gradlew + L" assembleDebug --offline", proj_.wstring(), log, env);
    if (rc != 0 && rc != -2 && !job.cancel) { jobSay("Gradle: fetching what it needs (online)", 0.7f); rc = runTool(gradlew + L" assembleDebug", proj_.wstring(), log, env); }
    if (job.cancel || rc == -2) { err = "Build cancelled"; return false; }
    if (rc != 0) { err = "Gradle couldn't build the app (Builds/Android/gradle.log):\n" + lastLines(log, 6); return false; }
    const fs::path apk = proj_ / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk";
    apkOut = (o / fs::u8path(gameFileName() + ".apk")).u8string();
    fs::copy_file(apk, fs::u8path(apkOut), fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "Couldn't copy the app to " + apkOut; return false; }
    writeText(o / "README.txt", gameFileName() + ", made with Diorama: " + gameFileName() + ".apk (" + packageIdOf() + ", version " + proj.version + ").\n\n"
        "Install it on an Android phone (8.0 or newer): copy the .apk over and open it (allow installing apps from there), or with a USB cable:\n"
        "adb install -r \"" + gameFileName() + ".apk\"\nIt asks for the camera the first time. Print markers.png from the game folder at 100%.\n"
        "This is a debug-signed app for testing and sharing; the Play Store needs a release signing key.\n");
    return true;
#else
    (void)out; (void)apkOut; err = "Android builds are made on Windows for now"; return false;
#endif
}
#ifdef _WIN32
static bool runOnPhone(const std::string& apk, std::string& err) {
    const fs::path adb = androidSdk() / "platform-tools" / "adb.exe";
    if (!fs::exists(adb)) { err = "adb isn't in the Android SDK (platform-tools)"; return false; }
    const std::wstring log = (fs::u8path(apk).parent_path() / "adb.log").wstring();
    jobSay("Installing on the phone (USB debugging on)", 0.92f);
    if (runTool(L"\"" + adb.wstring() + L"\" install -r \"" + fs::u8path(apk).wstring() + L"\"", L"", log, {}) != 0) {
        err = "Couldn't install on a phone: plug one in with USB debugging on.\n" + lastLines(log, 3); return false;
    }
    runTool(L"\"" + adb.wstring() + L"\" shell monkey -p " + fs::u8path(packageIdOf()).wstring() + L" -c android.intent.category.LAUNCHER 1", L"", log, {});
    return true;
}
#endif

// the job: build for a platform (and run it), on its own thread
static void startBuild(const std::string& platform, bool run) {
    if (job.running) return;
    if (job.th.joinable()) job.th.join();
    if (!folder.empty()) saveProject(false);
    job.cancel = false; job.finished = false; job.ok = false; job.run = run; job.platform = platform;
    job.out = buildFolderFor(platform); job.error.clear(); job.progress = 0; job.status = "Starting";
    job.running = true;
    job.th = std::thread([] {
        std::string err, apk; bool ok = false;
        const std::string pf = job.platform, out = job.out;
        std::error_code ec; fs::create_directories(fs::u8path(out), ec);
        if (pf == "web") ok = buildGame(out, err);
        else if (pf == "windows") {
            ok = buildWindows(out, err);
        }
        else if (pf == "linux") ok = buildLinux(out, false, err);
        else if (pf == "pi") ok = buildLinux(out, true, err);
        else if (pf == "android") {
            ok = buildAndroid(out, err, apk);
#ifdef _WIN32
            if (ok && job.run) ok = runOnPhone(apk, err);
#endif
        }
        if (job.cancel) { ok = false; err = "Build cancelled"; }
        { std::lock_guard<std::mutex> l(job.mu); job.ok = ok; job.error = err; job.progress = 1; }
#ifdef _WIN32
        if (job.jobObject) { CloseHandle(job.jobObject); job.jobObject = nullptr; }
#endif
        job.finished = true;
    });
}
// the main thread: its progress window; when it's done, what happened (and Run)
static void buildProgress() {
    if (job.running && !ImGui::IsPopupOpen("Building")) ImGui::OpenPopup("Building");
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (ImGui::BeginPopupModal("Building", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        std::string st; float pr;
        { std::lock_guard<std::mutex> l(job.mu); st = job.status; pr = job.progress; }
        ImGui::Text("%s for %s", job.run ? "Build And Run" : "Build", platformOf(job.platform)->name);
        ImGui::ProgressBar(pr, ImVec2(-1, 0));
        ImGui::TextDisabled("%s", st.c_str());
        ImGui::Spacing();
        ImGui::BeginDisabled(job.cancel.load());
        if (ImGui::Button(job.cancel ? "Cancelling..." : "Cancel", ImVec2(-1, 28))) job.cancel = true;
        ImGui::EndDisabled();
        if (job.finished) {
            ImGui::CloseCurrentPopup();
            if (job.th.joinable()) job.th.join();
            job.running = false;
            const std::string out = job.out;
            if (!job.ok) say(job.error);
            else if (!job.run) { say("Built for " + std::string(platformOf(job.platform)->name) + ": " + out); openInExplorer(out); }
            else if (job.platform == "web") {
                lastBuild = out;
                if (!Serve::start(out, 8765)) say("Couldn't start the test server on port 8765");
                else {
#ifdef _WIN32
                    ShellExecuteW(nullptr, L"open", L"http://localhost:8765/", nullptr, nullptr, SW_SHOWNORMAL);
#endif
                    say("Running on http://localhost:8765/");
                }
            }
#ifdef _WIN32
            else if (job.platform == "windows") { ShellExecuteW(nullptr, L"open", (fs::u8path(out) / fs::u8path(gameFileName() + ".exe")).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); say("Running the Windows build"); }
#endif
            else if (job.platform == "android") say("Running on the phone");
            else { say("Built: copy the folder to the " + std::string(job.platform == "pi" ? "Raspberry Pi" : "Linux computer") + " and run its .sh"); openInExplorer(out); }
        }
        ImGui::EndPopup();
    }
}

static bool showBuild = false;
static std::string shownPlatform;
static void buildSettings() {
    if (!showBuild) return;
    if (shownPlatform.empty()) shownPlatform = proj.platform;
    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(380, 110), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Settings", &showBuild)) { ImGui::End(); return; }
    // the scenes in the build
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Scenes In Build"); ImGui::PopFont();
    ImGui::BeginChild("##bscenes", ImVec2(0, 90), true);
    for (size_t i = 0; i < proj.scenes.size(); ++i) ImGui::Text("%d   %s%s", (int)i, proj.scenes[i].c_str(), i == 0 ? "   (index.html: where it starts)" : "");
    ImGui::EndChild();
    if (ImGui::SmallButton("Edit the scenes...")) showScenes = true;
    ImGui::Spacing();
    // platforms on the left, the chosen one's settings on the right
    ImGui::BeginChild("##plats", ImVec2(210, -44), true);
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Platform"); ImGui::PopFont();
    ImGui::Separator();
    for (const auto& pl : PLATFORMS) {
        const bool active = proj.platform == pl.id;
        std::string label = std::string(pl.name) + (active ? "   (active)" : "");
        if (ImGui::Selectable(label.c_str(), shownPlatform == pl.id, 0, ImVec2(0, 26))) shownPlatform = pl.id;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pset", ImVec2(0, -44), true);
    const PlatformInfo* sp = platformOf(shownPlatform);
    ImGui::PushFont(boldFont); ImGui::TextUnformatted(sp->name); ImGui::PopFont();
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", sp->what); ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushItemWidth(-1);
    auto text = [](const char* label, std::string& v, const char* hint = "") {
        char b[256]; snprintf(b, sizeof b, "%s", v.c_str()); labelled(label);
        if (ImGui::InputTextWithHint((std::string("##") + label).c_str(), hint, b, sizeof b)) v = b;
    };
    ImGui::SeparatorText("Runtime");
    if (shownPlatform == "windows") {
        ImGui::RadioButton("JavaScript (WebView2)", &proj.windowsRuntime, 0); tip("The game as on the web, in Windows' own browser engine: everything works");
        ImGui::RadioButton("C++ (Diorama's native player)", &proj.windowsRuntime, 1);
        tip("Diorama's own renderer and C++ trackers: the PC's power (PBR, many lights, soft shadows, HDR, bloom).\nMarkers, ArUco and Surfaces; scripts run as on the web.");
        ImGui::TextDisabled("C++: Diorama's own renderer with native ARToolKit and the PC's full power\n(more lights, shadows); markers first.");
        ImGui::Checkbox("Start full screen", &proj.fullscreen);
    } else if (shownPlatform == "android") {
        ImGui::TextDisabled("JavaScript (Android's WebView). C++ comes later.");
        text("Package id", proj.packageId, packageIdOf().c_str()); tip("Empty: com.cursedentertainment.<game>");
        const char* ori[] = {"Turns with the phone", "Portrait", "Landscape"};
        labelled("Screen"); ImGui::Combo("##ori", &proj.orientation, ori, 3);
#ifdef _WIN32
        const fs::path sdk = androidSdk(), jv = javaHome();
        ImGui::TextColored(sdk.empty() ? col4(IM_COL32(0xf0, 0xb0, 0x60, 255)) : col4(C_DIM), "Android SDK: %s", sdk.empty() ? "not found (install Android Studio)" : sdk.u8string().c_str());
        ImGui::TextColored(jv.empty() ? col4(IM_COL32(0xf0, 0xb0, 0x60, 255)) : col4(C_DIM), "Java: %s", jv.empty() ? "17 or newer not found" : jv.u8string().c_str());
#endif
    } else if (shownPlatform == "linux" || shownPlatform == "pi") {
        ImGui::TextDisabled("JavaScript (Chromium). The C++ runtime comes to Linux and the Pi after Windows.");
        if (shownPlatform == "linux") ImGui::Checkbox("Start full screen", &proj.fullscreen);
    } else ImGui::TextDisabled("JavaScript: three.js with jsartoolkit5 (markers) or MindAR (pictures, faces).");
    ImGui::SeparatorText("Graphics");
    {
        ImGui::RadioButton("WebGL 2", &proj.renderer, 0); tip("Works in every browser and phone");
        ImGui::SameLine(); ImGui::RadioButton("WebGPU", &proj.renderer, 1);
        tip("Newer and faster where the browser has it (Chrome, Edge, WebView2); WebGL 2 by itself where it hasn't");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", proj.tracking == TRACK_IMAGES || proj.tracking == TRACK_FACES
            ? "Images and Faces draw with MindAR's own WebGL, whichever is picked."
            : proj.tracking == TRACK_SURFACES ? "Surfaces: WebGPU draws the preview; AR on a phone (WebXR) always draws with WebGL."
            : "Markers draw with the one picked (three.js r172).");
        ImGui::PopTextWrapPos();
    }
    {
        ImGui::Checkbox("Shadows", &proj.shadows); ImGui::SameLine(160);
        const char* qs[] = {"1024", "2048", "4096"}; ImGui::SetNextItemWidth(110); ImGui::Combo("##sq", &proj.shadowQuality, qs, 3); tip("The shadow map's size: sharper, slower");
        ImGui::Checkbox("Bloom", &proj.bloom); tip("Bright and glowing things bleed light");
        if (proj.bloom) { ImGui::SameLine(160); ImGui::SetNextItemWidth(160); ImGui::SliderFloat("##bs", &proj.bloomStrength, 0, 2, "strength %.2f");
                          ImGui::SameLine(); ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##bt", &proj.bloomThreshold, 0, 2, "from %.2f"); }
        const char* tms[] = {"None", "PBR Neutral (colours kept)", "ACES (filmic)"};
        labelled("Tone mapping"); ImGui::Combo("##tm", &proj.toneMapping, tms, 3);
        labelled("Exposure"); ImGui::SliderFloat("##exp", &proj.exposure, 0.2f, 3, "%.2f");
        ImGui::TextDisabled("The editor's view shows these too. No bloom: AR on phones (WebXR), MindAR, WebGPU;\nWebGPU has no shadows on the real world yet (three.js r172).");
    }
    ImGui::SeparatorText("Player Settings");
    { char b[128]; snprintf(b, sizeof b, "%s", proj.title.c_str()); labelled("Game name"); if (ImGui::InputText("##gname", b, sizeof b) && b[0]) proj.title = b; }
    text("Company", proj.company);
    text("Version", proj.version);
    fileRow("Icon", proj.icon, L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0");
    ImGui::Checkbox("Skip the splash screen", &proj.skipSplash); tip("Off: the game opens on its splash (picture, name, intro words, Tap to start)");
    if (!proj.skipSplash) {
        fileRow("Splash", proj.splash, L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0"); tip("Empty: the icon");
        char b[1024]; snprintf(b, sizeof b, "%s", proj.intro.c_str());
        ImGui::TextUnformatted("Intro words");
        if (ImGui::InputTextMultiline("##intro", b, sizeof b, ImVec2(-1, 70))) proj.intro = b;
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    // the buttons
    if (proj.platform != shownPlatform) {
        if (ImGui::Button("Switch Platform", ImVec2(170, 32))) { proj.platform = shownPlatform; if (!folder.empty()) saveProject(false); say(std::string("Platform: ") + sp->name); }
        ImGui::SameLine();
    }
    {   // where it goes (the last two folders; the whole path on hover)
        const fs::path bf = fs::u8path(buildFolderFor(shownPlatform));
        ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("...\%s\%s", bf.parent_path().filename().u8string().c_str(), bf.filename().u8string().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bf.u8string().c_str());
        if (ImGui::IsItemClicked()) { std::error_code ec; fs::create_directories(bf, ec); openInExplorer(bf.u8string()); }
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 290);
    if (ImGui::Button("Build", ImVec2(130, 32))) startBuild(shownPlatform, false);
    ImGui::SameLine();
    if (ImGui::Button("Build And Run", ImVec2(140, 32))) startBuild(shownPlatform, true);
    ImGui::End();
}

// ---------- the Marker Maker: markers made here, saved into the project (assets/markers), Hiro and Kanji stay stock ----------
// a saved marker is <name>.marker (what it is), <name>.png (to print) and, for patterns, <name>.patt (what the game looks for)
static bool showMaker = false;
static void tip(const char* s); static void labelled(const char* l); static void giveVideo(Object& o, const std::string& file);
struct MakerState {
    int type = MK_PATTERN;        // MK_PATTERN / MK_BARCODE / 2 ArUco (the project's dictionary)
    int arucoId = 0;
    int source = 2;               // 0 Hiro, 1 Kanji, 2 letters, 3 a picture, 4 random blocks
    char letters[16] = "A";
    std::string picture;          // source 3
    Markers::Image blocks;        // source 4
    int barcode = 0;
    char name[64] = "My marker";
    GLuint tex = 0; std::string shownKey;
} mk;
static fs::path markersDir() { return fs::u8path(folder) / "assets" / "markers"; }
// what a marker in the maker is, as a scene marker (to print, to use)
static Scene::Marker makerMarker() {
    Scene::Marker m; m.type = mk.type == 2 ? MK_PATTERN : mk.type;
    if (mk.type == 2) { m.arucoId = mk.arucoId; return m; }
    if (mk.type == MK_BARCODE) { m.barcode = mk.barcode; return m; }
    m.preset.clear(); m.letter.clear(); m.picture.clear();
    if (mk.source == 0) m.preset = "hiro";
    else if (mk.source == 1) m.preset = "kanji";
    else if (mk.source == 2) m.letter = mk.letters[0] ? mk.letters : "A";
    else if (mk.source == 3) m.picture = mk.picture;
    return m;
}
static Markers::Image makerInside() {
    if (mk.type == MK_PATTERN && mk.source == 4) return mk.blocks;
    return insidePicture(makerMarker());
}
static Markers::Image makerPrintable(int size) {
    if (mk.type == 2) return Markers::printableAruco(proj.arucoDict, mk.arucoId, size);
    if (mk.type == MK_PATTERN && mk.source == 4) return Markers::printablePattern(mk.blocks, size);
    return printable(makerMarker(), size);
}
// random blocks, 5 x 5, that look different each way round (so the game knows which way up it is)
static Markers::Image randomBlocks() {
    static unsigned seed = (unsigned)SDL_GetTicks() * 2654435761u + 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const int N = 5, S = 512, c = S / N;
    bool g[N][N];
    for (int tries = 0; tries < 100; ++tries) {
        int black = 0;
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) { g[y][x] = rnd() % 100 < 45; black += g[y][x]; }
        bool same = false;   // the same as itself turned a quarter or half: try again
        for (int r = 1; r < 4 && !same; ++r) {
            bool eq = true;
            for (int y = 0; y < N && eq; ++y) for (int x = 0; x < N && eq; ++x) {
                int sx = x, sy = y; for (int k = 0; k < r; ++k) { const int t = sx; sx = N - 1 - sy; sy = t; }
                eq = g[y][x] == g[sy][sx];
            }
            same = eq;
        }
        if (!same && black > 6 && black < 19) break;
    }
    Markers::Image im; im.w = im.h = S; im.px.assign((size_t)S * S * 4, 255);
    for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) if (g[std::min(N - 1, y / c)][std::min(N - 1, x / c)]) {
        unsigned char* q = &im.px[((size_t)y * S + x) * 4]; q[0] = q[1] = q[2] = 0;
    }
    return im;
}
// the barcode numbers in use (this project's markers and the saved ones)
static std::vector<bool> barcodesInUse() {
    std::vector<bool> used(64, false);
    for (const auto& m : proj.markers) if (m.type == MK_BARCODE && m.barcode >= 0 && m.barcode < 64) used[m.barcode] = true;
    std::error_code ec;
    if (!folder.empty()) for (const auto& e : fs::directory_iterator(markersDir(), ec)) if (e.path().extension() == ".marker") {
        std::ifstream f(e.path(), std::ios::binary); std::stringstream ss; ss << f.rdbuf();
        Json::Value v; if (Json::parse(ss.str(), v) && v.str("type") == "barcode") { const int b = (int)v.num("barcode", -1); if (b >= 0 && b < 64) used[b] = true; }
    }
    return used;
}
static std::string jsonEsc(const std::string& t) { std::string o; for (char c : t) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
static bool saveMakerMarker(std::string& savedName) {
    if (folder.empty()) { say("Save the project first: markers go into its assets"); return false; }
    std::string name = mk.name; for (auto& c : name) if (strchr("\\/:*?\"<>|", c)) c = '_';
    if (name.empty()) name = "Marker";
    std::error_code ec; fs::create_directories(markersDir(), ec);
    const fs::path base = markersDir() / fs::u8path(name);
    // the picture it was made from goes with it
    std::string picRel;
    if (mk.type == MK_PATTERN && (mk.source == 3 || mk.source == 4)) {
        const fs::path pic = markersDir() / fs::u8path(name + ".inside.png");
        if (!Markers::savePng(pic.u8string(), makerInside())) { say("Couldn't save the marker's picture"); return false; }
        picRel = pic.filename().u8string();
    }
    if (!Markers::savePng((base.u8string() + ".png"), makerPrintable(1024))) { say("Couldn't save " + name + ".png"); return false; }
    if (mk.type == MK_PATTERN) std::ofstream(fs::u8path(base.u8string() + ".patt"), std::ios::binary) << Markers::pattFrom(makerInside());
    const std::string kind = mk.type == 2 ? "aruco" : mk.type == MK_BARCODE ? "barcode" : "pattern";
    std::ofstream f(fs::u8path(base.u8string() + ".marker"), std::ios::binary);
    f << "{\"diorama-marker\":1,\"name\":\"" << jsonEsc(mk.name) << "\",\"type\":\"" << kind << "\",\"arucoId\":" << mk.arucoId << ",\"arucoDict\":" << proj.arucoDict << ""
      << ",\"barcode\":" << mk.barcode
      << ",\"preset\":\"" << (mk.type == MK_PATTERN && mk.source == 0 ? "hiro" : mk.type == MK_PATTERN && mk.source == 1 ? "kanji" : "") << "\""
      << ",\"letter\":\"" << (mk.type == MK_PATTERN && mk.source == 2 ? jsonEsc(mk.letters) : "") << "\""
      << ",\"picture\":\"" << jsonEsc(picRel) << "\"}\n";
    savedName = name;
    return true;
}
// a saved marker onto an anchor
static bool applyMarkerFile(Scene::Marker& m, const std::string& file) {
    std::ifstream f(fs::u8path(file), std::ios::binary); std::stringstream ss; ss << f.rdbuf();
    Json::Value v; if (!Json::parse(ss.str(), v) || !v.get("diorama-marker")) { say("That isn't a Diorama marker"); return false; }
    m.type = v.str("type") == "barcode" ? MK_BARCODE : MK_PATTERN;
    m.arucoId = (int)v.num("arucoId", m.arucoId);
    m.barcode = (int)v.num("barcode", 0);
    m.preset = v.str("preset"); m.letter = v.str("letter");
    const std::string pic = v.str("picture");
    m.picture = pic.empty() ? "" : (fs::u8path(file).parent_path() / fs::u8path(pic)).u8string();
    if (m.preset.empty() && m.letter.empty() && m.picture.empty() && m.type == MK_PATTERN) m.preset = "hiro";
    const std::string nm = v.str("name"); if (!nm.empty()) m.name = nm;
    shownMarker = -1;
    return true;
}
static void showImage(GLuint& tex, const Markers::Image& im) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.px.data());
}
static void markerMaker() {
    if (!showMaker) return;
    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(420, 140), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Marker Maker", &showMaker)) { ImGui::End(); return; }
    if (mk.blocks.px.empty()) mk.blocks = randomBlocks();
    // left: what it is
    ImGui::BeginChild("##mkl", ImVec2(270, 0));
    ImGui::PushItemWidth(-1);
    labelled("Name"); ImGui::InputText("##mkn", mk.name, sizeof mk.name);
    labelled("Type");
    const char* types[] = {"Pattern (like Hiro)", "Barcode (3x3)", "ArUco (OpenCV)"};
    ImGui::Combo("##mkt", &mk.type, types, 3);
    if (mk.type == MK_PATTERN) {
        const char* srcs[] = {"Hiro", "Kanji", "A letter or word", "A picture", "Random blocks"};
        labelled("Made from"); ImGui::Combo("##mks", &mk.source, srcs, 5);
        if (mk.source == 2) { labelled("Letters"); ImGui::InputText("##mkl2", mk.letters, sizeof mk.letters); tip("One to three letters print best"); }
        if (mk.source == 3) {
            if (ImGui::Button(mk.picture.empty() ? "Choose a picture..." : fs::u8path(mk.picture).filename().u8string().c_str(), ImVec2(-1, 0))) {
                const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0"); if (!f.empty()) mk.picture = f;
            }
            ImGui::TextDisabled("Bold, high-contrast pictures track best;\nthe middle half of the square is used.");
        }
        if (mk.source == 4 && ImGui::Button("New random blocks", ImVec2(-1, 0))) mk.blocks = randomBlocks();
        if (mk.source <= 1) ImGui::TextDisabled("The stock marker, as printed everywhere.");
    } else if (mk.type == 2) {
        const int n = Markers::arucoCount(proj.arucoDict);
        labelled("Number"); ImGui::SliderInt("##mka", &mk.arucoId, 0, n - 1);
        std::vector<bool> used(n, false); for (const auto& m : proj.markers) if (m.arucoId >= 0 && m.arucoId < n) used[m.arucoId] = proj.tracking == TRACK_ARUCO;
        if (ImGui::Button("Random (an unused one)", ImVec2(-1, 0))) {
            std::vector<int> free; for (int i = 0; i < n; ++i) if (!used[i]) free.push_back(i);
            if (!free.empty()) { static unsigned r = (unsigned)SDL_GetTicks() * 7u + 3u; r = r * 1664525u + 1013904223u; mk.arucoId = free[(r >> 8) % free.size()]; }
        }
        if (mk.arucoId < n && used[mk.arucoId]) ImGui::TextColored(col4(IM_COL32(0xf0, 0xb0, 0x60, 255)), "This project uses %d already.", mk.arucoId);
        ImGui::TextDisabled("Dictionary %s (Project Settings).\nPrint ArUco markers with their white margin.", Markers::arucoName(proj.arucoDict));
    } else {
        labelled("Number"); ImGui::SliderInt("##mkb", &mk.barcode, 0, 63);
        const auto used = barcodesInUse();
        if (ImGui::Button("Random (an unused one)", ImVec2(-1, 0))) {
            std::vector<int> free; for (int i = 0; i < 64; ++i) if (!used[i]) free.push_back(i);
            if (free.empty()) say("All 64 barcodes are in use");
            else { static unsigned r = (unsigned)SDL_GetTicks(); r = r * 1664525u + 1013904223u; mk.barcode = free[(r >> 8) % free.size()]; }
        }
        if (used[mk.barcode]) ImGui::TextColored(col4(IM_COL32(0xf0, 0xb0, 0x60, 255)), "This project uses %d already.", mk.barcode);
        ImGui::TextDisabled("Up to 64 barcodes can be on the table\nat once; each is its own anchor.");
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (ImGui::Button("Save to the Project", ImVec2(-1, 30))) {
        std::string n; if (saveMakerMarker(n)) { projectDir = markersDir().u8string(); say("Saved " + n + " into assets/markers (.marker, .png to print" + std::string(mk.type == MK_PATTERN ? ", .patt)" : ")")); }
    }
    Scene::Marker* cm = curMarker();
    const bool canUse = cm && (mk.type == 2 ? proj.tracking == TRACK_ARUCO : proj.tracking == TRACK_MARKERS);
    if (ImGui::Button("Use on the selected marker", ImVec2(-1, 0)) && canUse) {
        beginEdit();
        if (mk.type == MK_PATTERN && mk.source == 4) {   // random blocks need their picture somewhere: the project
            std::string n; if (saveMakerMarker(n)) applyMarkerFile(*cm, (markersDir() / fs::u8path(n + ".marker")).u8string());
        } else if (mk.type == 2) { cm->arucoId = mk.arucoId; cm->name = mk.name; shownMarker = -1;
        } else { const Scene::Marker mm = makerMarker(); cm->type = mm.type; cm->barcode = mm.barcode; cm->preset = mm.preset; cm->letter = mm.letter; cm->picture = bring(mm.picture); cm->name = mk.name; shownMarker = -1; }
        say("Marker " + std::string(mk.name) + " is on " + cm->name);
    }
    if (!canUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select a marker (the game tracks Markers)");
    if (ImGui::Button("Save the printable picture...", ImVec2(-1, 0))) {
        const std::string f = saveDialog(L"PNG picture\0*.png\0\0", L"png", std::string(mk.name) + ".png");
        if (!f.empty()) say(Markers::savePng(f, makerPrintable(1024)) ? "Saved: print it at 100%" : "Couldn't save it");
    }
    if (ImGui::Button("Print sheet of the project's markers...", ImVec2(-1, 0))) {
        std::vector<std::pair<Markers::Image, std::string>> all;
        for (const auto& m : proj.markers) if (proj.tracking == TRACK_MARKERS) all.push_back({printable(m, 600), m.name});
        std::error_code ec;
        if (!folder.empty()) for (const auto& e : fs::directory_iterator(markersDir(), ec)) if (e.path().extension() == ".marker") {
            Scene::Marker tmp; if (applyMarkerFile(tmp, e.path().u8string())) all.push_back({printable(tmp, 600), tmp.name});
        }
        if (all.empty()) say("No markers yet");
        else { const std::string f = saveDialog(L"PNG picture\0*.png\0\0", L"png", "markers sheet.png");
               if (!f.empty()) say(Markers::savePng(f, Markers::sheet(all, fontFile)) ? "Saved the sheet (A4, 150 dpi)" : "Couldn't save it"); }
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    // right: the printable marker
    ImGui::SameLine();
    ImGui::BeginChild("##mkr", ImVec2(0, 0), true);
    const std::string key = std::to_string(mk.arucoId) + "/" + std::to_string(proj.arucoDict) + "|" + std::to_string(mk.type) + "|" + std::to_string(mk.source) + "|" + mk.letters + "|" + mk.picture + "|" + std::to_string(mk.barcode) + "|" + std::to_string(mk.blocks.px.size() ? mk.blocks.px[600 * 4] + mk.blocks.px[100000] * 7 + mk.blocks.px[400000] * 13 : 0);
    if (key != mk.shownKey || !mk.tex) { mk.shownKey = key; showImage(mk.tex, makerPrintable(512)); }
    const ImVec2 av = ImGui::GetContentRegionAvail();
    const float side = std::max(64.0f, std::min(av.x, av.y));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (av.x - side) / 2, ImGui::GetCursorPosY() + (av.y - side) / 2));
    ImGui::Image((ImTextureID)(intptr_t)mk.tex, ImVec2(side, side));
    ImGui::EndChild();
    ImGui::End();
}


// ---------- the game's UI: Canvases and their elements, drawn over the Scene view on a phone-shaped frame ----------
static bool showUI = true;
static const float UI_W = 400, UI_H = 711;   // the phone the UI is laid out on (points); the game scales it to the screen
static int canvasOf(const Scene::Marker& m, int id) {   // the Canvas an object is in (-1 none)
    for (int i = indexOfId(m, id); i >= 0; i = indexOfId(m, m.objects[i].parent)) if (m.objects[i].hasCanvas) return m.objects[i].id;
    return -1;
}
// an element's rectangle (screen) inside its parent's
static ImVec4 uiRectIn(const Object& o, const ImVec4& p, float s) {
    const int ax = o.uiAnchor % 3, ay = o.uiAnchor / 3;
    const float w = o.uiRect[2] * s, h = o.uiRect[3] * s;
    const float px = p.x + (p.z - p.x) * ax * 0.5f + o.uiRect[0] * s, py = p.y + (p.w - p.y) * ay * 0.5f + o.uiRect[1] * s;
    const float x0 = px - w * ax * 0.5f, y0 = py - h * ay * 0.5f;
    return ImVec4(x0, y0, x0 + w, y0 + h);
}
static ImU32 u32(const float* c) { return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(c[3] * 255)); }
struct UiHit { int marker, id; ImVec4 r; };
static void drawUiTree(ImDrawList* dl, const Scene::Marker& m, int mi, int parentId, const ImVec4& pr, float s, std::vector<UiHit>& hits) {
    for (const Object& o : m.objects) {
        if (o.parent != parentId || !o.active) continue;
        ImVec4 r = pr;
        if (o.ui != UI_NONE && o.uiOn) {
            r = uiRectIn(o, pr, s);
            const ImVec2 a(r.x, r.y), b(r.z, r.w);
            const float fs = std::max(6.0f, o.uiFont * s);
            ImFont* f = UiFonts::get(o.uiFontFile);
            auto text = [&](const std::string& t, ImVec2 lo, ImVec2 hi, int align) {
                const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str());
                const float x = align == 0 ? lo.x + 4 * s : align == 2 ? hi.x - ts.x - 4 * s : (lo.x + hi.x - ts.x) / 2;
                dl->AddText(f, fs, ImVec2(x, (lo.y + hi.y - ts.y) / 2), u32(o.uiColour), t.c_str());
            };
            const float rad = 6 * s;
            switch (o.ui) {
            case UI_PANEL: dl->AddRectFilled(a, b, u32(o.uiBg), rad); break;
            case UI_TEXT: text(o.uiText, a, b, o.uiAlign); break;
            case UI_BUTTON: dl->AddRectFilled(a, b, u32(o.uiBg), rad); text(o.uiText, a, b, 1); break;
            case UI_SLIDER: {
                const float cy = (a.y + b.y) / 2, t = std::clamp((o.uiValue - o.uiMin) / std::max(1e-6f, o.uiMax - o.uiMin), 0.0f, 1.0f);
                dl->AddRectFilled(ImVec2(a.x, cy - 3 * s), ImVec2(b.x, cy + 3 * s), IM_COL32(255, 255, 255, 90), 3 * s);
                dl->AddRectFilled(ImVec2(a.x, cy - 3 * s), ImVec2(a.x + (b.x - a.x) * t, cy + 3 * s), u32(o.uiBg), 3 * s);
                dl->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * t, cy), std::min(10 * s, (b.y - a.y) / 2), IM_COL32(255, 255, 255, 255));
                break;
            }
            case UI_TOGGLE: {
                const float bx = std::min(b.y - a.y, 24 * s), by = (a.y + b.y - bx) / 2;
                dl->AddRectFilled(ImVec2(a.x, by), ImVec2(a.x + bx, by + bx), o.uiChecked ? u32(o.uiBg) : IM_COL32(255, 255, 255, 60), 4 * s);
                if (o.uiChecked) dl->AddPolyline(std::vector<ImVec2>{ImVec2(a.x + bx * 0.22f, by + bx * 0.52f), ImVec2(a.x + bx * 0.42f, by + bx * 0.72f), ImVec2(a.x + bx * 0.8f, by + bx * 0.3f)}.data(), 3, IM_COL32(255, 255, 255, 255), 0, 2.5f * s);
                text(o.uiText, ImVec2(a.x + bx + 4 * s, a.y), b, 0);
                break;
            }
            case UI_IMAGE: {
                const unsigned t = Viewer::pictureTexture(o.uiFile, nullptr, nullptr);
                if (t) dl->AddImage((ImTextureID)(intptr_t)t, a, b, ImVec2(0, 0), ImVec2(1, 1), u32(o.uiColour));
                else dl->AddRect(a, b, IM_COL32(255, 255, 255, 120), 0, 0, 1);
                break;
            }
            }
            hits.push_back({mi, o.id, r});
        }
        if (o.ui != UI_NONE || o.hasCanvas) drawUiTree(dl, m, mi, o.id, r, s, hits);
    }
}
// the overlay; true when the mouse is on the UI (it picks and drags UI elements, not the 3D view)
static bool uiOverlay(ImVec2 a, ImVec2 avail) {
    bool any = false;
    for (const auto& m : proj.markers) for (const auto& o : m.objects) if (o.hasCanvas) any = true;
    if (!showUI || !any) return false;
    const float h = avail.y - 16, w = h * UI_W / UI_H;
    const ImVec4 frame(a.x + (avail.x - w) / 2, a.y + 8, a.x + (avail.x + w) / 2, a.y + 8 + h);
    const float s = w / UI_W;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(ImVec2(frame.x - 1, frame.y - 1), ImVec2(frame.z + 1, frame.w + 1), IM_COL32(255, 255, 255, 70), 10, 0, 1.5f);
    std::vector<UiHit> hits;
    for (int mi = 0; mi < (int)proj.markers.size(); ++mi) {
        const auto& m = proj.markers[mi];
        for (const Object& o : m.objects) if (o.hasCanvas && o.active && (!o.canvasWithAnchor || mi == selMarker)) drawUiTree(dl, m, mi, o.id, frame, s, hits);
    }
    // the selected one, outlined
    for (const auto& hh : hits) if (hh.marker == selMarker && hh.id == selObject)
        dl->AddRect(ImVec2(hh.r.x - 1, hh.r.y - 1), ImVec2(hh.r.z + 1, hh.r.w + 1), C_BLUE, 0, 0, 2);
    // pick (the topmost: drawn last) and drag
    static int dragId = -1, dragMarker = -1, dragAxis = 2;   // dragAxis: 0 x only, 1 y only, 2 both
    const ImVec2 mp = ImGui::GetMousePos();
    const bool inView = mp.x >= a.x && mp.y >= a.y && mp.x < a.x + avail.x && mp.y < a.y + avail.y && ImGui::IsWindowHovered();
    // the selected element's move arrows, as Unity's: red x (right), green y (up), the square in the middle both
    int arrow = -1;
    for (const auto& hh : hits) if (hh.marker == selMarker && hh.id == selObject) {
        const ImVec2 c((hh.r.x + hh.r.z) / 2, (hh.r.y + hh.r.w) / 2);
        const float L = 46, hw = 7;
        const float dx = mp.x - c.x, dy = mp.y - c.y;
        if (std::fabs(dx) <= hw && std::fabs(dy) <= hw) arrow = 2;
        else if (dx > hw && dx <= L + 10 && std::fabs(dy) <= 7) arrow = 0;
        else if (-dy > hw && -dy <= L + 10 && std::fabs(dx) <= 7) arrow = 1;
        if (dragId >= 0) arrow = dragAxis;
        const ImU32 red = arrow == 0 ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 64, 64, 255);
        const ImU32 green = arrow == 1 ? IM_COL32(255, 220, 90, 255) : IM_COL32(90, 215, 90, 255);
        const ImU32 both = arrow == 2 ? IM_COL32(255, 220, 90, 230) : IM_COL32(80, 140, 255, 200);
        dl->AddLine(c, ImVec2(c.x + L, c.y), red, 2.5f);
        dl->AddTriangleFilled(ImVec2(c.x + L + 10, c.y), ImVec2(c.x + L, c.y - 6), ImVec2(c.x + L, c.y + 6), red);
        dl->AddLine(c, ImVec2(c.x, c.y - L), green, 2.5f);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - L - 10), ImVec2(c.x - 6, c.y - L), ImVec2(c.x + 6, c.y - L), green);
        dl->AddRectFilled(ImVec2(c.x - hw, c.y - hw), ImVec2(c.x + hw, c.y + hw), both);
        break;
    }
    int over = -1;
    for (int i = (int)hits.size() - 1; i >= 0; --i) if (mp.x >= hits[i].r.x && mp.x <= hits[i].r.z && mp.y >= hits[i].r.y && mp.y <= hits[i].r.w) { over = i; break; }
    if (inView && arrow >= 0 && dragId < 0 && ImGui::IsMouseClicked(0)) { beginEdit(); dragId = selObject; dragMarker = selMarker; dragAxis = arrow; }
    else if (inView && over >= 0 && ImGui::IsMouseClicked(0)) { beginEdit(); selMarker = hits[over].marker; selObject = hits[over].id; dragId = selObject; dragMarker = selMarker; dragAxis = 2; }
    if (dragId >= 0) {
        if (!ImGui::IsMouseDown(0)) dragId = -1;
        else if (Scene::Marker* m = dragMarker < (int)proj.markers.size() ? &proj.markers[dragMarker] : nullptr) {
            const int i = indexOfId(*m, dragId);
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (i >= 0) { if (dragAxis != 1) m->objects[i].uiRect[0] += d.x / s; if (dragAxis != 0) m->objects[i].uiRect[1] += d.y / s; }
        }
        return true;
    }
    if (inView && arrow >= 0) return true;
    return inView && over >= 0;
}
// a UI element: under the selected Canvas (or its elements), else in a new Canvas, like Unity
static void createUI(int type) {
    Marker* m = curMarker();
    if (!m) { Scene::Marker k; k.id = newId(proj); k.name = std::string(anchorWord()) + " 1"; proj.markers.push_back(k); selMarker = 0; m = curMarker(); }
    beginEdit();
    int canvas = selObject >= 0 ? canvasOf(*m, selObject) : -1;
    if (type == 0 || canvas < 0) {
        Object c = make(proj, EMPTY, -1); c.name = "Canvas"; c.hasCanvas = true;
        m->objects.push_back(c); canvas = c.id;
        if (type == 0) { selObject = c.id; return; }
    }
    int parent = canvas;
    if (selObject >= 0) { const int i = indexOfId(*m, selObject); if (i >= 0 && (m->objects[i].ui == UI_PANEL || m->objects[i].hasCanvas)) parent = selObject; }
    Object o = make(proj, EMPTY, parent);
    o.ui = type; o.name = UI_NAMES[type];
    switch (type) {
    case UI_TEXT: o.uiText = "New Text"; o.uiRect[2] = 240; o.uiRect[3] = 40; break;
    case UI_BUTTON: o.uiText = "Button"; break;
    case UI_SLIDER: o.uiRect[2] = 240; o.uiRect[3] = 32; break;
    case UI_TOGGLE: o.uiText = "Toggle"; o.uiRect[2] = 200; o.uiRect[3] = 32; break;
    case UI_IMAGE: { const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0"); o.uiFile = f.empty() ? "" : bring(f); o.uiRect[2] = o.uiRect[3] = 120; break; }
    case UI_PANEL: o.uiRect[2] = 320; o.uiRect[3] = 200; o.uiBg[0] = 0.1f; o.uiBg[1] = 0.1f; o.uiBg[2] = 0.11f; o.uiBg[3] = 0.85f; break;
    }
    m->objects.push_back(o);
    reparent(*m, o.id, parent);
    selObject = o.id;
}
static void uiMenuItems() {
    if (ImGui::MenuItem("Canvas")) createUI(0);
    ImGui::Separator();
    for (int k = 1; k < UI_COUNT; ++k) if (ImGui::MenuItem((std::string(UI_NAMES[k]) + (k == UI_IMAGE ? "..." : "")).c_str())) createUI(k);
}


// ---------- the Scene Manager: the game's scenes (scenes/<name>.scene), in build order; scene 0 is index.html ----------
static fs::path sceneFile(const std::string& name) { return fs::u8path(folder) / "scenes" / fs::u8path(name + ".scene"); }
static void sceneToFile(const Project& p, const std::string& name) {   // its anchors and objects, paths relative to the project
    if (folder.empty()) return;
    Project rel = p;
    const fs::path base = fs::u8path(folder);
    forEachFile(rel, [&](std::string& s) { if (!s.empty()) { std::error_code ec; const fs::path r = fs::relative(fs::u8path(s), base, ec); if (!ec && !r.empty() && r.u8string().rfind("..", 0) != 0) s = r.generic_u8string(); } });
    std::error_code ec; fs::create_directories(fs::u8path(folder) / "scenes", ec);
    std::ofstream(sceneFile(name), std::ios::binary) << Scene::toJson(rel);
}
static bool sceneFromFile(const std::string& name, std::vector<Scene::Marker>& out, bool bumpIds, Project* whole) {
    std::ifstream f(sceneFile(name), std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    Project t; std::string err; if (!Scene::fromJson(ss.str(), t, err)) return false;
    const fs::path base = fs::u8path(folder);
    forEachFile(t, [&](std::string& s) { if (!s.empty() && fs::u8path(s).is_relative()) s = (base / fs::u8path(s)).u8string(); });
    if (bumpIds) proj.nextId = std::max(proj.nextId, t.nextId);
    out = t.markers;
    if (whole) *whole = t;
    return true;
}
static std::vector<Scene::Marker> sceneMarkers(const std::string& name) {   // a scene's anchors (the open one: as edited)
    if (name == proj.scene) return proj.markers;
    std::vector<Scene::Marker> ms; sceneFromFile(name, ms, false); return ms;
}
static std::vector<Scene::Marker> emptyScene() {
    std::vector<Scene::Marker> v;
    Scene::Marker k; k.id = newId(proj); k.name = proj.tracking == TRACK_MARKERS ? "Hiro" : std::string(anchorWord()) + " 1"; v.push_back(k);
    return v;
}
// a scene's Main Camera (its settings and what rides with it)
static void copyCamera(Project& to, const Project& from) {
    to.camera = from.camera; to.camClear = from.camClear; memcpy(to.camColour, from.camColour, sizeof to.camColour);
    to.camNear = from.camNear; to.camFar = from.camFar; to.camFov = from.camFov;
}
static void openScene(const std::string& name) {
    if (name == proj.scene) return;
    if (!folder.empty()) saveProject(false);
    std::vector<Scene::Marker> ms; Project whole;
    if (!sceneFromFile(name, ms, true, &whole)) { ms = emptyScene(); whole = Project(); }
    copyCamera(proj, whole);
    proj.markers = ms; proj.scene = name;
    undoStack.clear(); redoStack.clear(); frameStart = proj;
    selMarker = 0; selObject = -1; shownMarker = -1;
    if (!folder.empty()) saveProject(false);
    say("Scene " + name);
}
static std::string uniqueSceneName(std::string base) {
    for (auto& c : base) if (strchr("\\/:*?\"<>|", c)) c = '_';
    if (base.empty()) base = "Scene";
    std::string n = base;
    for (int k = 2; std::find(proj.scenes.begin(), proj.scenes.end(), n) != proj.scenes.end(); ++k) n = base + " " + std::to_string(k);
    return n;
}
static void newScene(bool duplicateOpen) {
    if (folder.empty()) { say("Save the project first"); return; }
    saveProject(false);
    const std::string name = uniqueSceneName(duplicateOpen ? proj.scene : "Scene " + std::to_string(proj.scenes.size() + 1));
    Project t = proj;
    t.markers = duplicateOpen ? proj.markers : emptyScene();
    if (!duplicateOpen) copyCamera(t, Project());   // a new scene: a new Main Camera
    sceneToFile(t, name);
    proj.scenes.push_back(name);
    openScene(name);
}
// a new scene made from a template (its anchors, objects, Main Camera and scripts), for a game of the same tracking
static void newSceneFromTemplate(int which) {
    if (folder.empty()) { say("Save the project first"); return; }
    const std::string dir = (fs::u8path(exeDir) / "templates" / "gallery").u8string() + "/";
    const Project tmpl = which == 4 ? physicsTemplate() : which == 3 ? planeDemoTemplate() : which == 2 ? navigationTemplate() : which == 1 ? galleryTemplate(dir) : basicTemplate();
    if (tmpl.tracking != proj.tracking) { say(std::string("That template is for ") + TRACKING_NAMES[tmpl.tracking] + " games (Edit > Project Settings)"); return; }
    saveProject(false);
    const char* names[] = {"Cube on a marker", "Art gallery", "Indoor navigation", "Place cubes", "Physics"};
    const std::string name = uniqueSceneName(names[which]);
    Project t = proj;
    t.markers = tmpl.markers;
    copyCamera(t, tmpl);
    sceneToFile(t, name);
    proj.scenes.push_back(name);
    openScene(name);
    proj.nextId = std::max(proj.nextId, tmpl.nextId);
    if (which == 3) { attachPlaceCubes(); saveProject(false); }   // its script (tap a plane: a cube)
    if (which == 4) { attachPhysicsScripts(); saveProject(false); }   // its scripts (a tap drops a ball; the zone)
    say("Made the scene " + name);
}
static void sceneManager() {
    if (!showScenes) return;
    ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(500, 160), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scenes (Build Settings)", &showScenes)) { ImGui::End(); return; }
    ImGui::TextDisabled("The game's scenes in build order: scene 0 is index.html, where the game starts.\nDrag to reorder, double-click to open. Scripts: loadScene(\"Name\") or loadScene(1).");
    ImGui::Separator();
    static int renamingScene = -1; static char nameBuf[64];
    int moveFrom = -1, moveTo = -1, del = -1, open = -1;
    ImGui::BeginChild("##scenes", ImVec2(0, -40), true);
    for (int i = 0; i < (int)proj.scenes.size(); ++i) {
        ImGui::PushID(i);
        const std::string& n = proj.scenes[i];
        const bool cur = n == proj.scene;
        if (renamingScene == i) {
            ImGui::SetNextItemWidth(-1); ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rn", nameBuf, sizeof nameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                const std::string nn = uniqueSceneName(nameBuf);
                if (nameBuf[0] && nn != n) {
                    if (!folder.empty()) { saveProject(false); std::error_code ec; fs::rename(sceneFile(n), sceneFile(nn), ec); }
                    if (cur) proj.scene = nn;
                    proj.scenes[i] = nn;
                    if (!folder.empty()) saveProject(false);
                }
                renamingScene = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) renamingScene = -1;
        } else {
            char label[160]; snprintf(label, sizeof label, "%d   %s%s", i, n.c_str(), i == 0 ? "   (index.html)" : "");
            if (ImGui::Selectable(label, cur, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) open = i;
            if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("SCENE", &i, sizeof i); ImGui::Text("%s", n.c_str()); ImGui::EndDragDropSource(); }
            if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SCENE")) { moveFrom = *(const int*)pl->Data; moveTo = i; } ImGui::EndDragDropTarget(); }
            if (ImGui::BeginPopupContextItem("ctx")) {
                if (ImGui::MenuItem("Open")) open = i;
                if (ImGui::MenuItem("Rename")) { renamingScene = i; snprintf(nameBuf, sizeof nameBuf, "%s", n.c_str()); }
                if (ImGui::MenuItem("Make it scene 0 (index.html)", nullptr, false, i > 0)) { moveFrom = i; moveTo = 0; }
                if (ImGui::MenuItem("Remove from the game", nullptr, false, proj.scenes.size() > 1 && !cur)) del = i;
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) { const std::string n = proj.scenes[moveFrom]; proj.scenes.erase(proj.scenes.begin() + moveFrom); proj.scenes.insert(proj.scenes.begin() + moveTo, n); if (!folder.empty()) saveProject(false); }
    if (del >= 0) { proj.scenes.erase(proj.scenes.begin() + del); if (!folder.empty()) saveProject(false); say("Removed from the game (its file stays in scenes/)"); }
    if (open >= 0) openScene(proj.scenes[open]);
    if (ImGui::Button("New Scene", ImVec2(140, 28))) newScene(false);
    ImGui::SameLine(); if (ImGui::Button("Duplicate This Scene", ImVec2(170, 28))) newScene(true);
    ImGui::SameLine(); if (ImGui::Button("From Template...", ImVec2(130, 28))) ImGui::OpenPopup("tmplscene");
    if (ImGui::BeginPopup("tmplscene")) {
        struct T { int which; const char* label; int tracking; };
        const T ts[] = {{3, "Place cubes: planes found, tap one, a cube lands", TRACK_SURFACES}, {2, "Indoor navigation: a path with arrows", TRACK_SURFACES},
                        {0, "A cube on a marker", TRACK_MARKERS}, {4, "Physics: a stack, balls, a spinning paddle, a trigger; tap to drop a ball", TRACK_MARKERS}, {1, "The art gallery: a window into the marker", TRACK_MARKERS}};
        for (const T& x : ts) {
            if (ImGui::MenuItem(x.label, nullptr, false, x.tracking == proj.tracking)) newSceneFromTemplate(x.which);
            if (x.tracking != proj.tracking && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("For %s games (Edit > Project Settings)", TRACKING_NAMES[x.tracking]);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(); ImGui::TextDisabled("right-click: more");
    ImGui::End();
}

// ---------- the menus ----------
// the Hierarchy's clipboard: an object and everything under it (Copy, then Paste / Paste As Child, as Unity's)
static std::vector<Object> objClip;
static int renamingObj = -1; static char objNameBuf[128];
static void copyObject(const Scene::Marker& m, int id) {
    objClip.clear();
    for (const Object& o : m.objects) if (isUnder(m, o.id, id)) objClip.push_back(o);
    say("Copied " + (objClip.empty() ? std::string("nothing") : objClip[0].name));
}
// a copy of the clipboard under parent (-1: the top level), new ids; returns the copy's id
static int pasteObject(Scene::Marker& m, int parent) {
    if (objClip.empty()) return -1;
    beginEdit();
    std::map<int, int> ids; for (const auto& o : objClip) ids[o.id] = newId(proj);
    for (Object o : objClip) { o.parent = o.id == objClip[0].id ? parent : ids[o.parent]; o.id = ids[o.id]; m.objects.push_back(o); }
    const int root = ids[objClip[0].id];
    reparent(m, root, parent);
    return root;
}
static void gameObjectItems() {
    if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) create(EMPTY);
    if (ImGui::BeginMenu("3D Object")) {
        for (int k : {PLANE, CUBE, SPHERE, CAPSULE, CYLINDER, QUAD}) if (ImGui::MenuItem(KIND_NAMES[k])) create(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI")) { uiMenuItems(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Audio Source...")) {   // an object that plays a sound (3D: quieter further from the phone)
            const std::string f = openDialog(L"Sound\0*.mp3;*.ogg;*.wav;*.m4a\0\0");
            if (!f.empty()) { create(EMPTY); if (Object* o = curObject()) { o->name = fs::u8path(f).stem().u8string(); o->hasAudio = true; o->audioOn = true; o->audioFile = bring(f); } }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("AR")) {
        if (ImGui::MenuItem("Picture...")) create(PICTURE);
        if (ImGui::MenuItem("Model...")) create(MODEL);
        if (ImGui::MenuItem("Sprites...")) create(SPRITES);
        if (ImGui::MenuItem("Light rays")) create(RAYS);
        if (ImGui::MenuItem("Words")) create(WORDS);
        ImGui::Separator();
        if (ImGui::MenuItem((std::string("Anchor: ") + anchorWord()).c_str())) addMarker();
        ImGui::EndMenu();
    }
}
static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New")) newFromTemplate(0);
        if (ImGui::BeginMenu("New from template")) {
            if (ImGui::MenuItem("Art gallery")) newFromTemplate(1);
            if (ImGui::MenuItem("Indoor navigation (surfaces)")) newFromTemplate(2);
            if (ImGui::MenuItem("Plane detection demo (tap to place cubes)")) newFromTemplate(3);
            if (ImGui::MenuItem("A cube on Hiro")) newFromTemplate(0);
            if (ImGui::MenuItem("Physics (falling, bouncing, a trigger)")) newFromTemplate(4);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) { const std::string f = openDialog(L"Diorama project\0*.diorama\0\0"); if (!f.empty()) openProject(f); }
        {   // the project that was open last, one click away
            const std::string last = lastProject();
            const bool ok = !last.empty() && last != folder;
            if (ImGui::MenuItem(ok ? ("Open recent: " + fs::u8path(last).filename().u8string()).c_str() : "Open recent", nullptr, false, ok)) openProject(last);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene")) newScene(false);
        if (ImGui::BeginMenu("Open Scene", proj.scenes.size() > 1)) {
            for (const auto& n : proj.scenes) if (ImGui::MenuItem(n.c_str(), nullptr, n == proj.scene)) { const std::string c = n; openScene(c); break; }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Scenes (Build Settings)...")) showScenes = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject(false);
        if (ImGui::MenuItem("Save As...")) saveProject(true);
        if (!folder.empty() && ImGui::MenuItem("Show the project folder")) openInExplorer(folder);
        ImGui::Separator();
        if (ImGui::MenuItem("Play in the browser", "Ctrl+P")) playInBrowser();
        if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) showBuild = true;
        if (ImGui::MenuItem((std::string("Build And Run (") + platformOf(proj.platform)->name + ")").c_str(), "Ctrl+B")) startBuild(proj.platform, true);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Project Settings...")) showSettings = true;
        if (ImGui::MenuItem("Preferences...")) showPrefs = true;
        ImGui::Separator();
        Marker* m = curMarker();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m && selObject >= 0)) selObject = duplicate(proj, *m, selObject);
        if (ImGui::MenuItem("Delete", "Del", false, m != nullptr)) {
            if (selObject >= 0) { remove(*m, selObject); selObject = -1; }
            else if (selMarker >= 0) { proj.markers.erase(proj.markers.begin() + selMarker); selMarker = std::max(0, selMarker - 1); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) { gameObjectItems(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Scenes", nullptr, showScenes)) showScenes = !showScenes;
        if (ImGui::MenuItem("Build Settings", "Ctrl+Shift+B", showBuild)) showBuild = !showBuild;
        if (ImGui::MenuItem("Marker Maker", nullptr, showMaker)) showMaker = !showMaker;
        if (ImGui::MenuItem("Script Editor", nullptr, showScripts, !scripts.empty())) showScripts = !showScripts;
        ImGui::Separator();
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
        ImGui::MenuItem("Scene", nullptr, &showSceneWin);
        ImGui::MenuItem("Inspector", nullptr, &showInspector);
        ImGui::MenuItem("Project", nullptr, &showProject);
        ImGui::MenuItem("Console", nullptr, &showConsole);
        ImGui::MenuItem("Profiler", nullptr, &showProfiler);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) { if (ImGui::MenuItem("About Diorama")) showAbout = true; ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}


// ---------- prefabs: an object and everything under it, kept as a file (assets/<name>.prefab) ----------
// the file is a small Diorama project with one marker holding the objects (paths relative to the project)
static bool savePrefab(const Scene::Marker& m, int rootId, const std::string& dir) {
    Project tmp; Scene::Marker k; k.name = "prefab";
    for (const Object& o : m.objects) if (isUnder(m, o.id, rootId)) { Object c = o; if (c.id == rootId) c.parent = -1; k.objects.push_back(c); }
    if (k.objects.empty()) return false;
    const fs::path base = fs::u8path(folder);
    for (auto& o : k.objects) {
        auto rel = [&](std::string& f) { if (f.empty()) return; std::error_code ec; const fs::path r = fs::relative(fs::u8path(f), base, ec); if (!ec && r.u8string().rfind("..", 0) != 0) f = r.generic_u8string(); };
        rel(o.file); rel(o.audioFile); rel(o.particleFile); rel(o.videoFile); rel(o.uiFile); rel(o.uiFontFile); rel(o.canvasCss); for (auto& sc : o.scripts) rel(sc.file);
    }
    tmp.markers.push_back(k);
    std::string name = k.objects[0].name; for (auto& c : name) if (strchr("<>:\"/\\|?*", c)) c = '_';
    fs::path f = fs::u8path(dir) / fs::u8path(name + ".prefab");
    std::ofstream(f, std::ios::binary) << Scene::toJson(tmp);
    say("Made a prefab: " + f.filename().u8string());
    return true;
}
static bool loadPrefab(const std::string& file, std::vector<Object>& out) {
    std::ifstream in(fs::u8path(file), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
    Project tmp; std::string err;
    if (!Scene::fromJson(ss.str(), tmp, err) || tmp.markers.empty()) return false;
    out = tmp.markers[0].objects;
    const fs::path base = fs::u8path(folder);
    for (auto& o : out) {
        auto abs = [&](std::string& f) { if (!f.empty() && fs::u8path(f).is_relative()) f = (base / fs::u8path(f)).u8string(); };
        abs(o.file); abs(o.audioFile); abs(o.particleFile); abs(o.videoFile); abs(o.uiFile); abs(o.uiFontFile); abs(o.canvasCss); for (auto& sc : o.scripts) abs(sc.file);
    }
    return !out.empty();
}
// a copy of the prefab under onto (-1: the marker's top level), with new ids
static int placePrefab(Scene::Marker& m, const std::string& file, int onto) {
    std::vector<Object> objs;
    if (!loadPrefab(file, objs)) { say("Couldn't read " + fs::u8path(file).filename().u8string()); return -1; }
    std::map<int, int> ids;
    for (auto& o : objs) ids[o.id] = newId(proj);
    for (auto& o : objs) { o.parent = o.parent == -1 ? onto : ids[o.parent]; o.id = ids[o.id]; o.prefab = file; m.objects.push_back(o); }
    reparent(m, objs[0].id, onto);
    return objs[0].id;
}

// ---------- assets dropped from the Project window ----------
static void dropAsset(Scene::Marker& m, int mi, int onto, const std::string& file) {
    beginEdit();
    std::string e = fs::u8path(file).extension().u8string(); for (auto& c : e) c = (char)tolower((unsigned char)c);
    const int i = onto >= 0 ? indexOfId(m, onto) : -1;
    if (e == ".js") {
        if (i < 0) { say("Drop a script onto an object"); return; }
        m.objects[i].scripts.push_back({file, true});
        selMarker = mi; selObject = onto;
        return;
    }
    if (e == ".prefab") { const int id = placePrefab(m, file, onto); if (id >= 0) { selMarker = mi; selObject = id; } return; }
    if (e == ".material") {
        if (i < 0 || m.objects[i].kind == EMPTY) { say("Drop a material onto an object with a look (a mesh, a picture, a model)"); return; }
        Object mat; if (!loadMaterial(file, mat)) { say("Couldn't read the material"); return; }
        m.objects[i].material = file; applyMaterial(mat, m.objects[i]); selMarker = mi; selObject = onto;
        say(m.objects[i].name + " has the material " + fs::u8path(file).stem().u8string());
        return;
    }
    if (e == ".marker") { if (applyMarkerFile(m, file)) { selMarker = mi; selObject = -1; say("The marker is now " + m.name); } return; }
    if (e == ".mp3" || e == ".ogg" || e == ".wav" || e == ".m4a") {
        if (i < 0) { say("Drop a sound onto an object: it gets an Audio Source"); return; }
        Object& t = m.objects[i]; t.hasAudio = true; t.audioOn = true; t.audioFile = file; selMarker = mi; selObject = onto; return;
    }
    if (e == ".mp4" || e == ".webm" || e == ".mov" || e == ".m4v" || e == ".mkv" || e == ".avi" || e == ".gif" || e == ".apng") {
        if (i < 0) { Object o = make(proj, EMPTY, -1); o.name = fs::u8path(file).stem().u8string(); m.objects.push_back(o); giveVideo(m.objects.back(), file); selMarker = mi; selObject = o.id; return; }
        giveVideo(m.objects[i], file); selMarker = mi; selObject = onto; return;
    }
    int kind = -1;
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".jfif") kind = PICTURE;
    if (e == ".glb" || e == ".gltf") kind = MODEL;
    if (kind < 0) { say("Diorama can't place that kind of file yet"); return; }
    Object o = make(proj, kind, onto);
    o.file = file; o.name = fs::u8path(file).stem().u8string();
    if (kind == MODEL) { const auto c = Viewer::clipsOf(file); o.clip = c.empty() ? "" : c[0]; }
    m.objects.push_back(o);
    reparent(m, o.id, onto);
    selMarker = mi; selObject = o.id;
}
static void acceptAsset(Scene::Marker& m, int mi, int onto) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, mi, onto, std::string((const char*)pl->Data));
        ImGui::EndDragDropTarget();
    }
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
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, mi, o.id, std::string((const char*)pl->Data));
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        selMarker = mi; selObject = o.id; selAsset.clear();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) copyObject(m, o.id);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !objClip.empty())) { const int id = pasteObject(m, o.parent); if (id >= 0) selObject = id; }
        if (ImGui::MenuItem("Paste As Child", nullptr, false, !objClip.empty())) { const int id = pasteObject(m, o.id); if (id >= 0) selObject = id; }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2")) { renamingObj = o.id; snprintf(objNameBuf, sizeof objNameBuf, "%s", o.name.c_str()); }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { beginEdit(); selObject = duplicate(proj, m, o.id); }
        if (ImGui::MenuItem("Delete", "Del")) { beginEdit(); moveId = o.id; moveTo = -99; }
        ImGui::Separator();
        if (ImGui::MenuItem("Make Prefab", nullptr, false, !folder.empty())) savePrefab(m, o.id, projectDir.empty() ? (fs::u8path(folder) / "assets").u8string() : projectDir);
        if (folder.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Save the project first: prefabs go in its assets");
        ImGui::Separator();
        if (ImGui::BeginMenu("Create Under It")) { gameObjectItems(); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }
    if (open) {
        for (int k : kids) objectNode(m, mi, k, moveId, moveTo);
        ImGui::TreePop();
    }
}
static void hierarchyItems();
static void hierarchy() { hierarchyItems(); }
static void hierarchyItems() {
    ImGui::TextDisabled("Scene: %s", proj.scene.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The open scene (Window > Scenes)");
    if (ImGui::IsItemClicked()) showScenes = true;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("create");
    if (ImGui::BeginPopup("create")) { gameObjectItems(); ImGui::EndPopup(); }
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    {   // the Main Camera: every scene has one
        Scene::Marker& m = proj.camera; const int mi = -2;
        ImGui::PushID("camera");
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (m.objects.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
        if (selMarker == mi && selObject < 0) fl |= ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(m.expanded);
        const bool open = ImGui::TreeNodeEx("##cam", fl, "\xe2\x97\x89 Main Camera");
        m.expanded = open;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The phone (or the webcam). Objects under it ride with it: a torch, a HUD, a held thing");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selMarker = mi; selObject = -1; }
        int moveId = -1, moveTo = -1;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("OBJ")) { const int* d = (const int*)pl->Data; if (d[0] == mi) { moveId = d[1]; moveTo = -1; } }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, mi, -1, std::string((const char*)pl->Data));
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) { selMarker = mi; selObject = -1; gameObjectItems(); ImGui::EndPopup(); }
        if (open) { for (int k : childrenOf(m, -1)) objectNode(m, mi, k, moveId, moveTo); ImGui::TreePop(); }
        if (moveId >= 0) { if (moveTo == -99) { remove(m, moveId); if (selObject == moveId) selObject = -1; } else reparent(m, moveId, moveTo); }
        ImGui::PopID();
    }
    for (int mi = 0; mi < (int)proj.markers.size(); ++mi) {
        Scene::Marker& m = proj.markers[mi];
        ImGui::PushID(m.id);
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (selMarker == mi && selObject < 0) fl |= ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(m.expanded);
        ImGui::PushStyleColor(ImGuiCol_Text, col4(IM_COL32(0x34, 0xa9, 0xe3, 255)));
        const bool open = ImGui::TreeNodeEx("##m", fl, "%s %s  (%s)", anchorIcon(), m.name.c_str(), anchorWord());   // what kind of anchor, at a glance
        ImGui::PopStyleColor();
        m.expanded = open;
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selMarker = mi; selObject = -1; }
        int moveId = -1, moveTo = -1;
        if (ImGui::BeginDragDropTarget()) {   // dropped on the marker: back to the top level
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("OBJ")) { const int* d = (const int*)pl->Data; if (d[0] == mi) { moveId = d[1]; moveTo = -1; } }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, mi, -1, std::string((const char*)pl->Data));
            ImGui::EndDragDropTarget();
        }
        int deleteAnchor = -1;
        if (ImGui::BeginPopupContextItem()) {
            selMarker = mi; selObject = -1; gameObjectItems();
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) deleteAnchor = mi;
            ImGui::EndPopup();
        }
        if (deleteAnchor >= 0) {
            beginEdit(); proj.markers.erase(proj.markers.begin() + deleteAnchor);
            selMarker = proj.markers.empty() ? -1 : std::max(0, deleteAnchor - 1); selObject = -1; shownMarker = -1;
            ImGui::PopID(); break;
        }
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
    if (renamingObj >= 0) { ImGui::OpenPopup("Rename object"); }
    if (ImGui::BeginPopupModal("Rename object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(300);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##on", objNameBuf, sizeof objNameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        Object* ro = nullptr;
        for (int a = -1; a < (int)proj.markers.size() && !ro; ++a) { Scene::Marker& rm = a < 0 ? proj.camera : proj.markers[a]; const int k = indexOfId(rm, renamingObj); if (k >= 0) ro = &rm.objects[k]; }
        if ((enter || ImGui::Button("Rename", ImVec2(145, 0))) && ro && objNameBuf[0]) { beginEdit(); ro->name = objNameBuf; renamingObj = -1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(145, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !ro) { renamingObj = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    // what's left of the window: files and objects dropped there go to the selected anchor's top level
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (rest.x > 4 && rest.y > 4) {
        ImGui::InvisibleButton("##hrest", rest, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(0)) selAsset.clear();
        if (ImGui::BeginPopupContextItem("##hctx")) {
            if (selMarker < 0 && selMarker != -2 && !proj.markers.empty()) selMarker = 0;
            selObject = -1; selAsset.clear();
            Scene::Marker* pm = curMarker();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, pm && !objClip.empty())) { const int id = pasteObject(*pm, -1); if (id >= 0) selObject = id; }
            ImGui::Separator();
            gameObjectItems();
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) {
                Scene::Marker* dm = curMarker(); int dmi = selMarker;
                if (!dm && !proj.markers.empty()) { dm = &proj.markers[0]; dmi = 0; }
                if (dm) dropAsset(*dm, dmi, -1, std::string((const char*)pl->Data));
            }
            ImGui::EndDragDropTarget();
        }
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(0) && ImGui::IsAnyItemHovered()) selAsset.clear();
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
    ImGui::TextDisabled("%s anchor (Edit > Project Settings: what the game tracks)", anchorWord());
    if (proj.tracking == TRACK_IMAGES && section("Picture to look for")) {
        fileRow("Picture", m.target, L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0");
        ImGui::TextDisabled("Any photo, poster or page; it's 1 across in the scene.\nDetailed pictures with corners and texture track best;\nplain or repeating ones don't.");
    }
    if (proj.tracking == TRACK_FACES && section("Face")) {
        labelled("On the");
        const auto& cur = facePointOf(m);
        if (ImGui::BeginCombo("##fp", cur.name)) {
            for (int i = 0; i < FACE_POINT_COUNT; ++i) if (ImGui::Selectable(FACE_POINTS[i].name, m.facePoint == FACE_POINTS[i].landmark)) m.facePoint = FACE_POINTS[i].landmark;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("1 unit is the face's width; +Z comes out of the face.\nThe head hides what's behind it (a hat's back, glasses' arms).");
    }
    if (proj.tracking == TRACK_SURFACES && section("AR Plane Manager")) {
        const char* modes[] = {"Horizontal (floors, tables)", "Vertical (walls)", "Both"};
        labelled("Detection"); ImGui::Combo("##pd", &m.planeDetect, modes, 3);
        ImGui::Checkbox("Show the planes found", &m.showPlanes); tip("The dotted look on floors, tables and walls as they're found");
        if (m.showPlanes) {
            labelled("Plane look"); ImGui::ColorEdit3("##pc", m.planeColour, ImGuiColorEditFlags_NoInputs);
            labelled("Plane opacity"); ImGui::SliderFloat("##po", &m.planeColour[3], 0, 1, "%.2f"); tip("How much the planes found show: their fill, grid and edge (0: hidden, still there to tap)");
        }
        fileRow("Plane prefab", m.planePrefab, L"Prefab\0*.prefab\0\0"); tip("Made on each plane found (AR Foundation's plane prefab); empty: just the look");
        const char* places[] = {"Tap to place", "On the first floor found", "Where the phone starts"};
        labelled("Placement"); ImGui::Combo("##pl", &m.placement, places, 3);
        tip("Where this anchor's things go: where the player taps on a plane, on the first floor found, or round the phone where the game starts");
        ImGui::TextDisabled("Surfaces are in metres: 1 = 1 m. The room here is pretend: a floor,\na table and a wall, like the planes a phone finds.");
        ImGui::SeparatorText("Any camera (no ARCore)");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("PCs and phones without ARCore: the player points at a table or floor with some pattern and taps; it's followed from there.");
        ImGui::PopTextWrapPos();
        const char* kinds[] = {"Auto", "A table or floor (it looks down)", "Facing it (a wall, a poster, a screen)"};
        labelled("Surface"); ImGui::Combo("##wsk", &m.webcamSurface, kinds, 3);
        tip("Auto: a phone held upright locks onto what faces it, tilted down onto a table or floor; a PC webcam: a table or floor");
        labelled("Looks down"); ImGui::SliderFloat("##wt", &m.webcamTilt, 10, 90, "%.0f\xc2\xb0"); tip("How far a webcam looks down at the table (phones use their own gravity)");
        labelled("Height"); ImGui::SliderFloat("##wh", &m.webcamHeight, 0.1f, 3, "%.2f m"); tip("The camera's height above the surface: sets the scale");
        labelled("Field of view"); ImGui::SliderFloat("##wf", &m.webcamFov, 30, 110, "%.0f\xc2\xb0"); tip("Across; most webcams 60-80");
    }
    if (proj.tracking == TRACK_ARUCO && section("ArUco Marker")) {
        labelled("Number"); ImGui::SliderInt("##aid", &m.arucoId, 0, Markers::arucoCount(proj.arucoDict) - 1);
        ImGui::TextDisabled("Dictionary %s (Edit > Project Settings). Each number is its own anchor;\nmany can be on the table at once. 1 unit = the marker's black square.", Markers::arucoName(proj.arucoDict));
        if (ImGui::Button("Marker Maker...", ImVec2(-1, 0))) showMaker = true;
        if (ImGui::Button("Save the printable marker...", ImVec2(-1, 0))) {
            const std::string f = saveDialog(L"PNG picture\0*.png\0\0", L"png", m.name + ".png");
            if (!f.empty()) say(Markers::savePng(f, printable(m, 1024)) ? "Saved " + fs::u8path(f).filename().u8string() + ": print it at 100%" : "Couldn't save it");
        }
    }
    if (proj.tracking == TRACK_MARKERS && section("AR Marker")) {
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
                    const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0");
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
        if (ImGui::Button("Marker Maker...", ImVec2(-1, 0))) showMaker = true;
        tip("Make markers (random barcodes and patterns too) and save them into the project;\ndrop a .marker from the Project window here or on the marker in the Hierarchy");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) { const std::string f((const char*)pl->Data); if (fs::u8path(f).extension() == ".marker") { beginEdit(); applyMarkerFile(m, f); } }
            ImGui::EndDragDropTarget();
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
        ImGui::Checkbox("The sun casts shadows", &m.sunShadows);
        ImGui::Checkbox("Shadows on the real world", &m.shadowCatcher); tip("An invisible floor that only shows shadows: they fall on the printed paper, the table or the floor");
        ImGui::Checkbox("Physics floor", &m.physicsFloor); tip("Things with a Rigidbody land on the real paper, table or floor (y = 0) instead of falling through it");
    }
    if ((proj.tracking == TRACK_MARKERS || proj.tracking == TRACK_IMAGES) && section("Window into the marker")) {
        ImGui::Checkbox("Show what's under the paper only through an opening", &m.portal);
        tip("For scenes that go down into the marker, like the art gallery: the opening is a hole in the table.");
        if (m.portal) { labelled("Opening"); ImGui::DragFloat2("##ps", m.portalSize, 0.01f, 0.1f, 10, "%.2f"); }
    }
}

// a component's header, Unity's way: open/close, its tick (when it has one), its name, and a menu (Reset, Remove)
enum { C_KEEP, C_RESET, C_REMOVE, C_UP, C_DOWN };
// Unity's component clipboard: Copy Component, Paste Component Values (onto the same kind), Paste Component As New
static Object* hdrObj = nullptr;            // the object whose Inspector is being drawn (nullptr: the Main Camera)
static const char* hdrKey = nullptr;        // this header's component, when its title says something else (the look, a script)
static int hdrScript = -1;                  // a script header: which of its scripts
static struct { std::string key; Object snap; std::string script; } compClip;
template <class T, size_t N> static void cpa(T (&a)[N], const T (&b)[N]) { for (size_t i = 0; i < N; ++i) a[i] = b[i]; }
// the fields of one component, from one object onto another
static void copyComponent(const std::string& key, const Object& f, Object& t) {
    if (key == "Transform") { cpa(t.pos, f.pos); cpa(t.rot, f.rot); cpa(t.scl, f.scl); }
    else if (key == "Look") {
        t.kind = f.kind; t.lookOn = f.lookOn; t.file = f.file; cpa(t.colour, f.colour); t.lit = f.lit; t.glow = f.glow; t.billboard = f.billboard;
        t.metallic = f.metallic; t.roughness = f.roughness; cpa(t.emissive, f.emissive); t.emissiveStrength = f.emissiveStrength;
        t.castShadows = f.castShadows; t.receiveShadows = f.receiveShadows; t.occluder = f.occluder;
        t.count = f.count; t.spriteSize = f.spriteSize; t.flap = f.flap; t.clip = f.clip; t.clipSpeed = f.clipSpeed; t.clipLoop = f.clipLoop; t.rays = f.rays; t.text = f.text;
    } else if (key == "Animate") { t.hasAnim = true; t.animOn = f.animOn; t.anim = f.anim; t.animSpeed = f.animSpeed; t.animAmount = f.animAmount; }
    else if (key == "Canvas") { t.hasCanvas = true; t.canvasWithAnchor = f.canvasWithAnchor; t.canvasCss = f.canvasCss; }
    else if (key == "UI") {
        t.ui = f.ui; t.uiOn = f.uiOn; t.uiAnchor = f.uiAnchor; cpa(t.uiRect, f.uiRect); t.uiText = f.uiText; t.uiFont = f.uiFont; t.uiAlign = f.uiAlign;
        cpa(t.uiColour, f.uiColour); cpa(t.uiBg, f.uiBg); t.uiMin = f.uiMin; t.uiMax = f.uiMax; t.uiValue = f.uiValue; t.uiChecked = f.uiChecked;
        t.uiFile = f.uiFile; t.uiFontFile = f.uiFontFile; t.uiClass = f.uiClass; t.uiOnClick = f.uiOnClick;
    } else if (key == "Light") {
        t.hasLight = true; t.lightOn = f.lightOn; t.lightType = f.lightType; cpa(t.lightColour, f.lightColour); t.lightIntensity = f.lightIntensity;
        t.lightRange = f.lightRange; t.spotAngle = f.spotAngle; t.lightShadows = f.lightShadows;
    } else if (key == "Audio Source") {
        t.hasAudio = true; t.audioOn = f.audioOn; t.audioFile = f.audioFile; t.volume = f.volume; t.audioLoop = f.audioLoop; t.playOnStart = f.playOnStart;
        t.spatial = f.spatial; t.audioNear = f.audioNear; t.audioFar = f.audioFar;
    } else if (key == "Particle System") {
        t.hasParticles = true; t.particlesOn = f.particlesOn; t.particleFile = f.particleFile; t.pRate = f.pRate; t.pLife = f.pLife; t.pSpeed = f.pSpeed;
        t.pSpread = f.pSpread; t.pSizeStart = f.pSizeStart; t.pSizeEnd = f.pSizeEnd; t.pGravity = f.pGravity; cpa(t.pColourStart, f.pColourStart);
        cpa(t.pColourEnd, f.pColourEnd); t.pGlow = f.pGlow;
    } else if (key == "Line Renderer") { t.hasLine = true; t.lineOn = f.lineOn; t.lineLoop = f.lineLoop; t.linePoints = f.linePoints; t.lineWidth = f.lineWidth; cpa(t.lineColour, f.lineColour); }
    else if (key == "AR Path") {
        t.hasPath = true; t.pathOn = f.pathOn; t.pathLoop = f.pathLoop; t.pathLine = f.pathLine; t.pathName = f.pathName; t.pathPoints = f.pathPoints;
        t.arrowSpacing = f.arrowSpacing; t.arrowSize = f.arrowSize; cpa(t.pathColour, f.pathColour);
    } else if (key == "AR Path Follower") { t.hasFollower = true; t.followerOn = f.followerOn; t.followLoop = f.followLoop; t.followOnStart = f.followOnStart; t.followPath = f.followPath; t.followSpeed = f.followSpeed; }
    else if (key == "Video Player") {
        t.hasVideo = true; t.videoOn = f.videoOn; t.videoFile = f.videoFile; t.videoLoop = f.videoLoop; t.videoPlayOnStart = f.videoPlayOnStart; t.videoMuted = f.videoMuted;
        t.videoVolume = f.videoVolume; t.videoSpeed = f.videoSpeed; t.videoAlpha = f.videoAlpha; cpa(t.videoKey, f.videoKey); t.videoKeyTol = f.videoKeyTol; t.videoKeySoft = f.videoKeySoft;
    } else if (key == "Trail Renderer") { t.hasTrail = true; t.trailOn = f.trailOn; t.trailTime = f.trailTime; t.trailWidth = f.trailWidth; cpa(t.trailColour, f.trailColour); }
    else if (key == "Rigidbody") { t.hasBody = true; t.mass = f.mass; t.drag = f.drag; t.angularDrag = f.angularDrag; t.useGravity = f.useGravity; t.isKinematic = f.isKinematic; t.freeze = f.freeze; }
    else if (key == "Collider") {
        t.hasCollider = true; t.colliderOn = f.colliderOn; t.isTrigger = f.isTrigger; t.colShape = f.colShape; cpa(t.colCentre, f.colCentre); cpa(t.colSize, f.colSize);
        t.colRadius = f.colRadius; t.colHeight = f.colHeight; t.friction = f.friction; t.bounciness = f.bounciness;
    }
}
static bool hasComponent(const std::string& key, const Object& o) {
    if (key == "Transform") return true; if (key == "Look") return o.kind != EMPTY; if (key == "Animate") return o.hasAnim; if (key == "Canvas") return o.hasCanvas;
    if (key == "UI") return o.ui != UI_NONE; if (key == "Light") return o.hasLight; if (key == "Audio Source") return o.hasAudio; if (key == "Particle System") return o.hasParticles;
    if (key == "Line Renderer") return o.hasLine; if (key == "AR Path") return o.hasPath; if (key == "AR Path Follower") return o.hasFollower;
    if (key == "Video Player") return o.hasVideo; if (key == "Trail Renderer") return o.hasTrail;
    if (key == "Rigidbody") return o.hasBody; if (key == "Collider") return o.hasCollider;
    return false;
}
static void pasteAsNew(Object& t) {   // the copied component onto this object (one of a kind: its values, if it has one already)
    if (compClip.key.empty()) return;
    beginEdit();
    if (compClip.key == "Script") { t.scripts.push_back({compClip.script, true}); say("Pasted the script " + fs::u8path(compClip.script).filename().u8string()); return; }
    const bool had = hasComponent(compClip.key, t);
    copyComponent(compClip.key, compClip.snap, t);
    say(had ? t.name + " has a " + compClip.key + " already: its values were pasted" : "Pasted " + compClip.key + " onto " + t.name);
}
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
        if (hdrKey && !strcmp(hdrKey, "Script") && hdrObj) {   // scripts run in their order: Move Up / Move Down
            if (ImGui::MenuItem("Move Up", nullptr, false, hdrScript > 0)) act = C_UP;
            if (ImGui::MenuItem("Move Down", nullptr, false, hdrScript + 1 < (int)hdrObj->scripts.size())) act = C_DOWN;
        }
        if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Every object has a Transform");
        ImGui::Separator();
        const std::string key = hdrKey ? hdrKey : std::string(name).rfind("UI ", 0) == 0 ? "UI" : name;
        if (ImGui::MenuItem("Copy Component", nullptr, false, hdrObj != nullptr) && hdrObj) {
            compClip.key = key; compClip.snap = *hdrObj;
            compClip.script = key == "Script" && hdrScript >= 0 && hdrScript < (int)hdrObj->scripts.size() ? hdrObj->scripts[hdrScript].file : "";
            say("Copied " + key + (compClip.script.empty() ? "" : " (" + fs::u8path(compClip.script).filename().u8string() + ")"));
        }
        const bool sameKind = hdrObj && compClip.key == key && key != "Script";
        if (ImGui::MenuItem("Paste Component Values", nullptr, false, sameKind) && sameKind) {
            beginEdit(); copyComponent(key, compClip.snap, *hdrObj); say("Pasted the " + key + "'s values");
        }
        if (!sameKind && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(compClip.key.empty() ? "Copy a component first" : "The copied one is a %s: values go onto the same kind", compClip.key.c_str());
        std::string asNew = compClip.key.empty() ? "Paste Component As New" : "Paste " + compClip.key + " As New";
        if (ImGui::MenuItem(asNew.c_str(), nullptr, false, hdrObj && !compClip.key.empty()) && hdrObj) pasteAsNew(*hdrObj);
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
    if (kind == PICTURE || kind == SPRITES) { file = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0"); if (file.empty()) return false; file = bring(file); }
    if (kind == MODEL) { file = openDialog(L"3D models (glTF)\0*.glb;*.gltf\0\0"); if (file.empty()) return false; file = bring(file); }
    o.kind = kind; o.lookOn = true; o.file = file;
    memcpy(o.colour, fresh.colour, sizeof o.colour); o.lit = fresh.lit; o.glow = fresh.glow; o.billboard = fresh.billboard;
    if (kind == MODEL) { const auto c = Viewer::clipsOf(o.file); o.clip = c.empty() ? "" : c[0]; }
    return true;
}
static std::string scriptsDir() {
    const fs::path d = folder.empty() ? fs::u8path(projectsHome()) / "scripts" : fs::u8path(folder) / "assets" / "scripts";
    std::error_code ec; fs::create_directories(d, ec);
    return d.u8string();
}
static void newScript(Object& o) {
    std::string base = o.name; for (auto& c : base) if (!isalnum((unsigned char)c)) c = '_';
    fs::path f = fs::u8path(scriptsDir()) / (base + ".js");
    for (int k = 2; fs::exists(f); ++k) f = fs::u8path(scriptsDir()) / (base + std::to_string(k) + ".js");
    std::ofstream(f, std::ios::binary) <<
        "// " << o.name << ": a behaviour on this object. It runs in Play (Ctrl+P) and in the built game.\n"
        "//\n"
        "// this (or gameObject): the object. this.position / this.rotation / this.scale have .x .y .z (rotation in degrees),\n"
        "//   this.active, this.velocity (moves it each frame), this.name, this.getComponent(\"Animate\" | \"Material\" |\n"
        "//   \"Words\" | \"Model\" | another script's name), this.toCamera() (direction to the phone, and .distance),\n"
        "//   this.lookAtCamera().\n"
        "// find(\"Name\" or \"Parent/Child\"), spawn(\"Name\" or a prefab, { position }), destroy(obj), time(), log(...),\n"
        "// camera.position, input.tapped, loadScene(\"Name\" or a number), scene.name / scene.index.\n"
        "// UI elements: onClick() (Button), onChange(value) (Slider, Toggle).\n\n"
        "function start() {\n  // once, before its first update\n}\n\n"
        "function update(dt) {\n  // every frame; dt: seconds since the last one\n}\n\n"
        "function onTap() {\n  // the player tapped it\n}\n\n"
        "function onFound() {\n  // its anchor (marker, picture or face) was found\n}\n\n"
        "function onLost() {\n  // its anchor was lost\n}\n";
    openScript(f.u8string());
    o.scripts.push_back({f.u8string(), true});
}
static const wchar_t* VIDEOS = L"Videos (with alpha too: MOV, WebM, GIF, APNG)\0*.mp4;*.webm;*.m4v;*.mov;*.mkv;*.avi;*.gif;*.apng\0\0";
// a tool's output (ffprobe), waited for
static std::string runCapture(const std::wstring& cmd, DWORD ms) {
    std::string out;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return out;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(wr); wr = nullptr;
        char b[4096]; DWORD n = 0;
        while (ReadFile(rd, b, sizeof b, &n, nullptr) && n) out.append(b, n);
        WaitForSingleObject(pi.hProcess, ms); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    if (wr) CloseHandle(wr);
    CloseHandle(rd);
#else
    (void)cmd; (void)ms;
#endif
    return out;
}
// does this video have an alpha channel (like a PNG's)? ffprobe: an alpha pixel format, WebM's alpha_mode, or a GIF
static bool videoHasAlpha(const std::string& file) {
    std::string ext = fs::u8path(file).extension().u8string(); for (auto& c : ext) c = (char)tolower((unsigned char)c);
    const std::string r = runCapture(L"ffprobe -v error -select_streams v:0 -show_entries stream=pix_fmt,codec_name:stream_tags=alpha_mode -of default=nw=1 \"" + fs::u8path(file).wstring() + L"\"", 20000);
    if (r.find("alpha_mode=1") != std::string::npos) return true;
    const size_t at = r.find("pix_fmt="); if (at == std::string::npos) return ext == ".gif" || ext == ".apng";
    const std::string pf = r.substr(at + 8, r.find_first_of("\r\n", at) - at - 8);
    for (const char* a : {"rgba", "bgra", "argb", "abgr", "yuva", "gbrap", "ya8", "ya16", "rgba64", "bgra64"}) if (pf.find(a) == 0) return true;
    return ext == ".gif" && pf == "pal8";
}
// a video with alpha the browsers can't play (MOV ProRes 4444 / PNG / Animation, GIF, APNG...) as WebM VP9 with alpha, once
static std::string toWebmAlpha(const std::string& file) {
    std::string ext = fs::u8path(file).extension().u8string(); for (auto& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == ".webm") return file;
    const fs::path out = fs::u8path(file).parent_path() / fs::u8path(fs::u8path(file).stem().u8string() + ".alpha.webm");
    std::error_code ec;
    if (fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(fs::u8path(file), ec)) return out.u8string();
    const std::wstring cmd = L"ffmpeg -nostdin -loglevel error -y -i \"" + fs::u8path(file).wstring() + L"\" -c:v libvpx-vp9 -pix_fmt yuva420p -b:v 0 -crf 28 -row-mt 1 "
                             L"-auto-alt-ref 0 -c:a libopus \"" + out.wstring() + L"\"";
    runCapture(cmd, 600000);
    return fs::exists(out, ec) && fs::file_size(out, ec) > 0 ? out.u8string() : file;
}
// a video chosen for a Video Player: alpha found and turned on (and made playable), its preview frame
static void useVideo(Object& o, const std::string& chosen) {
    std::string file = chosen;
    if (videoHasAlpha(file)) {
        const std::string web = toWebmAlpha(file);
        if (web != file) say("It has alpha: made " + fs::u8path(web).filename().u8string() + " (WebM with alpha) for the game");
        file = web; o.videoAlpha = 1;
    } else if (o.videoAlpha == 1) o.videoAlpha = 0;
    o.videoFile = file;
}
// the first frame of a video, beside it, for the editor's view (ffmpeg, when it's there; the game plays the real thing)
static std::string posterOf(const std::string& video) { return video + ".poster.png"; }
static void videoPoster(const std::string& video, const Object* o = nullptr, bool again = false) {
    if (video.empty()) return;
    const fs::path out = fs::u8path(posterOf(video));
    std::error_code ec; if (!again && fs::exists(out, ec)) return;
    std::string ext = fs::u8path(video).extension().u8string(); for (auto& c : ext) c = (char)tolower((unsigned char)c);
    // WebM's alpha only comes out through libvpx's decoder; a green screen is keyed out by ffmpeg's colorkey
    const std::wstring decoder = ext == ".webm" ? L"-c:v libvpx-vp9 " : L"";
    std::wstring vf = L"scale=640:-2";
    if (o && o->videoAlpha == 2) {
        char key[96]; snprintf(key, sizeof key, "colorkey=0x%02X%02X%02X:%.3f:%.3f,", (int)(o->videoKey[0] * 255), (int)(o->videoKey[1] * 255), (int)(o->videoKey[2] * 255), o->videoKeyTol, std::max(0.001f, o->videoKeySoft));
        vf = fs::u8path(key).wstring() + vf;
    }
    vf += L",format=rgba";
    std::wstring cmd = L"ffmpeg -nostdin -loglevel error -y -ss 0.5 " + decoder + L"-i \"" + fs::u8path(video).wstring() + L"\" -frames:v 1 -vf \"" + vf + L"\" \"" + out.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    if (fs::exists(out, ec)) {
        // WebM made without alpha (or a decoder that drops it): libvpx refuses some files, so once more plainly
        Viewer::reload(posterOf(video));
    } else if (!decoder.empty()) {
        std::wstring plain = L"ffmpeg -nostdin -loglevel error -y -ss 0.5 -i \"" + fs::u8path(video).wstring() + L"\" -frames:v 1 -vf \"" + vf + L"\" \"" + out.wstring() + L"\"";
        std::vector<wchar_t> b2(plain.begin(), plain.end()); b2.push_back(0);
        STARTUPINFOW s2{}; s2.cb = sizeof s2; PROCESS_INFORMATION p2{};
        if (CreateProcessW(nullptr, b2.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &s2, &p2)) { WaitForSingleObject(p2.hProcess, 15000); CloseHandle(p2.hProcess); CloseHandle(p2.hThread); }
        Viewer::reload(posterOf(video));
    }
}
// a Video Player, and a screen to play on when the object has no look yet (a Quad the video's shape)
static void giveVideo(Object& o, const std::string& file) {
    o.hasVideo = true; o.videoOn = true;
    useVideo(o, file);
    videoPoster(o.videoFile, &o, true);
    if (o.kind == EMPTY) {
        giveLook(o, QUAD);
        int w = 0, h = 0, n = 0;
        if (stbi_info(posterOf(o.videoFile).c_str(), &w, &h, &n) && w > 0) o.scl[1] = o.scl[0] * h / w;
        o.colour[0] = o.colour[1] = o.colour[2] = o.colour[3] = 1; o.lit = false;
    }
}
// a collider fitted to the object's look (a model: its bounds, from the file)
static void addCollider(Object& o, int shape) {
    o.hasCollider = true; o.colliderOn = true;
    Model::Model md;
    if (o.kind == MODEL && !o.file.empty() && Model::load(o.file, md)) fitCollider(o, shape, md.lo, md.hi);
    else fitCollider(o, shape);
}
static int defaultCollider(const Object& o) {   // what Unity gives each primitive
    return o.kind == SPHERE ? COL_SPHERE : o.kind == CAPSULE || o.kind == CYLINDER ? COL_CAPSULE : COL_BOX;
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
    if (ImGui::BeginMenu("Rendering")) {
        if (ImGui::MenuItem("Light", nullptr, false, !o.hasLight)) { o.hasLight = true; o.lightOn = true; }
        if (ImGui::MenuItem("Line Renderer", nullptr, false, !o.hasLine)) { o.hasLine = true; o.lineOn = true; }
        if (ImGui::MenuItem("Trail Renderer", nullptr, false, !o.hasTrail)) { o.hasTrail = true; o.trailOn = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Physics")) {
        if (ImGui::MenuItem("Rigidbody", nullptr, false, !o.hasBody)) {
            o.hasBody = true;
            if (!o.hasCollider) { addCollider(o, defaultCollider(o)); say(std::string("Added a Rigidbody, and a ") + COLLIDER_NAMES[o.colShape] + " to give it a shape"); }
        }
        ImGui::Separator();
        for (int k = 0; k < COL_COUNT; ++k) if (ImGui::MenuItem(COLLIDER_NAMES[k], nullptr, false, !o.hasCollider)) addCollider(o, k);
        if (o.hasCollider) ImGui::TextDisabled("(it has a collider: change its shape in the Inspector)");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("XR")) {
        if (ImGui::MenuItem("AR Path", nullptr, false, !o.hasPath)) { o.hasPath = true; o.pathOn = true; }
        if (ImGui::MenuItem("AR Path Follower", nullptr, false, !o.hasFollower)) { o.hasFollower = true; o.followerOn = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Behaviours")) {   // Diorama's own scripts (assets/behaviours): copied into the project, then edit them as any script
        std::error_code be;
        for (const auto& e : fs::directory_iterator(fs::u8path(exeDir) / "assets" / "behaviours", be)) {
            if (e.path().extension() != ".js") continue;
            if (ImGui::MenuItem(e.path().stem().u8string().c_str())) {
                const fs::path dir = folder.empty() ? fs::u8path(projectsHome()) / "scripts" : fs::u8path(folder) / "assets" / "behaviours";
                std::error_code ce; fs::create_directories(dir, ce);
                const fs::path to = dir / e.path().filename();
                if (!fs::exists(to, ce)) fs::copy_file(e.path(), to, ce);
                beginEdit(); o.scripts.push_back({to.u8string(), true});
                say("Added " + e.path().stem().u8string() + ": its settings are at the top of the script (Edit Script)");
            }
            if (ImGui::IsItemHovered()) {   // its first line says what it does
                std::ifstream in(e.path()); std::string line; std::getline(in, line);
                if (line.rfind("// ", 0) == 0) ImGui::SetTooltip("%s", line.substr(3).c_str());
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effects")) {
        if (ImGui::MenuItem("Particle System", nullptr, false, !o.hasParticles)) { o.hasParticles = true; o.particlesOn = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Audio Source...", nullptr, false, !o.hasAudio)) {
            const std::string f = openDialog(L"Sounds\0*.mp3;*.ogg;*.wav;*.m4a\0\0");
            if (!f.empty()) { o.hasAudio = true; o.audioOn = true; o.audioFile = bring(f); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Video")) {
        if (ImGui::MenuItem("Video Player...", nullptr, false, !o.hasVideo)) {
            const std::string f = openDialog(VIDEOS);
            if (!f.empty()) giveVideo(o, bring(f));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas", nullptr, false, !o.hasCanvas && o.ui == UI_NONE)) o.hasCanvas = true;
        for (int k = 1; k < UI_COUNT; ++k) if (ImGui::MenuItem(UI_NAMES[k], nullptr, false, o.ui == UI_NONE && !o.hasCanvas)) {
            o.ui = k; o.uiText = k == UI_TEXT ? "New Text" : UI_NAMES[k];
            if (k == UI_IMAGE) { const std::string f = openDialog(L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0"); if (!f.empty()) o.uiFile = bring(f); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Script")) {
        if (ImGui::MenuItem("New Script")) newScript(o);
        if (ImGui::MenuItem("Existing Script...")) { const std::string f = openDialog(L"JavaScript\0*.js\0\0"); if (!f.empty()) o.scripts.push_back({bring(f), true}); }
        ImGui::EndMenu();
    }
}

// a material's controls (an object's, or a material asset's); true when something changed
static bool materialFields(Object& o, bool texture) {
    const Object before = o;
    const wchar_t* PICS = L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0";
    int shader = o.lit ? 0 : 1; const char* shaders[] = {"Standard (lit, PBR)", "Unlit (its own colours)"};
    labelled("Shader"); if (ImGui::Combo("##shader", &shader, shaders, 2)) o.lit = shader == 0;
    labelled("Colour"); ImGui::ColorEdit4("##col", o.colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    if (texture) fileRow("Texture", o.file, PICS);
    ImGui::Checkbox("Glow", &o.glow); tip("Added on as light (god rays, sparks)");
    if (o.lit && !o.glow) {   // PBR: how it takes the light
        labelled("Metallic"); ImGui::SliderFloat("##met", &o.metallic, 0, 1, "%.2f"); tip("0: paint, plastic, wood; 1: metal (it takes the lights' colour)");
        labelled("Roughness"); ImGui::SliderFloat("##rough", &o.roughness, 0.02f, 1, "%.2f"); tip("0: a mirror shine; 1: chalk");
    }
    labelled("Emissive"); ImGui::ColorEdit3("##emc", o.emissive, ImGuiColorEditFlags_NoInputs); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##ems", &o.emissiveStrength, 0, 10, "light %.2f"); tip("Light of its own (it glows; bloom makes it bleed)");
    ImGui::Checkbox("Casts shadows", &o.castShadows); ImGui::SameLine(); ImGui::Checkbox("Gets shadows", &o.receiveShadows);
    return memcmp(before.colour, o.colour, sizeof o.colour) || memcmp(before.emissive, o.emissive, sizeof o.emissive) || before.lit != o.lit || before.glow != o.glow ||
           before.metallic != o.metallic || before.roughness != o.roughness || before.emissiveStrength != o.emissiveStrength || before.file != o.file ||
           before.castShadows != o.castShadows || before.receiveShadows != o.receiveShadows;
}
static void objectInspector(Scene::Marker& m, Object& o) {
    hdrObj = &o; hdrKey = nullptr; hdrScript = -1;
    ImGui::Checkbox("##active", &o.active); tip("Active: off hides it and everything under it");
    ImGui::SameLine();
    char name[96]; snprintf(name, sizeof name, "%s", o.name.c_str());
    ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##name", name, sizeof name)) o.name = name;
    // Transform: every object has one
    int h = componentHeader("Transform", nullptr, false);
    if ((h & 0xff) == C_RESET) for (int k = 0; k < 3; ++k) { o.pos[k] = 0; o.rot[k] = 0; o.scl[k] = 1; }
    if (h & 0x100) { vec3Row("Position", o.pos, 0.005f); vec3Row("Rotation", o.rot, 0.5f); vec3Row("Scale", o.scl, 0.005f); }

    const wchar_t* PICS = L"Pictures\0*.png;*.jpg;*.jpeg;*.jfif\0\0";
    // the look, with its material
    if (o.kind != EMPTY) {
        const std::string title = isPrimitive(o.kind) ? std::string("Mesh: ") + KIND_NAMES[o.kind] : KIND_NAMES[o.kind];
        hdrKey = "Look"; h = componentHeader(title.c_str(), &o.lookOn, true); hdrKey = nullptr;
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
                // its material: an asset (shared) or its own
                ImGui::SeparatorText("Material");
                labelled("Asset");
                if (ImGui::BeginCombo("##matasset", o.material.empty() ? "(its own)" : fs::u8path(o.material).stem().u8string().c_str())) {
                    if (ImGui::Selectable("(its own)", o.material.empty())) o.material.clear();
                    std::error_code me;
                    if (!folder.empty()) for (const auto& e : fs::recursive_directory_iterator(fs::u8path(folder) / "assets", me))
                        if (e.path().extension() == ".material" && ImGui::Selectable(e.path().stem().u8string().c_str(), o.material == e.path().u8string())) {
                            Object mat; if (loadMaterial(e.path().u8string(), mat)) { o.material = e.path().u8string(); applyMaterial(mat, o); }
                        }
                    ImGui::EndCombo();
                }
                tip("A material asset (Project window: right-click > Create > Material) is shared: change it here or in its own\nInspector and every object with it changes. Drop one onto an object to give it to that object.");
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, selMarker, o.id, std::string((const char*)pl->Data));
                    ImGui::EndDragDropTarget();
                }
                if (materialFields(o, isPrimitive(o.kind)) && !o.material.empty()) { saveMaterial(o.material, o); applyMaterialEverywhere(o.material); }
                if (o.kind == QUAD || o.kind == PICTURE || o.kind == WORDS) ImGui::Checkbox("Faces the phone", &o.billboard);
                ImGui::Checkbox("Occluder", &o.occluder); tip("Invisible in the game, but hides what's behind it:\na stand-in for a real thing (a box, a wall) so the AR goes behind it");
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
    // Canvas
    if (o.hasCanvas) {
        h = componentHeader("Canvas", nullptr, true);
        if ((h & 0xff) == C_REMOVE) o.hasCanvas = false;
        else if (h & 0x100) {
            ImGui::Checkbox("Only while its anchor is found", &o.canvasWithAnchor);
            tip("Off: always on the screen (menus, scores). On: shown while its marker, picture or face is seen");
            fileRow("Style sheet", o.canvasCss, L"CSS style sheet\0*.css\0\0");
            tip("Your own CSS for the game's UI: elements have classes dio-ui, dio-text, dio-button, dio-slider,\ndio-toggle, dio-image, dio-panel, and the classes you give them");
            ImGui::TextDisabled("The game's UI, laid over the camera as HTML.\nIts elements (GameObject > UI) go under it.");
        }
    }
    // a UI element
    if (o.ui != UI_NONE) {
        h = componentHeader((std::string("UI ") + UI_NAMES[o.ui]).c_str(), &o.uiOn, true);
        if ((h & 0xff) == C_REMOVE) o.ui = UI_NONE;
        else if (h & 0x100) {
            // the anchor: a 3 x 3 of buttons, like Unity's anchor presets
            labelled("Anchor");
            ImGui::BeginGroup();
            for (int k = 0; k < 9; ++k) {
                if (k % 3) ImGui::SameLine(0, 2);
                ImGui::PushID(500 + k);
                const bool on = o.uiAnchor == k;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
                if (ImGui::Button(on ? "\xe2\x97\x8f" : "", ImVec2(22, 16))) {   // keep it where it is on the screen
                    const ImVec4 pr(0, 0, UI_W, UI_H);
                    const ImVec4 r = uiRectIn(o, pr, 1);
                    const int ax = k % 3, ay = k / 3;
                    o.uiAnchor = k;
                    o.uiRect[0] = r.x + o.uiRect[2] * ax * 0.5f - UI_W * ax * 0.5f; o.uiRect[1] = r.y + o.uiRect[3] * ay * 0.5f - UI_H * ay * 0.5f;
                }
                if (on) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndGroup();
            labelled("X, Y"); ImGui::DragFloat2("##uixy", o.uiRect, 0.5f, -2000, 2000, "%.0f");
            labelled("Width, Height"); ImGui::DragFloat2("##uiwh", o.uiRect + 2, 0.5f, 0, 4000, "%.0f");
            if (o.ui == UI_TEXT || o.ui == UI_BUTTON || o.ui == UI_TOGGLE) {
                char t[512]; snprintf(t, sizeof t, "%s", o.uiText.c_str());
                labelled("Text"); if (ImGui::InputText("##uit", t, sizeof t)) o.uiText = t;
                fileRow("Font", o.uiFontFile, L"Fonts\0*.ttf;*.otf\0\0");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A .ttf or .otf font, as Unity's Font: it's copied into the project and goes into the builds");
                labelled("Size"); ImGui::SliderFloat("##uif", &o.uiFont, 6, 96, "%.0f");
                if (o.ui == UI_TEXT) { const char* al[] = {"Left", "Centre", "Right"}; labelled("Align"); ImGui::Combo("##uia", &o.uiAlign, al, 3); }
                labelled("Text colour"); ImGui::ColorEdit4("##uic", o.uiColour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            }
            if (o.ui == UI_IMAGE) {
                fileRow("Picture", o.uiFile, PICS);
                labelled("Tint"); ImGui::ColorEdit4("##uic", o.uiColour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            }
            if (o.ui == UI_BUTTON || o.ui == UI_PANEL || o.ui == UI_SLIDER || o.ui == UI_TOGGLE) {
                labelled(o.ui == UI_SLIDER ? "Fill" : o.ui == UI_TOGGLE ? "Tick box" : "Background"); ImGui::ColorEdit4("##uib", o.uiBg, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            }
            if (o.ui == UI_SLIDER) {
                labelled("Min, Max"); ImGui::DragFloat2("##uimm", &o.uiMin, 0.01f);
                labelled("Value"); ImGui::SliderFloat("##uiv", &o.uiValue, o.uiMin, o.uiMax);
            }
            if (o.ui == UI_TOGGLE) ImGui::Checkbox("On", &o.uiChecked);
            if (o.ui == UI_BUTTON) {   // Unity's On Click: what it does, as a line of script
                char oc[512]; snprintf(oc, sizeof oc, "%s", o.uiOnClick.c_str());
                labelled("On Click"); if (ImGui::InputTextWithHint("##uioc", "nav.follow('Path 1')", oc, sizeof oc)) o.uiOnClick = oc;
                tip("A line of script run on a tap: nav (paths), find(\"Name\"), loadScene(...), log(...), button (this one)");
            }
            char cls[128]; snprintf(cls, sizeof cls, "%s", o.uiClass.c_str());
            labelled("CSS class"); if (ImGui::InputText("##uicl", cls, sizeof cls)) o.uiClass = cls;
            const char* hint = o.ui == UI_BUTTON ? "Scripts on it get onClick(); others: getComponent(\"Button\").onClick(fn)"
                             : o.ui == UI_SLIDER ? "Scripts on it get onChange(value); getComponent(\"Slider\").value"
                             : o.ui == UI_TOGGLE ? "Scripts on it get onChange(on); getComponent(\"Toggle\").isOn"
                             : o.ui == UI_TEXT ? "getComponent(\"Text\").text = \"Score: \" + score" : "";
            if (*hint) ImGui::TextDisabled("%s", hint);
            if (canvasOf(m, o.id) < 0) ImGui::TextColored(col4(IM_COL32(0xf0, 0xb0, 0x60, 255)), "Put it under a Canvas to show it.");
        }
    }
    const Object D;   // defaults, for Reset
    // Light
    if (o.hasLight) {
        h = componentHeader("Light", &o.lightOn, true);
        if ((h & 0xff) == C_REMOVE) o.hasLight = false;
        else {
            if ((h & 0xff) == C_RESET) { o.lightType = D.lightType; memcpy(o.lightColour, D.lightColour, sizeof o.lightColour); o.lightIntensity = D.lightIntensity; o.lightRange = D.lightRange; o.spotAngle = D.spotAngle; }
            if (h & 0x100) {
                const char* types[] = {"Point", "Spot", "Directional"};
                labelled("Type"); ImGui::Combo("##lt", &o.lightType, types, 3);
                labelled("Colour"); ImGui::ColorEdit3("##lc", o.lightColour, ImGuiColorEditFlags_NoInputs);
                labelled("Intensity"); ImGui::SliderFloat("##li", &o.lightIntensity, 0, 5, "%.2f");
                if (o.lightType != 2) { labelled("Range"); ImGui::SliderFloat("##lr", &o.lightRange, 0.05f, 10, "%.2f"); }
                if (o.lightType == 1) { labelled("Spot angle"); ImGui::SliderFloat("##sa", &o.spotAngle, 1, 170, "%.0f\xc2\xb0"); }
                if (o.lightType != 0) { ImGui::Checkbox("Casts shadows", &o.lightShadows); ImGui::TextDisabled("It shines along the object's forward (-Z)."); }
                ImGui::TextDisabled("Lights the Lit things on its anchor (up to 16 lights).");
            }
        }
    }
    // Audio Source
    if (o.hasAudio) {
        h = componentHeader("Audio Source", &o.audioOn, true);
        if ((h & 0xff) == C_REMOVE) { o.hasAudio = false; o.audioFile.clear(); }
        else {
            if ((h & 0xff) == C_RESET) { o.volume = D.volume; o.audioLoop = D.audioLoop; o.playOnStart = D.playOnStart; o.spatial = D.spatial; o.audioNear = D.audioNear; o.audioFar = D.audioFar; }
            if (h & 0x100) {
                fileRow("Sound", o.audioFile, L"Sounds\0*.mp3;*.ogg;*.wav;*.m4a\0\0");
                labelled("Volume"); ImGui::SliderFloat("##vol", &o.volume, 0, 1, "%.2f");
                ImGui::Checkbox("Play on start", &o.playOnStart); tip("Starts when its anchor is first found;\nscripts can play() and stop() it too");
                ImGui::SameLine(); ImGui::Checkbox("Loop", &o.audioLoop);
                ImGui::Checkbox("3D sound", &o.spatial); tip("Quieter further from the phone, and comes from where it is");
                if (o.spatial) {
                    labelled("Full volume"); ImGui::SliderFloat("##an", &o.audioNear, 0.05f, 5, "%.2f"); tip("Closer than this: full volume");
                    labelled("Silent at"); ImGui::SliderFloat("##af", &o.audioFar, o.audioNear + 0.1f, 30, "%.2f");
                }
                ImGui::TextDisabled("Phones only play sound after the first tap.");
            }
        }
    }
    // Particle System
    if (o.hasParticles) {
        h = componentHeader("Particle System", &o.particlesOn, true);
        if ((h & 0xff) == C_REMOVE) { o.hasParticles = false; o.particleFile.clear(); }
        else {
            if ((h & 0xff) == C_RESET) { o.pRate = D.pRate; o.pLife = D.pLife; o.pSpeed = D.pSpeed; o.pSpread = D.pSpread; o.pSizeStart = D.pSizeStart; o.pSizeEnd = D.pSizeEnd; o.pGravity = D.pGravity;
                memcpy(o.pColourStart, D.pColourStart, sizeof o.pColourStart); memcpy(o.pColourEnd, D.pColourEnd, sizeof o.pColourEnd); o.pGlow = D.pGlow; o.particleFile.clear(); }
            if (h & 0x100) {
                fileRow("Picture", o.particleFile, PICS); tip("Empty: a soft dot");
                labelled("Per second"); ImGui::SliderFloat("##pr", &o.pRate, 0.5f, 200, "%.1f");
                labelled("Lifetime"); ImGui::SliderFloat("##pl", &o.pLife, 0.1f, 10, "%.2f s");
                labelled("Speed"); ImGui::SliderFloat("##ps", &o.pSpeed, 0, 5, "%.2f");
                labelled("Spread"); ImGui::SliderFloat("##pd", &o.pSpread, 0, 180, "%.0f\xc2\xb0"); tip("The cone they leave in, round the object's up (+Y)");
                labelled("Gravity"); ImGui::SliderFloat("##pg", &o.pGravity, -3, 3, "%.2f");
                labelled("Size start"); ImGui::SliderFloat("##pss", &o.pSizeStart, 0.001f, 1, "%.3f");
                labelled("Size end"); ImGui::SliderFloat("##pse", &o.pSizeEnd, 0, 1, "%.3f");
                labelled("Colour start"); ImGui::ColorEdit4("##pcs", o.pColourStart, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                labelled("Colour end"); ImGui::ColorEdit4("##pce", o.pColourEnd, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::Checkbox("Glow", &o.pGlow); tip("Added on as light (fire, sparks, magic)");
            }
        }
    }
    // Line Renderer
    if (o.hasLine) {
        h = componentHeader("Line Renderer", &o.lineOn, true);
        if ((h & 0xff) == C_REMOVE) o.hasLine = false;
        else {
            if ((h & 0xff) == C_RESET) { o.linePoints = D.linePoints; o.lineWidth = D.lineWidth; memcpy(o.lineColour, D.lineColour, sizeof o.lineColour); o.lineLoop = D.lineLoop; }
            if (h & 0x100) {
                labelled("Width"); ImGui::SliderFloat("##lw", &o.lineWidth, 0.001f, 0.5f, "%.3f");
                labelled("Colour"); ImGui::ColorEdit4("##lcol", o.lineColour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::Checkbox("Loop", &o.lineLoop); tip("Joins the last point back to the first");
                const int n = (int)o.linePoints.size() / 3;
                int del = -1;
                for (int i = 0; i < n; ++i) {
                    ImGui::PushID(1000 + i);
                    char lab[24]; snprintf(lab, sizeof lab, "Point %d", i + 1);
                    vec3Row(lab, &o.linePoints[i * 3], 0.005f);
                    if (n > 2 && ImGui::BeginPopupContextItem("pt")) { if (ImGui::MenuItem("Remove point")) del = i; ImGui::EndPopup(); }
                    ImGui::PopID();
                }
                if (del >= 0) o.linePoints.erase(o.linePoints.begin() + del * 3, o.linePoints.begin() + del * 3 + 3);
                if (ImGui::Button("Add point", ImVec2(-1, 0))) {
                    const size_t k = o.linePoints.size();
                    float p[3] = {o.linePoints[k - 3], o.linePoints[k - 2] + 0.2f, o.linePoints[k - 1]};
                    o.linePoints.insert(o.linePoints.end(), p, p + 3);
                }
                ImGui::TextDisabled("Points are in the object's space. Right-click a point to remove it.");
            }
        }
    }
    // AR Path (path marking)
    if (o.hasPath) {
        h = componentHeader("AR Path", &o.pathOn, true);
        if ((h & 0xff) == C_REMOVE) o.hasPath = false;
        else {
            if ((h & 0xff) == C_RESET) { o.pathPoints = D.pathPoints; o.arrowSpacing = D.arrowSpacing; o.arrowSize = D.arrowSize; memcpy(o.pathColour, D.pathColour, sizeof o.pathColour); o.pathLoop = D.pathLoop; o.pathLine = D.pathLine; }
            if (h & 0x100) {
                char nm[64]; snprintf(nm, sizeof nm, "%s", o.pathName.c_str());
                labelled("Name"); if (ImGui::InputText("##pn", nm, sizeof nm)) o.pathName = nm;
                tip("Followers follow it by name; a path walked and saved in the game with this name takes its place");
                labelled("Arrows every"); ImGui::SliderFloat("##as", &o.arrowSpacing, 0.1f, 3, "%.2f m");
                labelled("Arrow size"); ImGui::SliderFloat("##az", &o.arrowSize, 0.05f, 1, "%.2f m");
                labelled("Colour"); ImGui::ColorEdit4("##pcol", o.pathColour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                tip("Colour coding: e.g. green for the way, red for where not to go");
                ImGui::Checkbox("Line", &o.pathLine); ImGui::SameLine(); ImGui::Checkbox("Loop", &o.pathLoop);
                const int n = (int)o.pathPoints.size() / 3; int del = -1;
                for (int i = 0; i < n; ++i) {
                    ImGui::PushID(2000 + i);
                    char lab[24]; snprintf(lab, sizeof lab, "Point %d", i + 1);
                    vec3Row(lab, &o.pathPoints[i * 3], 0.01f);
                    if (n > 2 && ImGui::BeginPopupContextItem("pp")) { if (ImGui::MenuItem("Remove point")) del = i; ImGui::EndPopup(); }
                    ImGui::PopID();
                }
                if (del >= 0) o.pathPoints.erase(o.pathPoints.begin() + del * 3, o.pathPoints.begin() + del * 3 + 3);
                if (ImGui::Button("Add point", ImVec2(-1, 0))) { const size_t k = o.pathPoints.size(); float q[3] = {o.pathPoints[k - 3], o.pathPoints[k - 2], o.pathPoints[k - 1] - 0.5f}; o.pathPoints.insert(o.pathPoints.end(), q, q + 3); }
                ImGui::TextDisabled("On the floor, in metres. Scripts: nav.record(\"Path 1\"), nav.save(), nav.follow(...).");
            }
        }
    }
    // AR Path Follower (path following)
    if (o.hasFollower) {
        h = componentHeader("AR Path Follower", &o.followerOn, true);
        if ((h & 0xff) == C_REMOVE) o.hasFollower = false;
        else {
            if ((h & 0xff) == C_RESET) { o.followSpeed = D.followSpeed; o.followLoop = D.followLoop; o.followOnStart = D.followOnStart; }
            if (h & 0x100) {
                labelled("Path");
                if (ImGui::BeginCombo("##fp", o.followPath.c_str())) {
                    for (const auto& q : m.objects) if (q.hasPath && ImGui::Selectable(q.pathName.c_str(), q.pathName == o.followPath)) o.followPath = q.pathName;
                    ImGui::EndCombo();
                }
                labelled("Speed"); ImGui::SliderFloat("##fs", &o.followSpeed, 0.05f, 3, "%.2f m/s");
                ImGui::Checkbox("Walks from the start", &o.followOnStart); ImGui::SameLine(); ImGui::Checkbox("Loop", &o.followLoop);
                ImGui::TextDisabled("It walks the path facing the way it goes (the player follows it).");
            }
        }
    }
    // Video Player
    if (o.hasVideo) {
        h = componentHeader("Video Player", &o.videoOn, true);
        if ((h & 0xff) == C_REMOVE) { o.hasVideo = false; o.videoFile.clear(); }
        else {
            if ((h & 0xff) == C_RESET) { o.videoLoop = D.videoLoop; o.videoPlayOnStart = D.videoPlayOnStart; o.videoMuted = D.videoMuted; o.videoVolume = D.videoVolume; o.videoSpeed = D.videoSpeed; }
            if (h & 0x100) {
                if (fileRow("Video", o.videoFile, VIDEOS)) { useVideo(o, o.videoFile); videoPoster(o.videoFile, &o, true); }
                const char* alphas[] = {"None", "The video's own (WebM with alpha)", "Green screen (a colour keyed out)"};
                labelled("See-through"); if (ImGui::Combo("##va", &o.videoAlpha, alphas, 3)) videoPoster(o.videoFile, &o, true);
                if (o.videoAlpha == 1) ImGui::TextDisabled("WebM (VP8/VP9) with an alpha channel: Chrome, Edge, Android, Windows builds.\nSafari doesn't show WebM's alpha.");
                if (o.videoAlpha == 2) {
                    labelled("Key colour"); ImGui::ColorEdit3("##vk", o.videoKey, ImGuiColorEditFlags_NoInputs);
                    if (ImGui::IsItemDeactivatedAfterEdit()) videoPoster(o.videoFile, &o, true);
                    labelled("Tolerance"); ImGui::SliderFloat("##vkt", &o.videoKeyTol, 0.01f, 1, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) videoPoster(o.videoFile, &o, true);
                    tip("How far from the key colour still goes see-through");
                    labelled("Softness"); ImGui::SliderFloat("##vks", &o.videoKeySoft, 0, 0.5f, "%.2f"); if (ImGui::IsItemDeactivatedAfterEdit()) videoPoster(o.videoFile, &o, true);
                    tip("A soft edge round what's kept (hair, motion blur); the green fringe is taken out too");
                }
                ImGui::Checkbox("Play on start", &o.videoPlayOnStart); tip("Starts when its anchor is first found;\nscripts can play(), pause() and stop() it too");
                ImGui::SameLine(); ImGui::Checkbox("Loop", &o.videoLoop);
                labelled("Speed"); ImGui::SliderFloat("##vs", &o.videoSpeed, 0.1f, 3, "%.2f");
                ImGui::Checkbox("Muted", &o.videoMuted);
                if (!o.videoMuted) { labelled("Volume"); ImGui::SliderFloat("##vv", &o.videoVolume, 0, 1, "%.2f"); }
                if (o.kind == EMPTY) ImGui::TextDisabled("It needs a look to play on: add a Mesh (a Quad is a screen).");
                else ImGui::TextDisabled("It plays on the object's surface, in place of its texture.");
                ImGui::TextDisabled("With sound, phones start it after the first tap.");
            }
        }
    }
    // Trail Renderer
    if (o.hasTrail) {
        h = componentHeader("Trail Renderer", &o.trailOn, true);
        if ((h & 0xff) == C_REMOVE) o.hasTrail = false;
        else {
            if ((h & 0xff) == C_RESET) { o.trailTime = D.trailTime; o.trailWidth = D.trailWidth; memcpy(o.trailColour, D.trailColour, sizeof o.trailColour); }
            if (h & 0x100) {
                labelled("Length"); ImGui::SliderFloat("##tt", &o.trailTime, 0.05f, 5, "%.2f s"); tip("How long a piece of trail lasts");
                labelled("Width"); ImGui::SliderFloat("##tw", &o.trailWidth, 0.001f, 0.5f, "%.3f");
                labelled("Colour"); ImGui::ColorEdit4("##tc", o.trailColour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::TextDisabled("It fades out towards the end.");
            }
        }
    }
    // Rigidbody
    if (o.hasBody) {
        h = componentHeader("Rigidbody", nullptr, true);
        if ((h & 0xff) == C_REMOVE) o.hasBody = false;
        else {
            if ((h & 0xff) == C_RESET) { o.mass = D.mass; o.drag = D.drag; o.angularDrag = D.angularDrag; o.useGravity = D.useGravity; o.isKinematic = D.isKinematic; o.freeze = D.freeze; }
            if (h & 0x100) {
                labelled("Mass"); ImGui::SliderFloat("##rbm", &o.mass, 0.01f, 1000, "%.2f kg", ImGuiSliderFlags_Logarithmic); tip("How heavy: heavy things push light ones aside");
                labelled("Drag"); ImGui::SliderFloat("##rbd", &o.drag, 0, 10, "%.2f"); tip("How much the air slows it (0: not at all; a feather: 5)");
                labelled("Angular drag"); ImGui::SliderFloat("##rba", &o.angularDrag, 0, 10, "%.2f"); tip("How much the air slows its turning");
                ImGui::Checkbox("Use Gravity", &o.useGravity); tip("Off: it floats where it's left (Project Settings > Physics sets gravity)");
                ImGui::SameLine(160); ImGui::Checkbox("Is Kinematic", &o.isKinematic);
                tip("Moved only by its Transform (Animate, scripts, paths): it pushes things, nothing pushes it. A moving platform, a paddle, a door");
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Freeze position"); ImGui::SameLine(120);
                ImGui::CheckboxFlags("X##fpx", &o.freeze, 1); ImGui::SameLine(); ImGui::CheckboxFlags("Y##fpy", &o.freeze, 2); ImGui::SameLine(); ImGui::CheckboxFlags("Z##fpz", &o.freeze, 4);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Freeze rotation"); ImGui::SameLine(120);
                ImGui::CheckboxFlags("X##frx", &o.freeze, 8); ImGui::SameLine(); ImGui::CheckboxFlags("Y##fry", &o.freeze, 16); ImGui::SameLine(); ImGui::CheckboxFlags("Z##frz", &o.freeze, 32);
                tip("Constraints: it never moves or turns along these (in its anchor's axes). A ball that only rolls on the table: freeze Y");
                if (!o.hasCollider) ImGui::TextColored(col4(IM_COL32(0xf0, 0xb0, 0x60, 255)), "It needs a Collider to take part: Add Component > Physics.");
                else ImGui::TextDisabled("It falls, bounces and is pushed. Simulate in the Scene view (or Play) to see it.");
            }
        }
    }
    // Collider
    if (o.hasCollider) {
        o.colShape = std::clamp(o.colShape, 0, COL_COUNT - 1);
        hdrKey = "Collider"; h = componentHeader(COLLIDER_NAMES[o.colShape], &o.colliderOn, true); hdrKey = nullptr;
        if ((h & 0xff) == C_REMOVE) o.hasCollider = false;
        else {
            if ((h & 0xff) == C_RESET) { addCollider(o, o.colShape); o.isTrigger = D.isTrigger; o.friction = D.friction; o.bounciness = D.bounciness; }
            if (h & 0x100) {
                const char* shapes[] = {"Box", "Sphere", "Capsule", "Mesh (convex)"};
                int shape = o.colShape;
                labelled("Shape"); if (ImGui::Combo("##csh", &shape, shapes, COL_COUNT)) addCollider(o, shape);
                ImGui::Checkbox("Is Trigger", &o.isTrigger);
                tip("Things pass through it, and scripts hear them come and go: onTriggerEnter(other), onTriggerExit(other)");
                if (o.colShape != COL_MESH) vec3Row("Centre", o.colCentre, 0.005f);
                if (o.colShape == COL_BOX) vec3Row("Size", o.colSize, 0.005f);
                if (o.colShape == COL_SPHERE || o.colShape == COL_CAPSULE) { labelled("Radius"); ImGui::SliderFloat("##crad", &o.colRadius, 0.001f, 10, "%.3f", ImGuiSliderFlags_Logarithmic); }
                if (o.colShape == COL_CAPSULE) { labelled("Height"); ImGui::SliderFloat("##chei", &o.colHeight, 0.001f, 20, "%.3f", ImGuiSliderFlags_Logarithmic); tip("End to end, along its Y"); }
                if (o.colShape == COL_MESH) ImGui::TextDisabled("Its look's points, wrapped tight (a convex hull: hollows are filled in).");
                labelled("Friction"); ImGui::SliderFloat("##cfr", &o.friction, 0, 1, "%.2f"); tip("0: ice; 1: rubber");
                labelled("Bounciness"); ImGui::SliderFloat("##cbo", &o.bounciness, 0, 1, "%.2f"); tip("0: it lands dead; 1: it bounces back as high as it fell");
                if (ImGui::Button("Fit to its look", ImVec2(-1, 0))) addCollider(o, o.colShape);
                ImGui::TextDisabled("%s", o.hasBody ? "The green outline in the Scene view." : "Without a Rigidbody it stays put (moved by its Transform, it still pushes).");
            }
        }
    }
    // Scripts
    for (size_t si = 0; si < o.scripts.size(); ++si) {
        ImGui::PushID((int)si);
        Script& sc = o.scripts[si];
        const std::string title = "Script: " + fs::u8path(sc.file).filename().u8string();
        hdrKey = "Script"; hdrScript = (int)si; h = componentHeader(title.c_str(), &sc.on, true); hdrKey = nullptr; hdrScript = -1;
        if ((h & 0xff) == C_REMOVE) { o.scripts.erase(o.scripts.begin() + si); ImGui::PopID(); break; }
        if ((h & 0xff) == C_UP && si > 0) { std::swap(o.scripts[si], o.scripts[si - 1]); ImGui::PopID(); break; }
        if ((h & 0xff) == C_DOWN && si + 1 < o.scripts.size()) { std::swap(o.scripts[si], o.scripts[si + 1]); ImGui::PopID(); break; }
        if (h & 0x100) {
            fileRow("File", sc.file, L"JavaScript\0*.js\0\0");
            if (ImGui::Button("Edit Script", ImVec2(-1, 0))) openScript(sc.file);
            ImGui::TextDisabled("It runs in Play (Ctrl+P) and in the built game.");
        }
        ImGui::PopID();
    }
    // Add Component
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    const float bw = 200;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - bw) / 2 + ImGui::GetCursorPosX());
    if (ImGui::Button("Add Component", ImVec2(bw, 28))) ImGui::OpenPopup("addcomp");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) dropAsset(m, selMarker, o.id, std::string((const char*)pl->Data));
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopup("addcomp")) {
        if (!compClip.key.empty()) {   // the copied component (Copy Component in a component's ... menu)
            if (ImGui::MenuItem(("Paste " + compClip.key + (compClip.key == "Script" ? " (" + fs::u8path(compClip.script).filename().u8string() + ")" : "") + " As New").c_str())) pasteAsNew(o);
            ImGui::Separator();
        }
        addComponentMenu(o); ImGui::EndPopup();
    }
    (void)m;
}

// the Main Camera: Unity's Camera component (and its Audio Listener), for the phone or webcam
static void cameraInspector() {
    hdrObj = nullptr; hdrKey = nullptr;
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("\xe2\x97\x89 Main Camera"); ImGui::PopFont();
    ImGui::TextDisabled("Every scene has one: the phone (or the webcam).");
    int h = componentHeader("Camera", nullptr, false);
    if ((h & 0xff) == C_RESET) { const Project d; proj.camClear = d.camClear; memcpy(proj.camColour, d.camColour, sizeof proj.camColour); proj.camNear = d.camNear; proj.camFar = d.camFar; proj.camFov = d.camFov; }
    if (h & 0x100) {
        const char* clears[] = {"The camera's picture", "A solid colour"};
        labelled("Background"); ImGui::Combo("##clr", &proj.camClear, clears, 2);
        tip("AR shows what the camera sees. A solid colour: for previews and PCs (or a game that's all its own world)");
        if (proj.camClear == 1 || proj.tracking == TRACK_SURFACES) { labelled("Colour"); ImGui::ColorEdit3("##cc", proj.camColour, ImGuiColorEditFlags_NoInputs); }
        labelled("Field of view"); ImGui::SliderFloat("##fov", &proj.camFov, 20, 110, "%.0f\xc2\xb0"); tip("Up and down. AR uses the real lens; this is for previews and webcams");
        labelled("Near"); ImGui::DragFloat("##near", &proj.camNear, 0.001f, 0.001f, proj.camFar - 0.01f, "%.3f m");
        labelled("Far"); ImGui::DragFloat("##far", &proj.camFar, 0.5f, proj.camNear + 0.01f, 10000, "%.1f m");
        ImGui::TextDisabled("Its Audio Listener hears the Audio Sources (3D sound).");
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Objects under the Main Camera ride with it: a torch, a held thing, a sight in the middle of the view. "
                       "Make one with the Camera selected (GameObject menu), or drag one onto it.");
}
// a file selected in the Project window: what it is, a look at it, and what can be done with it
static void assetInspector() {
    const fs::path f = fs::u8path(selAsset);
    std::error_code ec;
    if (!fs::exists(f, ec)) { selAsset.clear(); return; }
    const bool dir = fs::is_directory(f, ec);
    const char* kind = dir ? "Folder" : assetKind(f);
    ImGui::PushFont(boldFont); ImGui::Text("%s  %s", dir ? "\xe2\x96\xb0" : assetIcon(kind), f.filename().u8string().c_str()); ImGui::PopFont();
    const auto size = dir ? 0 : fs::file_size(f, ec);
    const fs::path assets = fs::u8path(folder) / "assets";
    const fs::path rel = fs::relative(f, fs::u8path(folder), ec);
    ImGui::TextDisabled("%s%s   %s", kind, dir ? "" : size >= 1048576 ? ("   " + std::to_string(size / 1048576) + "." + std::to_string(size % 1048576 * 10 / 1048576) + " MB").c_str() : ("   " + std::to_string((size + 1023) / 1024) + " KB").c_str(),
                        rel.generic_u8string().c_str());
    (void)assets;
    ImGui::Separator(); ImGui::Spacing();
    Scene::Marker* m = curMarker();
    Object* o = curObject();
    const float w = ImGui::GetContentRegionAvail().x;
    if (!strcmp(kind, "Picture")) {
        int iw = 0, ih = 0; const unsigned t = Viewer::pictureTexture(selAsset, &iw, &ih);
        if (t && iw > 0) { const float k = std::min(w / iw, 260.0f / ih); ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(iw * k, ih * k)); ImGui::TextDisabled("%d x %d pixels", iw, ih); }
    } else if (!strcmp(kind, "Model")) {
        const auto clips = Viewer::clipsOf(selAsset);
        ImGui::Text("Animation clips: %d", (int)clips.size());
        for (const auto& c : clips) ImGui::BulletText("%s", c.c_str());
    } else if (!strcmp(kind, "Script")) {
        if (ImGui::Button("Edit Script", ImVec2(-1, 0))) openScript(selAsset);
        std::ifstream in(f, std::ios::binary); std::string code, line; int n = 0;
        while (n < 200 && std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); code += line + "\n"; ++n; }
        ImGui::BeginChild("##code", ImVec2(0, 280), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(code.c_str());
        ImGui::EndChild();
    } else if (!strcmp(kind, "Prefab")) {
        std::vector<Object> objs;
        if (loadPrefab(selAsset, objs)) { ImGui::Text("%d object%s:", (int)objs.size(), objs.size() == 1 ? "" : "s"); for (const auto& q : objs) ImGui::BulletText("%s%s", q.parent == -1 ? "" : "   ", q.name.c_str()); }
    } else if (!strcmp(kind, "Material")) {
        static std::string loaded; static Object mat;
        if (loaded != selAsset) { mat = Object(); mat.kind = CUBE; loadMaterial(selAsset, mat); loaded = selAsset; }
        ImGui::PushItemWidth(-1);
        if (materialFields(mat, true)) { beginEdit(); saveMaterial(selAsset, mat); const int n = applyMaterialEverywhere(selAsset); if (n) say("Changed on " + std::to_string(n) + " object" + (n == 1 ? "" : "s")); }
        ImGui::PopItemWidth();
        int users = 0; for (int a = -1; a < (int)proj.markers.size(); ++a) for (const auto& q : (a < 0 ? proj.camera : proj.markers[a]).objects) if (q.material == selAsset) ++users;
        ImGui::TextDisabled("%d object%s use%s it. Drop it onto an object to give it this material.", users, users == 1 ? "" : "s", users == 1 ? "s" : "");
    } else if (!strcmp(kind, "Video")) {
        const fs::path poster = f.parent_path() / fs::u8path(f.stem().u8string() + ".poster.png");
        int iw = 0, ih = 0; const unsigned t = fs::exists(poster, ec) ? Viewer::pictureTexture(poster.u8string(), &iw, &ih) : 0;
        if (t && iw > 0) { const float k = std::min(w / iw, 220.0f / ih); ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(iw * k, ih * k)); }
        if (ImGui::Button("Play it", ImVec2(-1, 0))) openInExplorer(selAsset);
    } else if (!strcmp(kind, "Audio")) {
        if (ImGui::Button("Play it", ImVec2(-1, 0))) openInExplorer(selAsset);
    } else if (!strcmp(kind, "Marker") && f.extension() == ".marker" && m && selMarker >= 0) {
        if (ImGui::Button(("Use on " + m->name).c_str(), ImVec2(-1, 0))) { beginEdit(); applyMarkerFile(*m, selAsset); shownMarker = -1; }
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    // what can be done with it in the scene (as dragging it onto the Scene view or the Hierarchy)
    if (!dir && m) {
        const bool needsObject = !strcmp(kind, "Script") || !strcmp(kind, "Audio");
        if (!needsObject && (!strcmp(kind, "Picture") || !strcmp(kind, "Model") || !strcmp(kind, "Prefab") || !strcmp(kind, "Video"))) {
            if (ImGui::Button(("Add to " + (selMarker == -2 ? std::string("the Main Camera") : m->name)).c_str(), ImVec2(-1, 0))) { const std::string a = selAsset; dropAsset(*m, selMarker, -1, a); selAsset.clear(); return; }
        }
        if (o && strcmp(kind, "Marker") && strcmp(kind, "Prefab")) {
            const std::string what = !strcmp(kind, "Script") ? "Add to " : !strcmp(kind, "Audio") ? "Give a sound to " : !strcmp(kind, "Video") ? "Play it on " : !strcmp(kind, "Material") ? "Give it to " : "Put under ";
            if (ImGui::Button((what + o->name).c_str(), ImVec2(-1, 0))) { const std::string a = selAsset; dropAsset(*m, selMarker, o->id, a); selAsset.clear(); return; }
        }
        if (needsObject && !o) ImGui::TextDisabled("Select an object to give it this %s (or drag it onto one).", !strcmp(kind, "Script") ? "script" : "sound");
    }
    if (ImGui::Button("Show in Explorer", ImVec2(-1, 0))) openInExplorer(f.parent_path().u8string());
    if (ImGui::Button("Rename...", ImVec2(-1, 0))) { renaming = selAsset; snprintf(renameBuf, sizeof renameBuf, "%s", f.filename().u8string().c_str()); }
    ImGui::Spacing();
    ImGui::TextDisabled("Drag it onto the Scene view, the Hierarchy or an object.");
}
static void inspector() {
    ImGui::BeginChild("##insp");
    ImGui::PushItemWidth(-1);
    Scene::Marker* m = curMarker();
    if (!selAsset.empty() && (selMarker != selAssetMarker || selObject != selAssetObject)) selAsset.clear();   // something else was selected since
    if (!selAsset.empty()) assetInspector();
    else if (Object* o = curObject()) objectInspector(*m, *o);
    else if (selMarker == -2) cameraInspector();
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
    ImGui::SameLine();
    {
        const bool on = simWorld != nullptr;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (ImGui::Button(on ? "Stop" : "Simulate", ImVec2(80, 0))) { if (on) stopSimulate(); else startSimulate(); }
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(on ? "Stop: everything goes back where it was" : "Physics in this view: Rigidbodies fall, bounce and push (scripts run in Play)");
    }
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
    ImGui::SameLine(0, 14);
    {   // Unity's 2D button: straight down and flat (a floor plan), or the 3D view
        const bool on = lk.view2D;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (ImGui::Button("2D", ImVec2(32, 0))) lk.view2D = !lk.view2D;
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("2D: from straight above, no perspective (drag pans, wheel zooms). Again: 3D");
    }
    ImGui::SameLine(0, 14); ImGui::Checkbox("Desk", &lk.desk);
    ImGui::SameLine(); ImGui::Checkbox("Axes", &lk.gizmo);
    ImGui::SameLine(); ImGui::Checkbox("UI", &showUI); if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's UI (Canvases) over the view, on a phone");
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
    static bool uiHasMouse = false;
    if (simWorld) { Physics::step(simWorld, proj, ImGui::GetIO().DeltaTime, lk.time); Physics::takeContacts(simWorld); }
    const unsigned tex = Viewer::render(proj, selMarker, selObject, (int)avail.x, (int)avail.y, hov && !handleHasMouse && !uiHasMouse, picked);
    if (!uiHasMouse) {
        if (picked >= 0) { selMarker = ownerOf(picked); selObject = picked; }
        else if (picked == -3) { selMarker = -2; selObject = -1; }   // the Main Camera
        else if (picked == -2 && !handleHasMouse) selObject = -1;
        if (picked != -1) selAsset.clear();
    }
    if (ImGui::BeginDragDropTarget()) {   // a file from the Project window: into the scene (onto the selected object, or the anchor)
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) {
            Scene::Marker* dm = curMarker(); const int dmi = dm ? selMarker : 0;
            if (!dm && !proj.markers.empty()) dm = &proj.markers[0];
            if (dm) dropAsset(*dm, dmi, selObject, std::string((const char*)pl->Data));
        }
        ImGui::EndDragDropTarget();
    }
    ImDrawList* vdl = ImGui::GetWindowDrawList();
    vdl->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + avail.x, a.y + avail.y), ImVec2(0, 1), ImVec2(1, 0));
    // the Main Camera selected (or something riding with it): what it sees, in the bottom right corner, as Unity does
    const bool camSel = selMarker == -2;
    if (camSel && lk.gizmo && avail.x > 200 && avail.y > 200) {
        const float ph = std::min(avail.y * 0.42f, 320.0f), pw = ph * 9.0f / 16.0f;
        const unsigned pt = Viewer::renderCameraPreview(proj, selMarker, (int)pw, (int)ph);
        const ImVec2 p1(a.x + avail.x - pw - 12, a.y + avail.y - ph - 12), p2(a.x + avail.x - 12, a.y + avail.y - 12);
        vdl->AddRectFilled(ImVec2(p1.x - 3, p1.y - 22), ImVec2(p2.x + 3, p2.y + 3), IM_COL32(0x22, 0x22, 0x22, 240), 4);
        vdl->AddText(ImVec2(p1.x + 4, p1.y - 19), C_TEXT, "Camera Preview");
        if (pt) vdl->AddImage((ImTextureID)(intptr_t)pt, p1, p2, ImVec2(0, 1), ImVec2(1, 0));
    }
    const Object* so = curObject();
    handleHasMouse = (so && so->ui != UI_NONE) ? false : handles(a, avail);   // UI elements move on the phone, not in 3D
    uiHasMouse = !handleHasMouse && uiOverlay(a, avail);
}

// ---------- Project Settings: what the game tracks, and the player's location ----------
static void projectSettings() {
    if (showSettings) { ImGui::OpenPopup("Project Settings"); showSettings = false; }
    ImGui::SetNextWindowSize(ImVec2(620, 0));
    if (!ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("What the game tracks"); ImGui::PopFont();
    ImGui::TextDisabled("One for the whole game: its anchors (the top of the Hierarchy) are of this kind.");
    struct Opt { int v; const char* name; const char* what; const char* how; bool ready; };
    const Opt opts[] = {
        {TRACK_MARKERS, "Markers", "Printed markers: Hiro, Kanji, your own picture or letter, barcodes 0..63. Steady and fast; needs printing.", "jsartoolkit5", true},
        {TRACK_IMAGES, "Images", "Any photo, poster, painting or book cover, as it is. Detailed pictures with corners track best.", "MindAR", true},
        {TRACK_FACES, "Faces", "The player's face: masks, glasses, hats, face filters; scripts can read expressions.", "MindAR face", true},
        {TRACK_SURFACES, "Surfaces", "Floors, tables and walls (plane detection): things placed on real surfaces, routes to walk and follow (indoor navigation). Phones with ARCore, in Chrome.", "WebXR", true},
        {TRACK_ARUCO, "ArUco", "OpenCV's square markers (4x4, 5x5, 6x6): many at once, fast and steady, each its own anchor; printed from here. The markers of OpenCV and robotics projects.", "OpenCV.js", true},
        {TRACK_HANDS, "Hands", "The player's hand: things on the palm or the fingertips.", "coming", false},
    };
    for (const Opt& o : opts) {
        ImGui::BeginDisabled(!o.ready);
        if (ImGui::RadioButton(o.name, proj.tracking == o.v) && proj.tracking != o.v) {
            beginEdit(); proj.tracking = o.v; shownMarker = -1;
            for (auto& m : proj.markers) if (m.name.rfind("Marker", 0) == 0 || m.name.rfind("Picture", 0) == 0 || m.name.rfind("AR Marker", 0) == 0) {}
        }
        ImGui::EndDisabled();
        ImGui::SameLine(110); ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
        ImGui::TextDisabled("%s  (%s)", o.what, o.how); ImGui::PopTextWrapPos();
    }
    if (proj.tracking == TRACK_ARUCO) {
        const char* dicts[] = {"4x4 (50 markers): biggest cells, seen furthest", "5x5 (100 markers)", "6x6 (250 markers)", "The original ArUco (1024)"};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##adict", &proj.arucoDict, dicts, 4)) { beginEdit(); shownMarker = -1; }
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Camera calibration"); ImGui::PopFont();
    if (proj.calibW > 0)
        ImGui::Text("%d x %d: field of view %.1f\xc2\xb0 across, error %.2f px", proj.calibW, proj.calibH, 2 * std::atan(proj.calibW / 2.0 / proj.calibK[0]) * 180 / 3.14159265, proj.calibRms);
    else ImGui::TextDisabled("None: the field of view is guessed (each anchor's \"Any camera\" settings).");
    ImGui::TextDisabled("Markers and surfaces sit exactly where they are on a calibrated camera. ArUco and Surfaces\n"
                        "games have calibrate.html: show it the board from 10-20 angles; it keeps the result on that\n"
                        "phone or PC, and gives camera.json to load here for every player with that camera.");
    if (ImGui::Button("Print the board...", ImVec2(160, 0))) {
        const std::string f = saveDialog(L"PNG picture\0*.png\0\0", L"png", "calibration board.png");
        if (!f.empty()) say(Markers::savePng(f, Markers::printableCharuco(300)) ? "Saved the board: print it flat at 100% (A4 is fine)" : "Couldn't save it");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load camera.json...", ImVec2(160, 0))) {
        const std::string f = openDialog(L"Camera calibration\0*.json\0\0");
        if (!f.empty()) {
            std::ifstream in(fs::u8path(f), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
            Json::Value v; const Json::Value* K = nullptr;
            if (Json::parse(ss.str(), v) && (K = v.get("K")) && K->type == Json::Value::ARR && K->a.size() == 4 && v.num("width", 0) > 0) {
                beginEdit();
                proj.calibW = (int)v.num("width", 0); proj.calibH = (int)v.num("height", 0); proj.calibRms = v.num("rms", 0);
                for (int i = 0; i < 4; ++i) proj.calibK[i] = K->a[i].n;
                proj.calibDist.clear();
                if (const Json::Value* d = v.get("dist")) if (d->type == Json::Value::ARR) for (const auto& x : d->a) proj.calibDist.push_back(x.n);
                say("The camera's calibration is in the project");
            } else say("That isn't a camera.json");
        }
    }
    if (proj.calibW > 0) { ImGui::SameLine(); if (ImGui::Button("Forget it", ImVec2(100, 0))) { beginEdit(); proj.calibW = 0; proj.calibDist.clear(); } }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Physics"); ImGui::PopFont();
    labelled("Gravity"); if (ImGui::SliderFloat3("##grav", proj.gravity, -30, 30, "%.2f")) beginEdit();
    tip("In each anchor's space, units a second a second: Y is up out of a marker, or up from the floor. Earth: 0, -9.81, 0");
    if (ImGui::Button("Earth", ImVec2(80, 0))) { beginEdit(); proj.gravity[0] = proj.gravity[2] = 0; proj.gravity[1] = -9.81f; }
    ImGui::SameLine(); if (ImGui::Button("Moon", ImVec2(80, 0))) { beginEdit(); proj.gravity[0] = proj.gravity[2] = 0; proj.gravity[1] = -1.62f; }
    ImGui::SameLine(); if (ImGui::Button("None", ImVec2(80, 0))) { beginEdit(); proj.gravity[0] = proj.gravity[1] = proj.gravity[2] = 0; }
    ImGui::TextDisabled("On a marker 1 unit is the marker's width: a small marker's world is small, so things fall fast\nfor their size. Lower it for a slower, toy-like fall.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::PushFont(boldFont); ImGui::TextUnformatted("Location"); ImGui::PopFont();
    if (ImGui::Checkbox("Use the player's location (GPS)", &proj.gps)) beginEdit();
    ImGui::TextDisabled("Off: the game never asks for it. On: scripts can read where the player is, and place anchors at\nreal places (coming). Phones ask the player first.");
    ImGui::Spacing();
    if (ImGui::Button("Done", ImVec2(-1, 28)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
    if (io.WantTextInput || scriptFocused) { if (scriptFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) playInBrowser(); return; }
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
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B)) { if (io.KeyShift) showBuild = true; else startBuild(proj.platform, true); }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) playInBrowser();
    if (m && selObject >= 0 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) { beginEdit(); selObject = duplicate(proj, *m, selObject); }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {   // whatever's selected, as Unity
        if (m && selObject >= 0) { beginEdit(); remove(*m, selObject); selObject = -1; }
        else if (selObject < 0 && selMarker >= 0 && selMarker < (int)proj.markers.size()) {   // an anchor, and all on it
            beginEdit(); const std::string n = proj.markers[selMarker].name;
            proj.markers.erase(proj.markers.begin() + selMarker); selMarker = proj.markers.empty() ? -1 : std::max(0, selMarker - 1); shownMarker = -1;
            say("Deleted " + n + " (Ctrl+Z brings it back)");
        } else if (selMarker == -2 && selObject < 0) say("Every scene needs its Main Camera");
    }
    if (m && selObject >= 0 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) copyObject(*m, selObject);
    if (m && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !objClip.empty()) {   // as Unity: beside the selected object (or on the anchor)
        const int k = selObject >= 0 ? indexOfId(*m, selObject) : -1;
        const int id = pasteObject(*m, k >= 0 ? m->objects[k].parent : -1); if (id >= 0) selObject = id;
    }
    if (m && selObject >= 0 && ImGui::IsKeyPressed(ImGuiKey_F2)) { const int k = indexOfId(*m, selObject); if (k >= 0) { renamingObj = selObject; snprintf(objNameBuf, sizeof objNameBuf, "%s", m->objects[k].name.c_str()); } }
}

static void frame() {
    frameStart = proj;
    keys();
    menuBar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = ImGui::GetFrameHeight(), status = 26;
    const float H = vp->Size.y - top - status;
    // the dockspace between the menu bar and the status line
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + top)); ImGui::SetNextWindowSize(ImVec2(vp->Size.x, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    const ImGuiID dock = ImGui::GetID("DioramaDock");
    if (resetLayout || !ImGui::DockBuilderGetNode(dock)) {   // Unity's default: Hierarchy left, Inspector right, the Scene in the middle, Project + Console under it
        resetLayout = false;
        showHierarchy = showInspector = showSceneWin = showProject = showConsole = true;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImVec2(vp->Size.x, H));
        ImGuiID mid = dock;
        const ImGuiID left = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Left, 280.0f / vp->Size.x, nullptr, &mid);
        const ImGuiID right = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Right, 340.0f / (vp->Size.x - 280.0f), nullptr, &mid);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, std::min(0.36f, 260.0f / H), nullptr, &mid);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene", mid);
        ImGui::DockBuilderDockWindow("Project", bottom);
        ImGui::DockBuilderDockWindow("###Console", bottom);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
    if (showHierarchy) { if (ImGui::Begin("Hierarchy", &showHierarchy)) hierarchy(); ImGui::End(); }
    if (showInspector) { if (ImGui::Begin("Inspector", &showInspector)) inspector(); ImGui::End(); }
    if (showSceneWin) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
        if (ImGui::Begin("Scene", &showSceneWin)) sceneView();
        ImGui::End();
        ImGui::PopStyleColor();
    }
    if (showProject) { if (ImGui::Begin("Project", &showProject)) projectPanel(); ImGui::End(); }
    if (bottomTab == 1) { showConsole = true; ImGui::SetNextWindowFocus(); bottomTab = 0; }
    if (showConsole) { if (ImGui::Begin(consoleTitle().c_str(), &showConsole)) consolePanel(); ImGui::End(); }
    scriptEditor();
    markerMaker();
    sceneManager();
    buildSettings();
    buildProgress();
    profiler();
    prefsWindow();
    // the status line
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - status)); ImGui::SetNextWindowSize(ImVec2(vp->Size.x, status));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    if (!toast.empty() && now() < toastUntil) ImGui::TextUnformatted(toast.c_str());
    else ImGui::TextDisabled("%s  -  %s", proj.title.c_str(), folder.empty() ? "not saved yet" : folder.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    about();
    projectSettings();
    // a field, checkbox or menu item that just began an edit: undoable (the handles call beginEdit themselves)
    const ImGuiID active = ImGui::GetActiveID();
    static ImGuiID lastActive = 0;
    if (active != 0 && active != lastActive && dragAxis < 0 && ImGui::IsAnyItemActive()) beginEdit();
    lastActive = active;
    if (undoStack.size() > 1 && Scene::toJson(undoStack.back()) == Scene::toJson(proj) && active == 0) undoStack.pop_back();   // an edit that changed nothing
}

int main(int argc, char** argv) {
    std::string capture, tmpl = "gallery", openArg, buildArg; int selectArg = -1, pickX = -1, pickY = -1; bool buildWin = false; std::string buildFor, addVideoArg, uiFontArg; float simulateArg = -1;
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
        else if (args[i] == "--board" && i + 1 < args.size()) return Markers::savePng(args[i + 1], Markers::printableCharuco(100)) ? 0 : 1;
        else if (args[i] == "--template" && i + 1 < args.size()) tmpl = args[++i];
        else if (args[i] == "--select" && i + 1 < args.size()) selectArg = atoi(args[++i].c_str());
        else if (args[i] == "--build" && i + 1 < args.size()) buildArg = args[++i];
        else if (args[i] == "--build-windows" && i + 1 < args.size()) { buildArg = args[++i]; buildWin = true; }
        else if (args[i] == "--build-for" && i + 2 < args.size()) { buildFor = args[++i]; buildArg = args[++i]; }
        else if (args[i] == "--build-ui") showBuild = true;   // opens Build Settings (screenshots)
        else if (args[i] == "--maker") { showMaker = true; mk.source = 4; }
        else if (args[i] == "--serve" && i + 2 < args.size()) {   // tests: Play's server on a folder for some seconds, then quit
            Serve::start(args[i + 1], 8796); SDL_Delay((Uint32)(atof(args[i + 2].c_str()) * 1000)); return 0;
        }
        else if (args[i] == "--scenes") showScenes = true;
        else if (args[i] == "--profiler") showProfiler = true;
        else if (args[i] == "--2d") Viewer::look().view2D = true;   // tests: the 2D view   // tests: the Profiler open
        else if (args[i] == "--simulate" && i + 1 < args.size()) simulateArg = (float)atof(args[++i].c_str());   // tests: Simulate, run this long
        else if (args[i] == "--ui-font" && i + 1 < args.size()) uiFontArg = args[++i];   // tests: this font on every UI element
        else if (args[i] == "--select-camera") selectArg = -100;   // tests: the Main Camera selected
        else if (args[i] == "--select-asset" && i + 1 < args.size()) { selAsset = args[++i]; selAssetMarker = 0; selAssetObject = -1; }   // tests: a file selected in the Project window
        else if (args[i] == "--add-video" && i + 1 < args.size()) addVideoArg = args[++i];   // tests: a video, as dropped on the first anchor   // opens the Scenes window (for screenshots)   // opens the Marker Maker (for screenshots)
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
    {   // the windows' layout (docking) is kept between sessions, in the user's settings
        static std::string ini;
        char* pref = SDL_GetPrefPath("Cursed Entertainment", "Diorama");
        if (pref) { ini = std::string(pref) + "layout.ini"; SDL_free(pref); }
        ImGui::GetIO().IniFilename = ini.empty() || !capture.empty() ? nullptr : ini.c_str();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    }
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
    autosaveFolders = capture.empty() && buildArg.empty();
    loadPrefs();
    // the start: a new scene, unless Preferences say the last project (then it's opened)
    const std::string last = autosaveFolders && openArg.empty() && prefs.reopenLast ? lastProject() : "";
    if (!openArg.empty()) openProject(openArg);
    else if (!last.empty()) openProject(last);
    else newFromTemplate(tmpl == "basic" ? 0 : tmpl == "navigation" ? 2 : tmpl == "planes" ? 3 : tmpl == "physics" ? 4 : 1);
    if (!addVideoArg.empty() && !proj.markers.empty()) dropAsset(proj.markers[0], 0, -1, addVideoArg);
    if (!buildArg.empty()) { std::string err, apk; const bool ok = buildFor == "android" ? buildAndroid(buildArg, err, apk) : buildFor == "linux" ? buildLinux(buildArg, false, err) : buildFor == "pi" ? buildLinux(buildArg, true, err) : (buildWin || buildFor == "windows") ? buildWindows(buildArg, err) : buildGame(buildArg, err); printf("%s\n", ok ? "built" : err.c_str()); return ok ? 0 : 1; }
    if (selectArg >= 0 && curMarker() && selectArg < (int)curMarker()->objects.size()) selObject = curMarker()->objects[selectArg].id;
    if (!uiFontArg.empty() && curMarker()) for (auto& o : curMarker()->objects) if (o.ui != UI_NONE) o.uiFontFile = uiFontArg;
    if (simulateArg >= 0) {   // tests: Simulate, stepped this many seconds at 60 a second
        startSimulate();
        for (int k = 1; k <= (int)std::lround(simulateArg * 60); ++k) Physics::step(simWorld, proj, 1.0f / 60, k / 60.0f);
        Physics::takeContacts(simWorld);
    }
    if (selectArg == -100) { selMarker = -2; selObject = -1; }
    if (capture.empty()) { SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window); }

    int frames = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) { const std::string f = e.drop.file; SDL_free(e.drop.file); if (fs::u8path(f).extension() == ".diorama") openProject(f); else importFiles({f}); }
        }
        if (UiFonts::load()) { ImGui_ImplOpenGL3_DestroyFontsTexture(); ImGui_ImplOpenGL3_CreateFontsTexture(); }   // the UI's fonts, as asked for
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        frame();
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (pickX >= 0 && frames == 20) Viewer::clickAt(pickX, pickY);
        if (!capture.empty() && ++frames >= 40) {
            if (pickX >= 0) { std::ofstream(fs::u8path(capture + ".txt")) << (curObject() ? curObject()->name : selMarker == -2 ? std::string("Main Camera") : std::string("(nothing)")) << "\n"; }
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
