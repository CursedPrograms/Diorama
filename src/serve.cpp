// Diorama - the Build And Run server (see serve.h). Plain HTTP/1.0 GETs, one thread a request.
#include "serve.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET Sock;
static void closeSock(Sock s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Sock;
#define INVALID_SOCKET (-1)
static void closeSock(Sock s) { close(s); }
#endif

namespace fs = std::filesystem;

namespace Serve {

static std::mutex mtx;
static std::string root;
static std::atomic<bool> running{false};
static Sock listener = INVALID_SOCKET;
static int thePort = 0;
int port() { return thePort; }

static const char* typeOf(const std::string& ext) {
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".glb") return "model/gltf-binary";
    if (ext == ".gltf") return "model/gltf+json";
    if (ext == ".css") return "text/css";
    if (ext == ".svg") return "image/svg+xml";
    return "application/octet-stream";
}

static void answer(Sock c) {
    char buf[4096];
    const int n = recv(c, buf, sizeof buf - 1, 0);
    if (n <= 0) { closeSock(c); return; }
    buf[n] = 0;
    std::string req(buf), path = "/";
    if (req.rfind("GET ", 0) == 0) { const size_t e = req.find(' ', 4); path = req.substr(4, e - 4); }
    const size_t q = path.find('?'); if (q != std::string::npos) path.resize(q);
    // %20 and friends
    std::string dec;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) { dec += (char)strtol(path.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else dec += path[i];
    }
    if (dec.empty() || dec.back() == '/') dec += "index.html";
    std::string base; { std::lock_guard<std::mutex> g(mtx); base = root; }
    std::error_code ec;
    const fs::path file = fs::weakly_canonical(fs::u8path(base) / fs::u8path(dec.substr(1)), ec);
    const fs::path top = fs::weakly_canonical(fs::u8path(base), ec);
    std::string body, head;
    const bool inside = file.u8string().rfind(top.u8string(), 0) == 0;
    std::ifstream f(file, std::ios::binary);
    if (inside && f) {
        std::stringstream ss; ss << f.rdbuf(); body = ss.str();
        head = "HTTP/1.0 200 OK\r\nContent-Type: " + std::string(typeOf(file.extension().u8string())) + "\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    } else {
        body = "Not found";
        head = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\nConnection: close\r\n\r\n";
    }
    send(c, head.data(), (int)head.size(), 0);
    for (size_t off = 0; off < body.size();) { const int s = send(c, body.data() + off, (int)std::min<size_t>(1 << 16, body.size() - off), 0); if (s <= 0) break; off += (size_t)s; }
    closeSock(c);
}

bool start(const std::string& folder, int p) {
    { std::lock_guard<std::mutex> g(mtx); root = folder; }
    if (running) return true;
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) return false;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)p); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (sockaddr*)&a, sizeof a) != 0 || listen(listener, 16) != 0) { closeSock(listener); listener = INVALID_SOCKET; return false; }
    thePort = p; running = true;
    std::thread([] {
        while (running) {
            Sock c = accept(listener, nullptr, nullptr);
            if (c == INVALID_SOCKET) break;
            std::thread(answer, c).detach();
        }
    }).detach();
    return true;
}

void stop() { running = false; if (listener != INVALID_SOCKET) { closeSock(listener); listener = INVALID_SOCKET; } }

}
