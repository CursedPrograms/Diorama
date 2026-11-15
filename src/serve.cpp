// Diorama - the Build And Run server (see serve.h). Plain HTTP/1.0 GETs, one thread a request.
#include "serve.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "json.h"

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
static std::vector<Log> logs;
std::vector<Log> takeLogs() { std::lock_guard<std::mutex> g(mtx); std::vector<Log> v; v.swap(logs); return v; }
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
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".patt" || ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

static void answer(Sock c) {
    std::string req;
    char buf[8192];
    int n = recv(c, buf, sizeof buf, 0);
    if (n <= 0) { closeSock(c); return; }
    req.assign(buf, n);
    // a POST's body may come after the headers in another read
    const size_t he = req.find("\r\n\r\n");
    if (req.rfind("POST ", 0) == 0 && he != std::string::npos) {
        size_t len = 0; const size_t cl = req.find("Content-Length:");
        if (cl != std::string::npos) len = (size_t)atol(req.c_str() + cl + 15);
        while (req.size() < he + 4 + len && (n = recv(c, buf, sizeof buf, 0)) > 0) req.append(buf, n);
        if (req.rfind("POST /__log", 0) == 0) {
            Json::Value v;
            if (Json::parse(req.substr(he + 4), v)) {
                Log l; l.level = v.str("level", "log"); l.file = v.str("file"); l.text = v.str("text"); l.line = (int)v.num("line", 0);
                std::lock_guard<std::mutex> g(mtx); logs.push_back(l); if (logs.size() > 2000) logs.erase(logs.begin());
            }
        }
        const std::string ok = "HTTP/1.0 204 No Content\r\nConnection: close\r\n\r\n";
        send(c, ok.data(), (int)ok.size(), 0); closeSock(c); return;
    }
    std::string path = "/";
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
        std::string ext = file.extension().u8string(); for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
        const size_t total = body.size();
        // a range (videos seek with them)
        size_t from = 0, to = total ? total - 1 : 0; bool ranged = false;
        const size_t rg = req.find("\r\nRange: bytes=");
        if (rg != std::string::npos && total) {
            const std::string v = req.substr(rg + 15, req.find("\r\n", rg + 2) - rg - 15);
            const size_t d = v.find('-');
            if (d != std::string::npos) {
                if (d > 0) from = (size_t)atoll(v.substr(0, d).c_str());
                if (d + 1 < v.size()) to = (std::min)(to, (size_t)atoll(v.substr(d + 1).c_str()));
                ranged = from <= to && from < total;
            }
        }
        if (ranged) body = body.substr(from, to - from + 1); else from = 0;
        head = std::string(ranged ? "HTTP/1.1 206 Partial Content" : "HTTP/1.1 200 OK") + "\r\nContent-Type: " + typeOf(ext) +
               "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nAccept-Ranges: bytes\r\n" +
               (ranged ? "Content-Range: bytes " + std::to_string(from) + "-" + std::to_string(from + body.size() - 1) + "/" + std::to_string(total) + "\r\n" : std::string()) +
               "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    } else {
        body = "Not found";
        head = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\nConnection: close\r\n\r\n";
    }
    send(c, head.data(), (int)head.size(), 0);
    for (size_t off = 0; off < body.size();) { const int s = send(c, body.data() + off, (int)std::min<size_t>(1 << 16, body.size() - off), 0); if (s <= 0) break; off += (size_t)s; }
    // a graceful close: closing at once can reset the connection and lose the end of a big file (OpenCV.js is 10 MB)
#ifdef _WIN32
    shutdown(c, SD_SEND);
#else
    shutdown(c, SHUT_WR);
#endif
    { char drain[512]; while (recv(c, drain, sizeof drain, 0) > 0) {} }
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
