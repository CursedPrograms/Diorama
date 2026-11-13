// Diorama - the project (see scene.h)
#include "scene.h"
#include "json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace Scene {

const char* KIND_NAMES[KIND_COUNT] = {"Empty", "Plane", "Cube", "Sphere", "Capsule", "Cylinder", "Quad", "Picture", "Model", "Sprites", "Light rays", "Words"};
const char* ANIM_NAMES[ANIM_COUNT] = {"Still", "Float", "Spin", "Sway", "Pulse"};
bool isPrimitive(int k) { return k >= PLANE && k <= QUAD; }

int newId(Project& p) { return p.nextId++; }

Object make(Project& p, int kind, int parent) {
    Object o;
    o.id = newId(p); o.parent = parent; o.kind = kind;
    o.name = kind == EMPTY ? "GameObject" : KIND_NAMES[kind];
    // Unity's sizes, made marker-sized: a cube 1 across would cover the marker, so primitives start at 0.3
    const float s = 0.3f;
    switch (kind) {
    case PLANE: o.scl[0] = o.scl[2] = 0.1f; o.scl[1] = 1; break;   // Unity's plane is 10 across
    case CUBE: case SPHERE: case CYLINDER: case CAPSULE: o.scl[0] = o.scl[1] = o.scl[2] = s; o.pos[1] = kind == CAPSULE ? s : kind == CYLINDER ? s : s / 2; break;
    case QUAD: o.scl[0] = o.scl[1] = 0.5f; o.pos[1] = 0.25f; break;
    case PICTURE: o.lit = false; break;
    case SPRITES: o.lit = false; o.scl[0] = o.scl[2] = 1; o.scl[1] = 0.6f; o.pos[1] = 0.3f; break;
    case RAYS: o.lit = false; o.glow = true; o.colour[0] = 1; o.colour[1] = 0.95f; o.colour[2] = 0.8f; o.colour[3] = 0.6f; break;
    case WORDS: o.lit = false; o.billboard = true; o.pos[1] = 0.4f; o.scl[0] = o.scl[1] = o.scl[2] = 0.5f; break;
    default: break;
    }
    return o;
}

// ---------- the hierarchy ----------
int indexOfId(const Marker& m, int id) { for (size_t i = 0; i < m.objects.size(); ++i) if (m.objects[i].id == id) return (int)i; return -1; }
std::vector<int> childrenOf(const Marker& m, int parent) { std::vector<int> v; for (size_t i = 0; i < m.objects.size(); ++i) if (m.objects[i].parent == parent) v.push_back((int)i); return v; }
bool isUnder(const Marker& m, int id, int ancestor) {
    for (int cur = id; cur != -1;) {
        if (cur == ancestor) return true;
        const int i = indexOfId(m, cur);
        if (i < 0) return false;
        cur = m.objects[i].parent;
    }
    return false;
}
// the list in tree order (each parent, then its children), so parents always come first
static void reorder(Marker& m) {
    std::vector<Object> out;
    std::function<void(int)> walk = [&](int parent) { for (int i : childrenOf(m, parent)) { out.push_back(m.objects[i]); walk(m.objects[i].id); } };
    walk(-1);
    m.objects = out;
}
void reparent(Marker& m, int id, int newParent) {
    if (newParent != -1 && isUnder(m, newParent, id)) return;   // not under itself
    const int i = indexOfId(m, id);
    if (i < 0) return;
    m.objects[i].parent = newParent;
    // last among its new siblings
    Object o = m.objects[i]; m.objects.erase(m.objects.begin() + i); m.objects.push_back(o);
    reorder(m);
}
void remove(Marker& m, int id) {
    m.objects.erase(std::remove_if(m.objects.begin(), m.objects.end(), [&](const Object& o) { return isUnder(m, o.id, id); }), m.objects.end());
}
int duplicate(Project& p, Marker& m, int id) {
    std::vector<Object> copies;
    std::vector<std::pair<int, int>> idMap;
    for (const Object& o : m.objects) if (isUnder(m, o.id, id)) { Object c = o; c.id = newId(p); idMap.push_back({o.id, c.id}); copies.push_back(c); }
    for (Object& c : copies) for (const auto& mp : idMap) if (c.parent == mp.first) { c.parent = mp.second; break; }
    if (copies.empty()) return -1;
    copies[0].name += " (1)";
    for (const Object& c : copies) m.objects.push_back(c);
    reorder(m);
    return copies[0].id;
}

// ---------- JSON ----------
static std::string arr(const float* v, int n) {
    std::string s = "[";
    char b[32];
    for (int i = 0; i < n; ++i) { snprintf(b, sizeof b, "%s%.5g", i ? "," : "", v[i]); s += b; }
    return s + "]";
}
static void readArr(const Json::Value& o, const char* k, float* v, int n) {
    const Json::Value* a = o.get(k);
    if (!a || a->type != Json::Value::ARR) return;
    for (int i = 0; i < n && i < (int)a->a.size(); ++i) if (a->a[i].type == Json::Value::NUM) v[i] = (float)a->a[i].n;
}
static bool readBool(const Json::Value& o, const char* k, bool def) { const Json::Value* v = o.get(k); return v && v->type == Json::Value::BOOL ? v->b : def; }
static int indexOf(const char* const* names, int n, const std::string& s, int def) { for (int i = 0; i < n; ++i) if (s == names[i]) return i; return def; }

std::string toJson(const Project& p) {
    std::string out = "{\n  \"diorama\": 1,\n  \"title\": " + Json::quote(p.title) + ",\n  \"nextId\": " + std::to_string(p.nextId) + ",\n  \"markers\": [";
    for (size_t m = 0; m < p.markers.size(); ++m) {
        const Marker& k = p.markers[m];
        Json::Writer w;
        w.i("id", k.id).s("name", k.name).s("type", k.type == MK_BARCODE ? "barcode" : "pattern").s("preset", k.preset).s("picture", k.picture)
         .s("letter", k.letter).i("barcode", k.barcode).f("sunStrength", k.sunStrength).f("sunYaw", k.sunYaw).f("sunPitch", k.sunPitch)
         .b("portal", k.portal).b("expanded", k.expanded);
        if (!k.patt.empty()) w.s("patt", k.patt);
        w.key("ambient"); w.out += arr(k.ambient, 3);
        w.key("sun"); w.out += arr(k.sun, 3);
        w.key("portalSize"); w.out += arr(k.portalSize, 2);
        w.key("objects"); w.out += "[";
        for (size_t i = 0; i < k.objects.size(); ++i) {
            const Object& o = k.objects[i];
            Json::Writer ow;
            ow.i("id", o.id).i("parent", o.parent).s("name", o.name).s("kind", KIND_NAMES[o.kind]).b("active", o.active).b("expanded", o.expanded)
              .s("file", o.file).b("lit", o.lit).b("glow", o.glow).b("billboard", o.billboard).s("anim", ANIM_NAMES[o.anim])
              .f("animSpeed", o.animSpeed).f("animAmount", o.animAmount).i("count", o.count).f("spriteSize", o.spriteSize).f("flap", o.flap)
              .i("rays", o.rays).s("text", o.text).s("clip", o.clip).f("clipSpeed", o.clipSpeed).b("clipLoop", o.clipLoop);
            ow.key("position"); ow.out += arr(o.pos, 3);
            ow.key("rotation"); ow.out += arr(o.rot, 3);
            ow.key("scale"); ow.out += arr(o.scl, 3);
            ow.key("colour"); ow.out += arr(o.colour, 4);
            w.out += std::string(i ? "," : "") + "\n        " + ow.str();
        }
        w.out += "]";
        out += std::string(m ? "," : "") + "\n    " + w.str();
    }
    return out + "\n  ]\n}\n";
}

bool fromJson(const std::string& text, Project& p, std::string& err) {
    Json::Value v;
    if (!Json::parse(text, v) || v.type != Json::Value::OBJ) { err = "Not a Diorama project"; return false; }
    p = Project();
    p.title = v.str("title", p.title);
    p.nextId = (int)v.num("nextId", 1);
    const Json::Value* ms = v.get("markers");
    if (!ms || ms->type != Json::Value::ARR) return true;
    for (const Json::Value& mv : ms->a) {
        Marker k;
        k.id = (int)mv.num("id", 0); k.name = mv.str("name", k.name); k.type = mv.str("type") == "barcode" ? MK_BARCODE : MK_PATTERN; k.preset = mv.str("preset", "");
        k.picture = mv.str("picture"); k.letter = mv.str("letter"); k.barcode = (int)mv.num("barcode", 0);
        k.sunStrength = (float)mv.num("sunStrength", k.sunStrength); k.sunYaw = (float)mv.num("sunYaw", k.sunYaw); k.sunPitch = (float)mv.num("sunPitch", k.sunPitch);
        k.portal = readBool(mv, "portal", k.portal); k.expanded = readBool(mv, "expanded", true);
        readArr(mv, "ambient", k.ambient, 3); readArr(mv, "sun", k.sun, 3); readArr(mv, "portalSize", k.portalSize, 2);
        if (const Json::Value* os = mv.get("objects"))
            for (const Json::Value& ov : os->a) {
                Object o;
                o.id = (int)ov.num("id", 0); o.parent = (int)ov.num("parent", -1); o.name = ov.str("name", o.name);
                o.kind = indexOf(KIND_NAMES, KIND_COUNT, ov.str("kind"), EMPTY); o.active = readBool(ov, "active", true); o.expanded = readBool(ov, "expanded", true);
                o.file = ov.str("file"); o.lit = readBool(ov, "lit", true); o.glow = readBool(ov, "glow", false); o.billboard = readBool(ov, "billboard", false);
                o.anim = indexOf(ANIM_NAMES, ANIM_COUNT, ov.str("anim"), A_STILL); o.animSpeed = (float)ov.num("animSpeed", 1); o.animAmount = (float)ov.num("animAmount", 0.1);
                o.count = (int)ov.num("count", 12); o.spriteSize = (float)ov.num("spriteSize", 0.15); o.flap = (float)ov.num("flap", 1);
                o.rays = (int)ov.num("rays", 6); o.text = ov.str("text", o.text);
                o.clip = ov.str("clip"); o.clipSpeed = (float)ov.num("clipSpeed", 1); o.clipLoop = readBool(ov, "clipLoop", true);
                readArr(ov, "position", o.pos, 3); readArr(ov, "rotation", o.rot, 3); readArr(ov, "scale", o.scl, 3); readArr(ov, "colour", o.colour, 4);
                k.objects.push_back(o);
                p.nextId = std::max(p.nextId, o.id + 1);
            }
        p.nextId = std::max(p.nextId, k.id + 1);
        p.markers.push_back(k);
    }
    return true;
}

bool save(const Project& p, const std::string& folder, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::u8path(folder) / "assets", ec);
    std::ofstream f(fs::u8path(folder) / "project.diorama", std::ios::binary);
    if (!f) { err = "Couldn't write " + folder; return false; }
    f << toJson(p);
    return (bool)f;
}

bool load(Project& p, const std::string& folder, std::string& err) {
    std::ifstream f(fs::u8path(folder) / "project.diorama", std::ios::binary);
    if (!f) { err = "No project.diorama in " + folder; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return fromJson(ss.str(), p, err);
}

// ---------- templates ----------
Project basicTemplate() {
    Project p;
    p.title = "My diorama";
    Marker k; k.id = newId(p); k.name = "Hiro"; k.preset = "hiro";
    Object cube = make(p, CUBE, -1); cube.colour[0] = 0; cube.colour[1] = 0.48f; cube.colour[2] = 0.72f; cube.anim = A_SPIN; cube.animSpeed = 0.5f;
    Object w = make(p, WORDS, -1); w.name = "Hello"; w.pos[1] = 0.6f;
    k.objects = {cube, w};
    p.markers.push_back(k);
    return p;
}

Project galleryTemplate(const std::string& dir) {
    // the ARToolkit art gallery (Unity), with its own positions: a shadow-box frame lying on the marker, the scene
    // going down into it (the forest under the paper, seen only through the frame), butterflies flying out of it
    Project p;
    p.title = "Art gallery";
    Marker k; k.id = newId(p); k.name = "Gallery"; k.preset = "kanji"; k.portal = true; k.portalSize[0] = 1.5f; k.portalSize[1] = 1.5f;
    k.ambient[0] = 0.62f; k.ambient[1] = 0.6f; k.ambient[2] = 0.58f; k.sun[0] = 1.0f; k.sun[1] = 0.93f; k.sun[2] = 0.78f;
    Object root = make(p, EMPTY, -1); root.name = "Marker scene"; root.scl[0] = root.scl[1] = root.scl[2] = 0.8f;
    auto pic = [&](const char* name, const char* file, float x, float y, float z, float w) {
        Object o = make(p, PICTURE, root.id); o.name = name; o.file = dir + file; o.pos[0] = x; o.pos[1] = y; o.pos[2] = z;
        o.scl[0] = o.scl[1] = o.scl[2] = w; return o;
    };
    Object frame = make(p, MODEL, root.id); frame.name = "Frame"; frame.file = dir + "obj_ar_frame.glb";
    frame.colour[0] = 0.485f; frame.colour[1] = 0.171f; frame.colour[2] = 0.0f;
    Object bg = pic("BG", "tex_treesBG.png", 0, -0.418f, 0, 1.9155f);
    Object rays1 = pic("Godrays", "tex_godrays.png", 0, -0.189f, 0, 1.9155f); rays1.glow = true; rays1.colour[3] = 0.8f;
    rays1.anim = A_PULSE; rays1.animSpeed = 0.4f; rays1.animAmount = 0.25f;
    Object rays2 = pic("Godrays", "tex_godrays.png", 0.116f, -0.175f, 0, 1.9155f); rays2.glow = true; rays2.colour[3] = 0.6f;
    rays2.anim = A_PULSE; rays2.animSpeed = 0.3f; rays2.animAmount = 0.3f;
    Object woman = pic("Woman", "tex_woman00.png", 0.288f, -0.118f, -0.278f, 1.3644f);
    woman.anim = A_FLOAT; woman.animSpeed = 0.5f; woman.animAmount = 0.015f;
    Object bf = make(p, SPRITES, root.id); bf.name = "Butterflies"; bf.file = dir + "tex_butterfly.png"; bf.count = 10; bf.spriteSize = 0.22f;
    bf.scl[0] = 1.6f; bf.scl[2] = 1.1f; bf.scl[1] = 1.1f; bf.pos[1] = 0.15f; bf.animSpeed = 0.6f;
    k.objects = {root, bg, rays1, rays2, woman, frame, bf};
    p.markers.push_back(k);
    return p;
}

}
