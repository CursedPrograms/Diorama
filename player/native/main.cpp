// DioramaNative: Diorama's native player. A built game (its scene.json, markers/, assets/) played with the PC's own
// camera and Diorama's own C++ renderer: PBR, many lights, soft shadows onto the real table, HDR and bloom. It tracks
// as the web game does, with the C++ twins of its trackers: ARToolKit's .patt and barcodes (src/markertrack), ArUco
// (src/arucotrack) and surfaces (src/surfacetrack); the camera's calibration from the project when there is one.
//
//   DioramaNative [game folder] [--camera 0] [--video file] [--shot out.png] [--after 3] [--size 1280x720]
//                 [--lock-after seconds] [--tap x,y,seconds]   (tests: lock a surface; a tap, x and y -1..1)
// The game folder: given, else beside the exe (scene.json there), else its "game" folder.
// Keys: Space or a click locks a surface (Surfaces); F11 full screen; Esc quits.
#include "arucotrack.h"
#include "markertrack.h"
#include "scene.h"
#include "surfacetrack.h"
#include "viewer.h"
#include "script.h"
#include "ui.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <SDL.h>
#include <SDL_opengl.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using Scene::Project;

// ---- the camera: frames read on their own thread (a video file plays at its own speed, round and round)
struct Camera {
    cv::VideoCapture cap;
    std::thread th;
    std::mutex mu;
    cv::Mat latest;
    long long count = 0;
    std::atomic<bool> stop{false};
    bool file = false;
    double fps = 30;
    bool open(int index, const std::string& video) {
        if (!video.empty()) { file = true; cap.open(video); fps = cap.get(cv::CAP_PROP_FPS); if (!(fps > 1 && fps < 240)) fps = 30; }
        else {
#ifdef _WIN32
            cap.open(index, cv::CAP_MSMF);
            if (!cap.isOpened()) cap.open(index, cv::CAP_DSHOW);
#else
            cap.open(index);
#endif
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280); cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        }
        if (!cap.isOpened()) return false;
        th = std::thread([this] {
            cv::Mat f;
            auto next = std::chrono::steady_clock::now();
            while (!stop) {
                if (!cap.read(f) || f.empty()) {
                    if (file) { cap.set(cv::CAP_PROP_POS_FRAMES, 0); continue; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;
                }
                { std::lock_guard<std::mutex> l(mu); f.copyTo(latest); ++count; }
                if (file) { next += std::chrono::microseconds((long long)(1e6 / fps)); std::this_thread::sleep_until(next); }
            }
        });
        return true;
    }
    bool get(cv::Mat& out, long long& seen) {
        std::lock_guard<std::mutex> l(mu);
        if (count == seen || latest.empty()) return false;
        latest.copyTo(out); seen = count; return true;
    }
    ~Camera() { stop = true; if (th.joinable()) th.join(); }
};

// ---- poses: OpenCV's (marker -> camera, its axes) into OpenGL's view of an anchor (column-major)
// the anchor's axes from the tracker's: markers lie with y up their picture and z out of them; Diorama's anchors have
// Y out of the marker and -Z up its picture. Surfaces are already Diorama's (y up out of them).
static void toView(const cv::Matx33d& R, const cv::Vec3d& t, bool markerAxes, float out[16]) {
    // anchor -> marker (M), then marker -> camera (R, t), then OpenCV's camera -> OpenGL's (y and z flipped)
    const cv::Matx33d M = markerAxes ? cv::Matx33d(1, 0, 0, 0, 0, -1, 0, 1, 0) : cv::Matx33d::eye();
    const cv::Matx33d A = R * M;
    const double F[3] = {1, -1, -1};
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) out[c * 4 + r] = (float)(F[r] * A(r, c));
    for (int r = 0; r < 3; ++r) out[12 + r] = (float)(F[r] * t[r]);
    out[3] = out[7] = out[11] = 0; out[15] = 1;
}

// the projection for a camera of focal lengths fx, fy and centre cx, cy (its picture's pixels), as much of the
// picture as fills the window (cropped, like object-fit: cover); rect: that part of the picture (0..1)
static void projection(double fx, double fy, double cx, double cy, int W, int H, int w, int h, float n, float f, float P[16], float rect[4]) {
    const double s = std::max((double)w / W, (double)h / H), vw = w / s, vh = h / s, x0 = (W - vw) / 2, y0 = (H - vh) / 2;
    std::memset(P, 0, 16 * sizeof(float));
    P[0] = (float)(2 * fx / vw);
    P[5] = (float)(2 * fy / vh);
    P[8] = (float)(1 - 2 * (cx - x0) / vw);
    P[9] = (float)(2 * (cy - y0) / vh - 1);
    P[10] = -(f + n) / (f - n); P[11] = -1;
    P[14] = -2 * f * n / (f - n);
    rect[0] = (float)(x0 / W); rect[1] = (float)(y0 / H); rect[2] = (float)(vw / W); rect[3] = (float)(vh / H);
}

static std::string readFile(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

static bool quiet = false;   // a test run (--shot): no message boxes
static void fail(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str()); std::fflush(stderr);
    if (!quiet) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Diorama", msg.c_str(), nullptr);
}

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    { int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n); if (wa) { for (int i = 1; i < n; ++i) args.push_back(fs::path(wa[i]).u8string()); LocalFree(wa); } }
#else
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
#endif
    (void)argc; (void)argv;
    std::string gameArg, video, shot; int camIndex = 0; double after = 3; int winW = 1280, winH = 720;
    double lockAfter = -1, lockVAfter = -1; std::vector<double> testTap;   // tests: lock a surface after some seconds; a tap (x, y in -1..1, seconds)
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        if (a == "--camera" && more) camIndex = std::atoi(args[++i].c_str());
        else if (a == "--video" && more) video = args[++i];
        else if (a == "--shot" && more) shot = args[++i];
        else if (a == "--after" && more) after = std::atof(args[++i].c_str());
        else if (a == "--lock-after" && more) lockAfter = std::atof(args[++i].c_str());
        else if (a == "--lock-wall-after" && more) lockVAfter = std::atof(args[++i].c_str());
        else if (a == "--tap" && more) { std::stringstream ss(args[++i]); std::string part; while (std::getline(ss, part, ',')) testTap.push_back(std::atof(part.c_str())); }
        else if (a == "--size" && more) { const std::string v = args[++i]; const size_t x = v.find('x'); if (x != std::string::npos) { winW = std::atoi(v.c_str()); winH = std::atoi(v.c_str() + x + 1); } }
        else if (a.rfind("--", 0) != 0) gameArg = a;
    }
    quiet = !shot.empty();
    char* base = SDL_GetBasePath();
    const fs::path exeDir = base ? fs::u8path(base) : fs::current_path();
    if (base) SDL_free(base);
    fs::path game = !gameArg.empty() ? fs::u8path(gameArg) : fs::exists(exeDir / "scene.json") ? exeDir : exeDir / "game";
    if (!fs::exists(game / "scene.json")) { fail("There's no game here (scene.json): " + game.u8string()); return 1; }
    std::error_code ec; fs::current_path(game, ec);   // the game's files are where its scene says, from here

    // ---- the game: scene 0 is scene.json, the others scene-<n>.json (Build writes them)
    Project proj; std::string err;
    auto sceneFile = [](int i) { return i == 0 ? std::string("scene.json") : "scene-" + std::to_string(i) + ".json"; };
    if (!Scene::fromJson(readFile(sceneFile(0)), proj, err)) { fail("The game's scene.json: " + err); return 1; }
    if (proj.tracking == Scene::TRACK_IMAGES || proj.tracking == Scene::TRACK_FACES || proj.tracking == Scene::TRACK_HANDS) {
        fail("The native player tracks markers, ArUco markers and surfaces. This game tracks pictures or faces: build it with the JavaScript runtime.");
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fail(SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (proj.fullscreen && shot.empty()) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* win = SDL_CreateWindow(proj.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, flags);
    if (!win) { fail(SDL_GetError()); return 1; }
    if (fs::exists("icon.png")) {
        int iw, ih, n; unsigned char* px = stbi_load("icon.png", &iw, &ih, &n, 4);
        if (px) { SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32); if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); } stbi_image_free(px); }
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    if (!Viewer::init(exeDir.u8string())) { fail("OpenGL 3.3 is needed"); return 1; }
    Viewer::look().desk = false; Viewer::look().gizmo = false;
    // the Canvas UI: Dear ImGui, the game's font
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    {
        ImFontConfig fc; fc.OversampleH = 2;
        static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2713, 0x2713, 0};
        if (!fs::exists("C:/Windows/Fonts/segoeui.ttf") || !ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 40, &fc, ranges)) ImGui::GetIO().Fonts->AddFontDefault();
        if (fs::exists("C:/Windows/Fonts/seguisym.ttf")) { ImFontConfig m; m.MergeMode = true; static const ImWchar tick[] = {0x2713, 0x2713, 0}; ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 40, &m, tick); }
    }
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    const bool surfaces = proj.tracking == Scene::TRACK_SURFACES, aruco = proj.tracking == Scene::TRACK_ARUCO;
    { const float head[3] = {0, 0, -0.6f}; Viewer::setAnchorMode(surfaces ? 3 : 0, head); }

    Camera cam;
    if (!cam.open(camIndex, video)) { fail(video.empty() ? "The camera didn't start (is another program using it?)" : "Couldn't open " + video); return 1; }

    // ---- the trackers (made again for each scene: its markers)
    const bool calibrated = proj.calibW > 0 && proj.calibK[0] > 0;
    double fovDeg = 60;
    std::unique_ptr<MarkerTrack::Tracker> mt; std::unique_ptr<ArucoTrack::Tracker> at; std::unique_ptr<SurfaceTrack::Tracker> st;
    // Surfaces: every plane locked (Space / a click: a floor or table; V: a wall), each with its own tracker; the scene's
    // anchors ride on the first floor or table (else the first plane)
    struct Plane { std::unique_ptr<SurfaceTrack::Tracker> tr; bool vertical = false; float view[16]; bool on = false; double at = -1; std::string id; bool sent = false; };
    std::vector<Plane> planes; SurfaceTrack::Settings surfBase; int lockKind = -1;   // 0 a floor or table, 1 a wall
    auto anchorPlane = [&]() -> int { for (size_t i = 0; i < planes.size(); ++i) if (!planes[i].vertical) return (int)i; return planes.empty() ? -1 : 0; };
    std::map<int, int> pattToMarker, barToMarker, arucoToMarker;
    auto makeTrackers = [&]() {
        mt.reset(); at.reset(); st.reset(); pattToMarker.clear(); barToMarker.clear(); arucoToMarker.clear();
        const Scene::Marker* first = proj.markers.empty() ? nullptr : &proj.markers[0];
        fovDeg = calibrated ? 2 * std::atan(proj.calibW / 2.0 / proj.calibK[0]) * 180 / 3.14159265358979 : first && first->webcamFov > 0 ? first->webcamFov : 60;
        if (aruco) {
            ArucoTrack::Settings s; s.dict = proj.arucoDict; s.fovDeg = fovDeg;
            if (calibrated) { s.K = cv::Matx33d(proj.calibK[0], 0, proj.calibK[2], 0, proj.calibK[1], proj.calibK[3], 0, 0, 1); s.dist = proj.calibDist; s.calibWidth = proj.calibW; }
            at = std::make_unique<ArucoTrack::Tracker>(s);
            for (size_t i = 0; i < proj.markers.size(); ++i) arucoToMarker[proj.markers[i].arucoId] = (int)i;
        } else if (surfaces) {
            SurfaceTrack::Settings s; s.fovDeg = fovDeg;
            if (first) { s.tiltDeg = first->webcamTilt; s.height = first->webcamHeight; s.facing = first->webcamSurface == 2; }
            surfBase = s; planes.clear();
            st = std::make_unique<SurfaceTrack::Tracker>(s);   // (the first floor's settings; planes[] does the tracking)
        } else {
            MarkerTrack::Settings s; s.fovDeg = fovDeg;
            if (calibrated) { s.K = cv::Matx33d(proj.calibK[0], 0, proj.calibK[2], 0, proj.calibK[1], proj.calibK[3], 0, 0, 1); s.dist = proj.calibDist; s.calibWidth = proj.calibW; }
            mt = std::make_unique<MarkerTrack::Tracker>(s);
            for (size_t i = 0; i < proj.markers.size(); ++i) {
                const Scene::Marker& m = proj.markers[i];
                if (m.type == Scene::MK_BARCODE) barToMarker[m.barcode] = (int)i;
                else if (!m.patt.empty()) { const int id = mt->addPatternFile(m.patt); if (id >= 0) pattToMarker[id] = (int)i; }
            }
        }
    };

    struct Seen { bool on = false, was = false; double at = -1; float view[16]; };
    std::vector<Seen> seen;
    float P[16]; for (float& v : P) v = 0;
    int sceneIndex = 0, pendingScene = -1;
    std::string problem; double problemUntil = 0;
    std::ofstream logFile;
    { std::error_code le; logFile.open(fs::u8path("log.txt"), std::ios::binary | std::ios::trunc); (void)le; }
    const auto t0 = std::chrono::steady_clock::now();
    auto clock = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    auto startScene = [&](int i) {
        if (i != 0) {
            Project next; std::string e2;
            if (!Scene::fromJson(readFile(sceneFile(i)), next, e2)) { problem = "loadScene: " + sceneFile(i) + ": " + e2; problemUntil = clock() + 6; return; }
            proj = next;
        } else if (sceneIndex != 0) { Project next; std::string e2; if (Scene::fromJson(readFile(sceneFile(0)), next, e2)) proj = next; }
        sceneIndex = i;
        seen.assign(proj.markers.size(), Seen());
        makeTrackers();
        Scripts::Env env;
        env.proj = &proj; env.projection = P; env.sceneIndex = i;
        env.anchorView = [&](int a, float v[16]) { if (a < 0 || a >= (int)seen.size() || !seen[a].on) return false; std::memcpy(v, seen[a].view, 16 * sizeof(float)); return true; };
        env.surface = [&](float v[16]) { const int a = anchorPlane(); if (a < 0 || !planes[a].on) return false; std::memcpy(v, planes[a].view, 16 * sizeof(float)); return true; };
        env.planes = [&]() { std::vector<Scripts::Env::Plane> v; for (const auto& p : planes) if (p.on) { Scripts::Env::Plane q; q.id = p.id; q.vertical = p.vertical; std::memcpy(q.view, p.view, sizeof q.view); v.push_back(q); } return v; };
        env.loadScene = [&](int n) { pendingScene = n; };
        env.log = [&](const std::string& level, const std::string& text) {
            std::fprintf(stderr, "%s%s\n", level == "error" ? "Problem: " : "", text.c_str()); std::fflush(stderr);
            if (logFile) { logFile << (level == "error" ? "Problem: " : "") << text << "\n"; logFile.flush(); }
            if (level == "error") { problem = text; problemUntil = clock() + 8; }
        };
        std::string se;
        if (!Scripts::start(env, se)) { problem = se; problemUntil = clock() + 8; }
        Scripts::seen(-1, true);   // the Main Camera's things: always there
    };
    startScene(0);

    unsigned bgTex = 0; int vidW = 1280, vidH = 720;
    cv::Mat frame; long long frameNo = 0;
    bool lockWanted = false, fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0, planesSent = false;
    int downX = -1, downY = -1; bool tapped = false; float tapX = 0, tapY = 0; int tapPx = -1, tapPy = -1;
    std::string lastTitle, shownHint;
    bool running = true, shotDone = false, testTapDone = false;
    double last = clock();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                else if (e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_h) { lockWanted = true; lockKind = 0; }
                else if (e.key.keysym.sym == SDLK_v) { lockWanted = true; lockKind = 1; }
                else if (e.key.keysym.sym == SDLK_r && surfaces) { planes.clear(); planesSent = false; Scripts::planesChanged("{\"added\":[],\"updated\":[],\"removed\":[]}"); }
                else if (e.key.keysym.sym == SDLK_F11) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { downX = e.button.x; downY = e.button.y; }
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                // a tap (not a drag, not on the UI): locks a surface first, then it's the game's
                if (downX >= 0 && std::hypot(e.button.x - downX, e.button.y - downY) <= 8 && !Ui::wantsMouse()) {
                    if (surfaces && planes.empty()) { lockWanted = true; lockKind = 0; }
                    else { tapped = true; tapPx = e.button.x; tapPy = e.button.y; }
                }
                downX = downY = -1;
            }
        }
        const double t = clock(), dt = std::min(0.1, t - last); last = t;
        if (lockAfter >= 0 && t >= lockAfter && surfaces && planes.empty()) { lockWanted = true; lockKind = 0; }
        if (lockVAfter >= 0 && t >= lockVAfter && surfaces && planes.size() == 1) { lockWanted = true; lockKind = 1; }
        if (testTap.size() == 3 && !testTapDone && t >= testTap[2]) {   // a test's tap (--tap x,y,seconds: -1..1 across the window)
            int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
            tapped = true; tapPx = (int)((testTap[0] * 0.5 + 0.5) * ww); tapPy = (int)((0.5 - testTap[1] * 0.5) * wh); testTapDone = true;
        }
        std::string hint;
        if (cam.get(frame, frameNo)) {
            vidW = frame.cols; vidH = frame.rows;
            bgTex = Viewer::uploadBGR(bgTex, frame.data, frame.cols, frame.rows);
            std::vector<bool> now(seen.size(), false);
            auto found = [&](int mi, const cv::Matx33d& R, const cv::Vec3d& tv, bool markerAxes) {
                if (mi < 0 || mi >= (int)seen.size()) return;
                toView(R, tv, markerAxes, seen[mi].view); seen[mi].on = true; seen[mi].at = t; now[mi] = true;
            };
            if (mt) for (const auto& f : mt->detect(frame)) {
                const auto& map = f.barcode ? barToMarker : pattToMarker;
                const auto it = map.find(f.id); if (it != map.end()) found(it->second, f.R, f.t, true);
            }
            if (at) for (const auto& f : at->detect(frame)) { const auto it = arucoToMarker.find(f.id); if (it != arucoToMarker.end()) found(it->second, f.R, f.t, true); }
            if (surfaces) {
                if (lockWanted) {   // a new plane where the camera looks now
                    lockWanted = false;
                    Plane p; SurfaceTrack::Settings s = surfBase; s.facing = lockKind == 1; p.vertical = lockKind == 1;
                    p.tr = std::make_unique<SurfaceTrack::Tracker>(s);
                    if (p.tr->lock(frame)) { p.id = std::string(p.vertical ? "wall " : "floor ") + std::to_string(planes.size() + 1); planes.push_back(std::move(p)); }
                    else { problem = "Not enough detail there to lock on: try a more textured surface"; problemUntil = t + 4; }
                }
                for (auto& p : planes) {
                    SurfaceTrack::Pose sp;
                    if (p.tr->track(frame, sp)) { toView(sp.R, sp.t, false, p.view); p.on = true; p.at = t; }
                    else if (p.on && t - p.at > 0.35) p.on = false;
                }
                const int a = anchorPlane();
                if (a >= 0 && planes[a].on) for (size_t i = 0; i < seen.size(); ++i) { std::memcpy(seen[i].view, planes[a].view, sizeof seen[i].view); seen[i].on = true; seen[i].at = t; now[i] = true; }
            }
            for (size_t i = 0; i < seen.size(); ++i) if (!now[i] && seen[i].on && t - seen[i].at > 0.35) seen[i].on = false;   // tracking flickers: a moment where it last was
        }
        // the projection: the camera's lens, as much of its picture as fills the window
        int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
        const double fx = calibrated ? proj.calibK[0] * vidW / proj.calibW : (vidW / 2.0) / std::tan(fovDeg * 3.14159265358979 / 360.0);
        const double fy = calibrated ? proj.calibK[1] * vidW / proj.calibW : fx;
        const double cx = calibrated ? proj.calibK[2] * vidW / proj.calibW : vidW / 2.0, cy = calibrated ? proj.calibK[3] * vidW / proj.calibW : vidH / 2.0;
        float rect[4];
        projection(fx, fy, cx, cy, vidW, vidH, w, h, std::max(0.005f, proj.camNear), std::max(proj.camNear + 1.0f, proj.camFar), P, rect);
        // anchors found and lost: their objects' onFound / onLost; Surfaces: the plane, once (onPlanesChanged)
        for (size_t i = 0; i < seen.size(); ++i) if (seen[i].on != seen[i].was) { seen[i].was = seen[i].on; Scripts::seen((int)i, seen[i].on); }
        for (auto& p : planes) if (!p.sent && p.on) {   // a new plane: the scripts' onPlanesChanged
            p.sent = true; planesSent = true;
            const float* V = p.view;
            char b[320]; std::snprintf(b, sizeof b, "{\"added\":[{\"id\":\"%s\",\"alignment\":\"%s\",\"center\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"size\":{\"x\":1.5,\"y\":1.5},\"boundary\":[]}],\"updated\":[],\"removed\":[]}",
                                             p.id.c_str(), p.vertical ? "vertical" : "horizontal", V[12], V[13], V[14]);
            Scripts::planesChanged(b);
        }
        {   // the planes' look (the first anchor's AR Plane Manager: Show planes, its colour; walls in blue)
            std::vector<Viewer::ArPlane> look;
            const Scene::Marker* pm = proj.markers.empty() ? nullptr : &proj.markers[0];
            if (!pm || pm->showPlanes)
                for (const auto& p : planes) if (p.on) {
                    Viewer::ArPlane a; std::memcpy(a.view, p.view, sizeof a.view); a.size = 1.5f;
                    const float floorC[4] = {pm ? pm->planeColour[0] : 0.25f, pm ? pm->planeColour[1] : 0.95f, pm ? pm->planeColour[2] : 0.55f, 0.6f}, wallC[4] = {0.2f, 0.66f, 0.89f, 0.6f};
                    std::memcpy(a.colour, p.vertical ? wallC : floorC, sizeof a.colour);
                    look.push_back(a);
                }
            Viewer::setArPlanes(look);
        }
        // a tap: where (-1..1), and on which object (the nearest whose bounds hold it)
        if (tapped) {
            int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
            tapX = (float)tapPx / ww * 2 - 1; tapY = 1 - (float)tapPy / wh * 2;
            int best = -1; float bestZ = 1e9f;
            for (size_t a = 0; a < seen.size(); ++a) {
                if (!seen[a].on) continue;
                for (const auto& o : proj.markers[a].objects) {
                    if (!o.active || o.kind == Scene::EMPTY || !o.lookOn) continue;
                    float W[16], Pm[16], M[16]; if (!Viewer::matrices(o.id, W, Pm)) continue;
                    const float* V = seen[a].view;
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) M[c * 4 + r] = V[r] * W[c * 4] + V[4 + r] * W[c * 4 + 1] + V[8 + r] * W[c * 4 + 2] + V[12 + r] * W[c * 4 + 3];
                    const float z = M[14]; if (z >= -0.01f) continue;
                    float rad = 0; for (int c = 0; c < 3; ++c) rad = std::max(rad, std::sqrt(M[c * 4] * M[c * 4] + M[c * 4 + 1] * M[c * 4 + 1] + M[c * 4 + 2] * M[c * 4 + 2]));
                    rad *= 0.6f;
                    const float sx = (P[0] * M[12] + P[8] * z) / -z, sy = (P[5] * M[13] + P[9] * z) / -z;
                    const float rx = rad * P[0] / -z, ry = rad * P[5] / -z;
                    const float dx = (tapX - sx) / std::max(1e-4f, rx), dy = (tapY - sy) / std::max(1e-4f, ry);
                    if (dx * dx + dy * dy <= 1 && -z < bestZ) { bestZ = -z; best = o.id; }
                }
            }
            if (best >= 0) Scripts::tap(best);
        }
        Scripts::frame(dt, t, tapped, tapX, tapY);
        tapped = false;
        if (pendingScene >= 0) { const int n = pendingScene; pendingScene = -1; planesSent = false; startScene(n); continue; }

        std::vector<Viewer::ArAnchor> anchors;
        for (size_t i = 0; i < seen.size(); ++i) if (seen[i].on) { Viewer::ArAnchor a; a.marker = (int)i; std::memcpy(a.view, seen[i].view, sizeof a.view); anchors.push_back(a); }
        Viewer::renderAR(proj, anchors, P, bgTex, rect, w, h, (float)t);
        Viewer::present(w, h);
        // the UI over it
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        std::vector<bool> on(seen.size()); for (size_t i = 0; i < seen.size(); ++i) on[i] = seen[i].on;
        Ui::draw(proj, on);
        if (surfaces) {   // what to press, along the bottom
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const float fs = std::max(14.0f, ds.y / 45);
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, shownHint.c_str());
            fg->AddRectFilled(ImVec2((ds.x - ts.x) / 2 - 10, ds.y - ts.y - 22), ImVec2((ds.x + ts.x) / 2 + 10, ds.y - 10), IM_COL32(0x1a, 0x1a, 0x1a, 200), 4);
            fg->AddText(ImGui::GetFont(), fs, ImVec2((ds.x - ts.x) / 2, ds.y - ts.y - 16), IM_COL32(0xe6, 0xe6, 0xe6, 255), shownHint.c_str());
        }
        ImGui::Render();
        glViewport(0, 0, w, h);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // the title says what's going on
        std::string names;
        for (size_t i = 0; i < seen.size(); ++i) if (seen[i].on) names += (names.empty() ? "" : ", ") + proj.markers[i].name;
        int planesOn = 0, walls = 0; for (const auto& p : planes) { if (p.on) ++planesOn; if (p.vertical) ++walls; }
        if (surfaces && planes.empty()) hint = "Point at a table or the floor and press Space (or click); at a wall and press V";
        else if (surfaces) hint = std::to_string(planesOn) + " of " + std::to_string(planes.size()) + " planes seen (" + std::to_string(walls) + " wall" + (walls == 1 ? "" : "s") + ")   Space: a floor or table   V: a wall   R: start again   click: the game";
        else if (names.empty()) hint = aruco ? "Point the camera at an ArUco marker" : "Point the camera at a marker";
        else hint = "Found: " + names;
        if (!problem.empty() && t < problemUntil) hint = "Problem: " + problem;
        shownHint = hint;   // (drawn next frame: the UI is drawn before this is worked out)
        const std::string title = proj.title + "  -  " + hint;
        if (title != lastTitle) { SDL_SetWindowTitle(win, title.c_str()); lastTitle = title; }

        if (!shot.empty() && !shotDone && t >= after) {   // the window as shown (the UI too)
            std::vector<unsigned char> px((size_t)w * h * 4), flip(px.size());
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < h; ++y) std::memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
            stbi_write_png(shot.c_str(), w, h, 4, flip.data(), w * 4);
            std::printf("shot %s: %s\n", shot.c_str(), hint.c_str()); std::fflush(stdout);
            shotDone = true; running = false;
        }
        SDL_GL_SwapWindow(win);
    }
    Scripts::stop();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
