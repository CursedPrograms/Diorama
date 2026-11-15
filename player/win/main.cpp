// Diorama's Windows player: a game made with Diorama, as a Windows program.
// The exe sits beside a "game" folder (the game Diorama built, the same as its website). It serves that folder on
// this PC only (127.0.0.1, a free port), shows it in WebView2 (Microsoft Edge's engine, part of Windows 11),
// lets it use the webcam (and the location, when the game asks for it), and lets sound and video start by themselves.
// F11: full screen.   Diorama copies this exe as <Game>.exe and gives it the game's icon.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wrl.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static HWND window;
static ComPtr<ICoreWebView2Controller> controller;
static ComPtr<ICoreWebView2> web;
static std::wstring gameDir, title = L"Diorama game";
static int port = 0;
static bool dev = false, fullscreen = false;
static std::wstring fakeCamera, shotFile;   // testing: a video file as the webcam; a picture of the window, then quit
static WINDOWPLACEMENT placement = {sizeof(WINDOWPLACEMENT)};

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static std::string readFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
// a string from game.json ({"title": "..."}): enough JSON for the player's few settings
static std::string jsonString(const std::string& json, const std::string& key) {
    const size_t k = json.find("\"" + key + "\""); if (k == std::string::npos) return "";
    size_t i = json.find(':', k); if (i == std::string::npos) return "";
    i = json.find('"', i); if (i == std::string::npos) return "";
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) { if (json[i] == '\\' && i + 1 < json.size()) ++i; out += json[i]; }
    return out;
}

// ---- the little web server: the game folder, on this PC only ----
static const char* typeOf(const std::wstring& path) {
    std::wstring e = path.substr(path.find_last_of(L'.') == std::wstring::npos ? path.size() : path.find_last_of(L'.'));
    for (auto& c : e) c = (wchar_t)towlower(c);
    static const std::pair<const wchar_t*, const char*> types[] = {
        {L".html", "text/html; charset=utf-8"}, {L".js", "text/javascript; charset=utf-8"}, {L".mjs", "text/javascript; charset=utf-8"},
        {L".css", "text/css; charset=utf-8"}, {L".json", "application/json"}, {L".png", "image/png"}, {L".jpg", "image/jpeg"},
        {L".jpeg", "image/jpeg"}, {L".gif", "image/gif"}, {L".svg", "image/svg+xml"}, {L".webp", "image/webp"}, {L".ico", "image/x-icon"},
        {L".glb", "model/gltf-binary"}, {L".gltf", "model/gltf+json"}, {L".bin", "application/octet-stream"}, {L".wasm", "application/wasm"},
        {L".mp3", "audio/mpeg"}, {L".ogg", "audio/ogg"}, {L".wav", "audio/wav"}, {L".m4a", "audio/mp4"},
        {L".mp4", "video/mp4"}, {L".m4v", "video/mp4"}, {L".webm", "video/webm"}, {L".mov", "video/quicktime"},
        {L".mind", "application/octet-stream"}, {L".patt", "text/plain"}, {L".dat", "application/octet-stream"}, {L".txt", "text/plain"}};
    for (const auto& t : types) if (e == t.first) return t.second;
    return "application/octet-stream";
}
static std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}
static void sendAll(SOCKET c, const char* p, size_t n) { while (n) { const int k = send(c, p, (int)std::min<size_t>(n, 1 << 20), 0); if (k <= 0) return; p += k; n -= k; } }
static void answer(SOCKET c) {
    std::string req; char buf[8192];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) { const int k = recv(c, buf, sizeof buf, 0); if (k <= 0) break; req.append(buf, k); }
    std::string path = "/";
    { const size_t a = req.find(' '), b = a == std::string::npos ? a : req.find(' ', a + 1); if (b != std::string::npos) path = req.substr(a + 1, b - a - 1); }
    path = unescape(path.substr(0, path.find('?')));
    if (path.empty() || path.back() == '/') path += "index.html";
    // inside the game folder only
    std::wstring rel = widen(path.substr(1)); std::replace(rel.begin(), rel.end(), L'/', L'\\');
    wchar_t full[MAX_PATH * 2]; GetFullPathNameW((gameDir + L"\\" + rel).c_str(), MAX_PATH * 2, full, nullptr);
    const std::wstring file = full;
    HANDLE h = file.rfind(gameDir, 0) == 0 ? CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr) : INVALID_HANDLE_VALUE;
    LARGE_INTEGER size{};
    if (h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h, &size) || GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_DIRECTORY) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot found";
        sendAll(c, nf, strlen(nf)); return;
    }
    // ranges: video seeks
    long long from = 0, to = size.QuadPart - 1; bool ranged = false;
    { const size_t r = req.find("\r\nRange: bytes="); if (r != std::string::npos) {
        const std::string v = req.substr(r + 15, req.find("\r\n", r + 2) - r - 15);
        const size_t d = v.find('-');
        if (d != std::string::npos) { if (d > 0) from = _atoi64(v.substr(0, d).c_str()); if (d + 1 < v.size()) to = std::min(to, _atoi64(v.substr(d + 1).c_str())); ranged = from <= to; }
    } }
    if (!ranged) { from = 0; to = size.QuadPart - 1; }
    char head[512];
    snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\nAccept-Ranges: bytes\r\n%sCache-Control: no-cache\r\nConnection: close\r\n\r\n",
             ranged ? "206 Partial Content" : "200 OK", typeOf(file), to - from + 1,
             ranged ? ("Content-Range: bytes " + std::to_string(from) + "-" + std::to_string(to) + "/" + std::to_string(size.QuadPart) + "\r\n").c_str() : "");
    sendAll(c, head, strlen(head));
    LARGE_INTEGER at; at.QuadPart = from; SetFilePointerEx(h, at, nullptr, FILE_BEGIN);
    std::vector<char> chunk(1 << 20);
    for (long long left = to - from + 1; left > 0;) {
        DWORD got = 0; if (!ReadFile(h, chunk.data(), (DWORD)std::min<long long>(left, (long long)chunk.size()), &got, nullptr) || !got) break;
        sendAll(c, chunk.data(), got); left -= got;
    }
    CloseHandle(h);
}
static bool startServer() {
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // the same port each run (the browser keeps the game's saved data per address), else any free one
    unsigned hash = 2166136261u; for (wchar_t ch : title) hash = (hash ^ ch) * 16777619u;
    a.sin_port = htons((u_short)(41000 + hash % 8000));
    if (bind(s, (sockaddr*)&a, sizeof a)) { a.sin_port = 0; if (bind(s, (sockaddr*)&a, sizeof a)) return false; }
    int len = sizeof a; getsockname(s, (sockaddr*)&a, &len); port = ntohs(a.sin_port);
    if (listen(s, 64)) return false;
    std::thread([s] { for (;;) { SOCKET c = accept(s, nullptr, nullptr); if (c == INVALID_SOCKET) continue; std::thread([c] {
        DWORD ms = 10000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
        answer(c);
        // a graceful close: closing at once can reset the connection and lose the end of what was sent
        shutdown(c, SD_SEND); char drain[512]; while (recv(c, drain, sizeof drain, 0) > 0) {}
        closesocket(c);
    }).detach(); } }).detach();
    return true;
}

// ---- the window ----
static void fit() { if (!controller) return; RECT r; GetClientRect(window, &r); controller->put_Bounds(r); }
static void setFullscreen(bool on) {
    if (on == fullscreen) return;
    fullscreen = on;
    if (on) {
        GetWindowPlacement(window, &placement);
        MONITORINFO mi = {sizeof mi}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(window, &placement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}
static std::wstring dataFolder() {   // %LOCALAPPDATA%\<Game> : the browser engine's own files (saves, permissions)
    wchar_t p[MAX_PATH]; SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p);
    std::wstring t = title; for (auto& c : t) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    const std::wstring d = std::wstring(p) + L"\\" + t; CreateDirectoryW(d.c_str(), nullptr); return d;
}
static void startPage() {
    ComPtr<ICoreWebView2Settings> st;
    if (SUCCEEDED(web->get_Settings(&st))) {
        st->put_AreDevToolsEnabled(dev); st->put_AreDefaultContextMenusEnabled(dev); st->put_IsStatusBarEnabled(FALSE);
        st->put_IsZoomControlEnabled(FALSE);
    }
    // the webcam, the microphone and the location: the game's own, no question each time
    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            COREWEBVIEW2_PERMISSION_KIND kind; args->get_PermissionKind(&kind);
            if (kind == COREWEBVIEW2_PERMISSION_KIND_CAMERA || kind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE ||
                kind == COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION || kind == COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY)
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
            return S_OK;
        }).Get(), nullptr);
    // the page's title is the window's (each scene can name itself)
    web->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
        [](ICoreWebView2* sender, IUnknown*) -> HRESULT {
            LPWSTR t = nullptr; if (SUCCEEDED(sender->get_DocumentTitle(&t)) && t) { if (*t && wcsncmp(t, L"127.0.0.1", 9)) SetWindowTextW(window, t); CoTaskMemFree(t); }
            return S_OK;
        }).Get(), nullptr);
    // links out of the game open in the real browser
    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr; args->get_Uri(&uri);
            if (uri) { ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL); CoTaskMemFree(uri); }
            args->put_Handled(TRUE); return S_OK;
        }).Get(), nullptr);
    web->add_ContainsFullScreenElementChanged(Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
        [](ICoreWebView2* sender, IUnknown*) -> HRESULT { BOOL f = FALSE; sender->get_ContainsFullScreenElement(&f); setFullscreen(f != FALSE); return S_OK; }).Get(), nullptr);
    controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
        [](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
            UINT key = 0; COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_VirtualKey(&key); args->get_KeyEventKind(&kind);
            if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && key == VK_F11) { args->put_Handled(TRUE); setFullscreen(!fullscreen); }
            if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && key == VK_ESCAPE && fullscreen) { args->put_Handled(TRUE); setFullscreen(false); }
            return S_OK;
        }).Get(), nullptr);
    web->Navigate((L"http://127.0.0.1:" + std::to_wstring(port) + L"/").c_str());
    if (!shotFile.empty()) SetTimer(window, 1, 9000, nullptr);
    controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
static void startWebView() {
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    std::wstring argsLine = L"--autoplay-policy=no-user-gesture-required";
    if (!fakeCamera.empty()) argsLine += L" --use-fake-device-for-media-stream --use-file-for-fake-video-capture=\"" + fakeCamera + L"\"";
    if (dev) argsLine += L" --remote-debugging-port=9335";   // --dev: DevTools from outside too (http://127.0.0.1:9335)
    options->put_AdditionalBrowserArguments(argsLine.c_str());
    const HRESULT started = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataFolder().c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) { MessageBoxW(window, L"Windows' WebView2 runtime didn't start.", title.c_str(), MB_ICONERROR); return result; }
                return env->CreateCoreWebView2Controller(window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT made, ICoreWebView2Controller* created) -> HRESULT {
                        if (FAILED(made)) return made;
                        controller = created; controller->get_CoreWebView2(&web);
                        // black behind the page while it loads, not white
                        ComPtr<ICoreWebView2Controller2> c2; if (SUCCEEDED(controller.As(&c2))) c2->put_DefaultBackgroundColor({255, 17, 17, 17});
                        fit(); startPage(); return S_OK;
                    }).Get());
            }).Get());
    if (FAILED(started)) {
        MessageBoxW(window, L"This game needs Microsoft's WebView2 runtime (it comes with Windows 11; for Windows 10: microsoft.com/edge/webview2).", title.c_str(), MB_ICONERROR);
        PostQuitMessage(1);
    }
}
static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: fit(); return 0;
    case WM_SETFOCUS: if (controller) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); return 0;
    case WM_TIMER:   // --shot: the window's picture, then quit
        KillTimer(hwnd, wp);
        if (web) {
            IStream* st = nullptr;
            if (SUCCEEDED(SHCreateStreamOnFileEx(shotFile.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &st)))
                web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, st, Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                    [st](HRESULT) -> HRESULT { st->Release(); PostQuitMessage(0); return S_OK; }).Get());
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe; dir = dir.substr(0, dir.find_last_of(L'\\'));
    gameDir = dir + L"\\game";
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--dev")) dev = true;                                   // F12 tools, right-click menu
        else if (!wcscmp(argv[i], L"--game") && i + 1 < argc) gameDir = argv[++i];    // another game folder
        else if (!wcscmp(argv[i], L"--fake-camera") && i + 1 < argc) fakeCamera = argv[++i];
        else if (!wcscmp(argv[i], L"--shot") && i + 1 < argc) shotFile = argv[++i];
    }
    LocalFree(argv);
    { wchar_t f[MAX_PATH * 2]; GetFullPathNameW(gameDir.c_str(), MAX_PATH * 2, f, nullptr); gameDir = f; }
    if (GetFileAttributesW((gameDir + L"\\index.html").c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, (L"The game's files aren't here:\n" + gameDir + L"\n\nKeep the \"game\" folder beside this program.").c_str(), L"Diorama game", MB_ICONERROR);
        return 1;
    }
    const std::string json = readFile(gameDir + L"\\game.json");
    { const std::string t = jsonString(json, "title"); if (!t.empty()) title = widen(t); else { std::wstring n = exe; n = n.substr(n.find_last_of(L'\\') + 1); title = n.substr(0, n.size() - 4); } }
    if (!startServer()) { MessageBoxW(nullptr, L"Couldn't start the game's local server.", title.c_str(), MB_ICONERROR); return 1; }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(17, 17, 17)); wc.lpszClassName = L"DioramaGame";
    RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance, nullptr);
    if (!shotFile.empty()) SetWindowPos(window, nullptr, -4000, -4000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);   // tests: off the screen
    ShowWindow(window, shotFile.empty() ? show : SW_SHOWNOACTIVATE);
    if (jsonString(json, "fullscreen") == "yes") setFullscreen(true);
    startWebView();
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
