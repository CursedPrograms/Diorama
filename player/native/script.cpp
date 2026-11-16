// Diorama's native player: the scripts' host (QuickJS) and __dio, the C++ scene seen from runtime.js. See script.h.
#include "script.h"
#include "viewer.h"
#include "runtime_js.h"   // RUNTIME_JS: player/native/runtime.js (made by CMake)

#include <quickjs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using Scene::Object;

namespace Scripts {

static Env E;
static JSRuntime* rt = nullptr;
static JSContext* ctx = nullptr;
static const int CAMERA_ROOT = -1000;

// ---- the objects: each in an anchor's list (-1: the Main Camera's)
struct Where { Scene::Marker* list = nullptr; int anchor = 0; };
static Scene::Marker* listOf(int anchor) { return anchor < 0 ? &E.proj->camera : &E.proj->markers[anchor]; }
static Object* object(int id, Where* w = nullptr) {
    for (int a = -1; a < (int)E.proj->markers.size(); ++a) {
        Scene::Marker* m = listOf(a);
        for (auto& o : m->objects) if (o.id == id) { if (w) { w->list = m; w->anchor = a; } return &o; }
    }
    return nullptr;
}
static int rootOf(int anchor) { return anchor < 0 ? CAMERA_ROOT : -(anchor + 1); }
static int anchorOfRoot(int root) { return root == CAMERA_ROOT ? -1 : -root - 1; }

// ---- small maths (column-major 4x4)
static void mul(const float* a, const float* b, float* o) {
    float r[16];
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
    std::memcpy(o, r, sizeof r);
}
static bool invert(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}
static void identity(float* m) { std::memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1; }
// the object's parent's frame -> the camera (false: its anchor isn't seen)
static bool parentToCamera(int id, float out[16]) {
    Where w; Object* o = object(id, &w); if (!o) return false;
    float V[16];
    if (w.anchor < 0) identity(V);   // the Main Camera's things: in the camera's own frame
    else if (!E.anchorView || !E.anchorView(w.anchor, V)) return false;
    float P[16]; identity(P);
    if (o->parent != -1) { float wm[16], pm[16]; if (Viewer::matrices(o->parent, wm, pm)) std::memcpy(P, wm, sizeof P); }
    mul(V, P, out);
    return true;
}

// ---- fields, by the names the web game's scene uses
static const char* uiName(int ui) { return ui > 0 && ui < Scene::UI_COUNT ? Scene::UI_NAMES[ui] : ""; }
struct Arr { float* f = nullptr; int n = 0; std::vector<float>* v = nullptr; };
static Arr arrOf(Object& o, const std::string& k) {
    Arr a;
    if (k == "position") { a.f = o.pos; a.n = 3; } else if (k == "rotation") { a.f = o.rot; a.n = 3; } else if (k == "scale") { a.f = o.scl; a.n = 3; }
    else if (k == "colour") { a.f = o.colour; a.n = 4; } else if (k == "emissive") { a.f = o.emissive; a.n = 3; }
    else if (k == "uiRect") { a.f = o.uiRect; a.n = 4; } else if (k == "uiColour") { a.f = o.uiColour; a.n = 4; }
    else if (k == "lightColour") { a.f = o.lightColour; a.n = 3; } else if (k == "pColourStart") { a.f = o.pColourStart; a.n = 4; }
    else if (k == "pColourEnd") { a.f = o.pColourEnd; a.n = 4; } else if (k == "lineColour") { a.f = o.lineColour; a.n = 4; }
    else if (k == "linePoints") a.v = &o.linePoints;
    return a;
}
static float* floatOf(Object& o, const std::string& k) {
    static const std::map<std::string, float Object::*> F = {
        {"metallic", &Object::metallic}, {"roughness", &Object::roughness}, {"emissiveStrength", &Object::emissiveStrength}, {"animSpeed", &Object::animSpeed},
        {"animAmount", &Object::animAmount}, {"lightIntensity", &Object::lightIntensity}, {"lightRange", &Object::lightRange}, {"pSpeed", &Object::pSpeed},
        {"pGravity", &Object::pGravity}, {"lineWidth", &Object::lineWidth}, {"trailTime", &Object::trailTime}, {"trailWidth", &Object::trailWidth},
        {"uiValue", &Object::uiValue}, {"volume", &Object::volume}};
    const auto it = F.find(k); return it == F.end() ? nullptr : &(o.*(it->second));
}
static bool* boolOf(Object& o, const std::string& k) {
    static const std::map<std::string, bool Object::*> B = {
        {"active", &Object::active}, {"hasAnim", &Object::hasAnim}, {"animOn", &Object::animOn}, {"hasLight", &Object::hasLight}, {"lightOn", &Object::lightOn},
        {"hasParticles", &Object::hasParticles}, {"particlesOn", &Object::particlesOn}, {"hasLine", &Object::hasLine}, {"lineOn", &Object::lineOn},
        {"hasTrail", &Object::hasTrail}, {"trailOn", &Object::trailOn}, {"hasCanvas", &Object::hasCanvas}, {"uiOn", &Object::uiOn}, {"uiChecked", &Object::uiChecked},
        {"hasAudio", &Object::hasAudio}, {"hasVideo", &Object::hasVideo}, {"lookOn", &Object::lookOn}};
    const auto it = B.find(k); return it == B.end() ? nullptr : &(o.*(it->second));
}
static std::string* strOf(Object& o, const std::string& k) {
    if (k == "name") return &o.name; if (k == "text") return &o.text; if (k == "clip") return &o.clip;
    if (k == "uiText") return &o.uiText; if (k == "uiFile") return &o.uiFile; if (k == "uiOnClick") return &o.uiOnClick;
    return nullptr;
}

// ---- QuickJS helpers
static std::string str(JSValueConst v) { const char* s = JS_ToCString(ctx, v); std::string r = s ? s : ""; if (s) JS_FreeCString(ctx, s); return r; }
static double dbl(JSValueConst v) { double d = 0; JS_ToFloat64(ctx, &d, v); return d; }
static int num(JSValueConst v) { int i = 0; JS_ToInt32(ctx, &i, v); return i; }
static JSValue jstr(const std::string& s) { return JS_NewStringLen(ctx, s.data(), s.size()); }
static std::string jsonQ(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else if ((unsigned char)c < 0x20) o += ' '; else o += c; }
    return o + "\"";
}
static std::string fmt(double d) { char b[40]; std::snprintf(b, sizeof b, "%.6g", std::isfinite(d) ? d : 0.0); return b; }
static std::string ids(const std::vector<int>& v) { std::string s = "["; for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]); return s + "]"; }
static void reportException(const char* where) {
    JSValue ex = JS_GetException(ctx);
    std::string msg = str(ex);
    JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
    if (JS_IsString(st)) { const std::string s = str(st); if (!s.empty()) msg += " (" + s.substr(0, s.find('\n')) + ")"; }
    JS_FreeValue(ctx, st); JS_FreeValue(ctx, ex);
    if (E.log) E.log("error", std::string(where) + ": " + msg);
}
#define FN(name) static JSValue name(JSContext*, JSValueConst, int argc, JSValueConst* argv)

FN(d_get) {
    if (argc < 2) return JS_UNDEFINED;
    Object* o = object(num(argv[0])); if (!o) return JS_UNDEFINED;
    const std::string k = str(argv[1]);
    if (float* f = floatOf(*o, k)) return JS_NewFloat64(ctx, *f);
    if (bool* b = boolOf(*o, k)) return JS_NewBool(ctx, *b);
    if (std::string* s = strOf(*o, k)) return jstr(*s);
    if (k == "kind") return jstr(Scene::KIND_NAMES[std::clamp(o->kind, 0, (int)Scene::KIND_COUNT - 1)]);
    if (k == "anim") return jstr(Scene::ANIM_NAMES[std::clamp(o->anim, 0, (int)Scene::ANIM_COUNT - 1)]);
    if (k == "ui") return jstr(uiName(o->ui));
    if (k == "linePointCount") return JS_NewInt32(ctx, (int)o->linePoints.size() / 3);
    return JS_UNDEFINED;
}
FN(d_set) {
    if (argc < 3) return JS_UNDEFINED;
    Object* o = object(num(argv[0])); if (!o) return JS_UNDEFINED;
    const std::string k = str(argv[1]);
    if (float* f = floatOf(*o, k)) *f = (float)dbl(argv[2]);
    else if (bool* b = boolOf(*o, k)) *b = JS_ToBool(ctx, argv[2]) != 0;
    else if (std::string* s = strOf(*o, k)) { if (k != "name") *s = str(argv[2]); }
    else if (k == "anim") { const std::string v = str(argv[2]); for (int i = 0; i < Scene::ANIM_COUNT; ++i) if (v == Scene::ANIM_NAMES[i]) o->anim = i; }
    return JS_UNDEFINED;
}
FN(d_getv) {
    if (argc < 3) return JS_UNDEFINED;
    Object* o = object(num(argv[0])); if (!o) return JS_NewFloat64(ctx, 0);
    const Arr a = arrOf(*o, str(argv[1])); const int i = num(argv[2]);
    if (a.f && i >= 0 && i < a.n) return JS_NewFloat64(ctx, a.f[i]);
    if (a.v && i >= 0 && i < (int)a.v->size()) return JS_NewFloat64(ctx, (*a.v)[i]);
    return JS_NewFloat64(ctx, 0);
}
FN(d_setv) {
    if (argc < 4) return JS_UNDEFINED;
    Object* o = object(num(argv[0])); if (!o) return JS_UNDEFINED;
    const Arr a = arrOf(*o, str(argv[1])); const int i = num(argv[2]); const float v = (float)dbl(argv[3]);
    if (a.f && i >= 0 && i < a.n) a.f[i] = v;
    if (a.v && i >= 0 && i < 3000) { if ((int)a.v->size() <= i) a.v->resize((i / 3 + 1) * 3, 0.0f); (*a.v)[i] = v; }
    return JS_UNDEFINED;
}
FN(d_children) {
    if (argc < 1) return JS_NewArray(ctx);
    const int id = num(argv[0]);
    std::vector<int> out;
    if (id < 0) { const int a = anchorOfRoot(id); if (a < (int)E.proj->markers.size()) for (const auto& o : listOf(a)->objects) if (o.parent == -1) out.push_back(o.id); }
    else { Where w; if (object(id, &w)) for (const auto& o : w.list->objects) if (o.parent == id) out.push_back(o.id); }
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < out.size(); ++i) JS_SetPropertyUint32(ctx, arr, (uint32_t)i, JS_NewInt32(ctx, out[i]));
    return arr;
}
FN(d_parent) {
    if (argc < 1) return JS_NULL;
    Where w; Object* o = object(num(argv[0]), &w); if (!o) return JS_NULL;
    return JS_NewInt32(ctx, o->parent == -1 ? rootOf(w.anchor) : o->parent);
}
FN(d_descendants) {
    if (argc < 1) return jstr("[]");
    const int id = num(argv[0]); std::vector<int> out;
    if (id < 0) { const int a = anchorOfRoot(id); if (a < (int)E.proj->markers.size()) for (const auto& o : listOf(a)->objects) out.push_back(o.id); }
    else { Where w; if (object(id, &w)) for (const auto& o : w.list->objects) if (Scene::isUnder(*w.list, o.id, id)) out.push_back(o.id); }
    return jstr(ids(out));
}
FN(d_roots) {
    std::string s = "[";
    for (size_t i = 0; i < E.proj->markers.size(); ++i) s += "{\"name\":" + jsonQ(E.proj->markers[i].name) + ",\"id\":" + std::to_string(rootOf((int)i)) + "},";
    s += "{\"name\":\"Main Camera\",\"id\":" + std::to_string(CAMERA_ROOT) + "}]";
    (void)argc; (void)argv;
    return jstr(s);
}
FN(d_scripts) {
    if (argc < 1) return jstr("[]");
    Object* o = object(num(argv[0])); if (!o) return jstr("[]");
    std::string s = "[";
    for (size_t i = 0; i < o->scripts.size(); ++i) s += std::string(i ? "," : "") + "{\"file\":" + jsonQ(o->scripts[i].file) + ",\"on\":" + (o->scripts[i].on ? "true" : "false") + "}";
    return jstr(s + "]");
}
FN(d_source) {
    if (argc < 1) return JS_NULL;
    std::ifstream f(fs::u8path(str(argv[0])), std::ios::binary); if (!f) return JS_NULL;
    std::stringstream ss; ss << f.rdbuf(); return jstr(ss.str());
}
FN(d_startObjects) {
    // every object that's active, under active parents, parents first (each list in order, as the web game builds them)
    std::vector<int> out;
    for (int a = 0; a <= (int)E.proj->markers.size(); ++a) {
        Scene::Marker* m = listOf(a == (int)E.proj->markers.size() ? -1 : a);
        std::map<int, bool> on;
        for (const auto& o : m->objects) {
            const bool up = o.parent == -1 || (on.count(o.parent) && on[o.parent]);
            on[o.id] = up && o.active;
            if (on[o.id]) out.push_back(o.id);
        }
    }
    (void)argc; (void)argv;
    return jstr(ids(out));
}
static std::vector<Object> prefab(const std::string& name) {
    std::ifstream f(fs::u8path("prefabs") / fs::u8path(name + ".prefab"), std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    Scene::Project tmp; std::string err;
    if (!Scene::fromJson(ss.str(), tmp, err) || tmp.markers.empty()) return {};
    return tmp.markers[0].objects;
}
FN(d_spawn) {
    if (argc < 2) return jstr("[]");
    int newRoot = -1; Where w;
    if (JS_IsNumber(argv[0])) {   // a copy of an object and everything under it
        const int src = num(argv[0]);
        if (!object(src, &w)) return jstr("[]");
        newRoot = Scene::duplicate(*E.proj, *w.list, src);
    } else {   // a prefab (prefabs/<name>.prefab), on the first anchor
        std::vector<Object> objs = prefab(str(argv[0]));
        if (objs.empty() || E.proj->markers.empty()) return jstr("[]");
        w.list = &E.proj->markers[0]; w.anchor = 0;
        std::map<int, int> nid; for (auto& o : objs) nid[o.id] = Scene::newId(*E.proj);
        for (auto& o : objs) { o.parent = o.parent == -1 ? -1 : nid[o.parent]; o.id = nid[o.id]; w.list->objects.push_back(o); }
        newRoot = objs[0].id;
    }
    if (newRoot < 0) return jstr("[]");
    if (JS_IsNumber(argv[1])) {   // beside another object: under its parent
        const int p = num(argv[1]);
        if (p < 0) Scene::reparent(*w.list, newRoot, -1);
        else { Where pw; if (object(p, &pw) && pw.list == w.list) Scene::reparent(*w.list, newRoot, p); }
    }
    std::vector<int> out;
    for (const auto& o : w.list->objects) if (Scene::isUnder(*w.list, o.id, newRoot)) out.push_back(o.id);
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return (a == newRoot) > (b == newRoot); });
    return jstr(ids(out));
}
FN(d_destroy) {
    if (argc < 1) return jstr("[]");
    const int id = num(argv[0]); Where w;
    if (!object(id, &w)) return jstr("[]");
    std::vector<int> out;
    for (const auto& o : w.list->objects) if (Scene::isUnder(*w.list, o.id, id)) out.push_back(o.id);
    Scene::remove(*w.list, id);
    return jstr(ids(out));
}
FN(d_worldToParent) {
    if (argc < 4) return jstr("{\"x\":0,\"y\":0,\"z\":0}");
    float M[16], inv[16];
    const float p[3] = {(float)dbl(argv[1]), (float)dbl(argv[2]), (float)dbl(argv[3])};
    if (!parentToCamera(num(argv[0]), M) || !invert(M, inv)) return jstr("{\"x\":" + fmt(p[0]) + ",\"y\":" + fmt(p[1]) + ",\"z\":" + fmt(p[2]) + "}");
    float q[3]; for (int r = 0; r < 3; ++r) q[r] = inv[r] * p[0] + inv[4 + r] * p[1] + inv[8 + r] * p[2] + inv[12 + r];
    return jstr("{\"x\":" + fmt(q[0]) + ",\"y\":" + fmt(q[1]) + ",\"z\":" + fmt(q[2]) + "}");
}
FN(d_toCamera) {
    if (argc < 1) return jstr("{\"x\":0,\"y\":0,\"z\":1,\"distance\":1}");
    const int id = num(argv[0]);
    float M[16], inv[16]; Object* o = object(id);
    if (!o || !parentToCamera(id, M) || !invert(M, inv)) return jstr("{\"x\":0,\"y\":0,\"z\":1,\"distance\":1}");
    float d[3]; for (int r = 0; r < 3; ++r) d[r] = inv[12 + r] - o->pos[r];   // the camera (the origin), in its parent's frame, from it
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); const float k = len > 1e-6f ? 1 / len : 0;
    return jstr("{\"x\":" + fmt(d[0] * k) + ",\"y\":" + fmt(d[1] * k) + ",\"z\":" + fmt(d[2] * k) + ",\"distance\":" + fmt(len) + "}");
}
// the locked surface as AR Foundation's one plane (Surfaces with any camera)
// the planes locked (Surfaces with any camera: floors and tables, and walls), as AR Foundation's
static std::vector<Env::Plane> planesNow() {
    if (E.planes) return E.planes();
    std::vector<Env::Plane> v; Env::Plane p; p.id = "surface"; p.vertical = false;
    if (E.surface && E.surface(p.view)) v.push_back(p);
    return v;
}
FN(d_planes) {
    (void)argc; (void)argv;
    std::string s = "[";
    for (const auto& p : planesNow()) {
        const float* V = p.view;
        s += std::string(s.size() > 1 ? "," : "") + "{\"id\":" + jsonQ(p.id) + ",\"alignment\":\"" + (p.vertical ? "vertical" : "horizontal") +
             "\",\"center\":{\"x\":" + fmt(V[12]) + ",\"y\":" + fmt(V[13]) + ",\"z\":" + fmt(V[14]) + "},\"size\":{\"x\":1.5,\"y\":1.5},\"boundary\":[";
        for (size_t k = 0; k + 1 < p.boundary.size(); k += 2) s += std::string(k ? "," : "") + "{\"x\":" + fmt(p.boundary[k]) + ",\"z\":" + fmt(p.boundary[k + 1]) + "}";
        s += "]}";
    }
    return jstr(s + "]");
}
FN(d_raycast) {   // where a screen point (-1..1) meets the planes: the nearest first
    if (argc < 2 || !E.projection) return jstr("[]");
    const float* P = E.projection;
    const float x = (float)dbl(argv[0]), y = (float)dbl(argv[1]);
    const float d[3] = {(x + P[8]) / P[0], (y + P[9]) / P[5], -1};
    std::vector<std::pair<float, std::string>> hits;
    for (const auto& p : planesNow()) {
        const float* V = p.view;
        float n[3] = {V[4], V[5], V[6]}; const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); for (float& c : n) c /= nl;
        const float o[3] = {V[12], V[13], V[14]};
        const float den = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
        if (std::fabs(den) < 1e-6f) continue;
        const float s = (n[0] * o[0] + n[1] * o[1] + n[2] * o[2]) / den;
        if (s <= 0) continue;
        const float h[3] = {d[0] * s, d[1] * s, d[2] * s};
        // inside the plane as found (its boundary; a 1.5 m square before it has one)
        float local[2] = {0, 0};
        for (int k = 0; k < 2; ++k) { const int c = k == 0 ? 0 : 8; const float ax[3] = {V[c], V[c + 1], V[c + 2]}; const float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
            local[k] = ((h[0] - o[0]) * ax[0] + (h[1] - o[1]) * ax[1] + (h[2] - o[2]) * ax[2]) / (al * al); }
        const auto& B = p.boundary; const int nb = (int)B.size() / 2;
        if (nb >= 3) {   // inside a convex polygon: on the same side of every edge
            int sign = 0; bool in = true;
            for (int e = 0; e < nb && in; ++e) {
                const float cr = (B[(e + 1) % nb * 2] - B[e * 2]) * (local[1] - B[e * 2 + 1]) - (B[(e + 1) % nb * 2 + 1] - B[e * 2 + 1]) * (local[0] - B[e * 2]);
                const int sg = cr > 0 ? 1 : cr < 0 ? -1 : 0;
                if (sg && sign && sg != sign) in = false; if (sg) sign = sg;
            }
            if (!in) continue;
        } else if (std::fabs(local[0]) > 0.75f || std::fabs(local[1]) > 0.75f) continue;
        const float dist = s * std::sqrt(d[0] * d[0] + d[1] * d[1] + 1);
        hits.push_back({dist, "{\"position\":{\"x\":" + fmt(h[0]) + ",\"y\":" + fmt(h[1]) + ",\"z\":" + fmt(h[2]) + "},\"distance\":" + fmt(dist) + ",\"plane\":" + jsonQ(p.id) + ",\"alignment\":\"" + (p.vertical ? "vertical" : "horizontal") + "\"}"});
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string s = "[";
    for (size_t i = 0; i < hits.size(); ++i) s += (i ? "," : "") + hits[i].second;
    return jstr(s + "]");
}
FN(d_scene) {
    const auto& names = E.proj->scenes;
    std::string s = "{\"name\":" + jsonQ(E.sceneIndex < (int)names.size() ? names[E.sceneIndex] : E.proj->scene) + ",\"index\":" + std::to_string(E.sceneIndex) +
                    ",\"count\":" + std::to_string(names.size()) + ",\"names\":[";
    for (size_t i = 0; i < names.size(); ++i) s += (i ? "," : "") + jsonQ(names[i]);
    (void)argc; (void)argv;
    return jstr(s + "]}");
}
FN(d_loadScene) { if (argc >= 1 && E.loadScene) E.loadScene(num(argv[0])); return JS_UNDEFINED; }
FN(d_log) {
    if (argc < 4) return JS_UNDEFINED;
    const std::string level = str(argv[0]), file = str(argv[1]), text = str(argv[3]); const int line = num(argv[2]);
    std::string where = file.empty() ? "" : fs::u8path(file).filename().u8string() + (line ? ":" + std::to_string(line) : "") + ": ";
    if (E.log) E.log(level, where + text);
    return JS_UNDEFINED;
}

static JSValue global(const char* name) { JSValue g = JS_GetGlobalObject(ctx); JSValue f = JS_GetPropertyStr(ctx, g, name); JS_FreeValue(ctx, g); return f; }
static void callGlobal(const char* name, int argc, JSValue* argv) {
    if (!ctx) return;
    JSValue f = global(name);
    if (JS_IsFunction(ctx, f)) {
        JSValue r = JS_Call(ctx, f, JS_UNDEFINED, argc, argv);
        if (JS_IsException(r)) reportException(name);
        JS_FreeValue(ctx, r);
    }
    JS_FreeValue(ctx, f);
    for (int i = 0; i < argc; ++i) JS_FreeValue(ctx, argv[i]);
}

bool start(const Env& env, std::string& err) {
    stop();
    E = env;
    rt = JS_NewRuntime();
    if (!rt) { err = "QuickJS didn't start"; return false; }
    JS_SetMemoryLimit(rt, 512u << 20);
    ctx = JS_NewContext(rt);
    JSValue g = JS_GetGlobalObject(ctx), d = JS_NewObject(ctx);
    const struct { const char* name; JSCFunction* fn; int n; } fns[] = {
        {"get", d_get, 2}, {"set", d_set, 3}, {"getv", d_getv, 3}, {"setv", d_setv, 4}, {"children", d_children, 1}, {"parent", d_parent, 1},
        {"descendants", d_descendants, 1}, {"roots", d_roots, 0}, {"scripts", d_scripts, 1}, {"source", d_source, 1}, {"startObjects", d_startObjects, 0},
        {"spawn", d_spawn, 2}, {"destroy", d_destroy, 1}, {"worldToParent", d_worldToParent, 4}, {"toCamera", d_toCamera, 1}, {"planes", d_planes, 0},
        {"raycast", d_raycast, 2}, {"scene", d_scene, 0}, {"loadScene", d_loadScene, 1}, {"log", d_log, 4}};
    for (const auto& f : fns) JS_SetPropertyStr(ctx, d, f.name, JS_NewCFunction(ctx, f.fn, f.name, f.n));
    JS_SetPropertyStr(ctx, g, "__dio", d);
    JS_FreeValue(ctx, g);
    const std::string src(reinterpret_cast<const char*>(RUNTIME_JS), sizeof RUNTIME_JS);
    JSValue r = JS_Eval(ctx, src.c_str(), src.size(), "runtime.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) { reportException("runtime.js"); JS_FreeValue(ctx, r); err = "The script runtime didn't load"; return false; }
    JS_FreeValue(ctx, r);
    callGlobal("__start", 0, nullptr);
    return true;
}
void frame(double dt, double t, bool tapped, float x, float y) {
    if (!ctx) return;
    JSValue a[5] = {JS_NewFloat64(ctx, dt), JS_NewFloat64(ctx, t), JS_NewBool(ctx, tapped), JS_NewFloat64(ctx, x), JS_NewFloat64(ctx, y)};
    callGlobal("__frame", 5, a);
    static int n = 0; if (++n % 30 == 0) JS_RunGC(rt);   // the cycles refcounting leaves, now and then
}
void seen(int anchor, bool on) { if (!ctx) return; JSValue a[2] = {JS_NewInt32(ctx, rootOf(anchor)), JS_NewBool(ctx, on)}; callGlobal("__seen", 2, a); }
void tap(int id) { if (!ctx) return; JSValue a[1] = {JS_NewInt32(ctx, id)}; callGlobal("__tap", 1, a); }
void ui(int id, const char* what, double value, bool isBool) { if (!ctx) return; JSValue a[3] = {JS_NewInt32(ctx, id), JS_NewString(ctx, what), isBool ? JS_NewBool(ctx, value != 0) : JS_NewFloat64(ctx, value)}; callGlobal("__ui", 3, a); }
void planesChanged(const std::string& json) { if (!ctx) return; JSValue a[1] = {jstr(json)}; callGlobal("__planesChanged", 1, a); }
void stop() {
    if (ctx) { JS_FreeContext(ctx); ctx = nullptr; }
    if (rt) { JS_FreeRuntime(rt); rt = nullptr; }
}

}
