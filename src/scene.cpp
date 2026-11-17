// Diorama - the project (see scene.h)
#include "scene.h"
#include <cstdio>
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
const char* TRACKING_NAMES[TRACKING_COUNT] = {"markers", "images", "faces", "surfaces", "aruco", "hands"};
const FacePoint FACE_POINTS[] = {
    {"Nose tip", 1, {0, 0.05f, -0.62f}}, {"Between the eyes", 168, {0, -0.12f, -0.5f}}, {"Forehead", 10, {0, -0.42f, -0.45f}},
    {"Chin", 152, {0, 0.62f, -0.42f}}, {"Mouth", 13, {0, 0.3f, -0.55f}}, {"Left eye", 159, {0.17f, -0.12f, -0.48f}},
    {"Right eye", 386, {-0.17f, -0.12f, -0.48f}}, {"Left cheek", 234, {0.48f, 0.05f, -0.1f}}, {"Right cheek", 454, {-0.48f, 0.05f, -0.1f}}};
const int FACE_POINT_COUNT = (int)(sizeof FACE_POINTS / sizeof FACE_POINTS[0]);
bool isPrimitive(int k) { return k >= PLANE && k <= QUAD; }

int newId(Project& p) { return p.nextId++; }

const char* UI_NAMES[UI_COUNT] = {"", "Text", "Button", "Slider", "Toggle", "Image", "Panel"};
const char* COLLIDER_NAMES[COL_COUNT] = {"Box Collider", "Sphere Collider", "Capsule Collider", "Mesh Collider"};
// a collider fitted to the object's look, as Unity fits one when it's added (in the object's own units, before its scale)
void fitCollider(Object& o, int shape, const float lo[3], const float hi[3]) {
    o.colShape = shape;
    float a[3] = {-0.5f, -0.5f, -0.5f}, b[3] = {0.5f, 0.5f, 0.5f};   // the look's bounds
    switch (o.kind) {
    case PLANE: a[0] = a[2] = -5; b[0] = b[2] = 5; a[1] = b[1] = 0; break;   // Unity's plane: 10 across, flat
    case CAPSULE: case CYLINDER: a[1] = -1; b[1] = 1; break;                 // 2 high
    case QUAD: case PICTURE: case WORDS: a[2] = b[2] = 0; break;            // flat, facing +Z
    case MODEL: if (lo && hi) for (int i = 0; i < 3; ++i) { a[i] = lo[i]; b[i] = hi[i]; } break;
    default: break;
    }
    for (int i = 0; i < 3; ++i) { o.colCentre[i] = (a[i] + b[i]) / 2; o.colSize[i] = b[i] - a[i]; }
    o.colRadius = 0.5f * std::max(o.colSize[0], o.colSize[2]);
    o.colHeight = std::max(o.colSize[1], 2 * o.colRadius);
    if (shape == COL_SPHERE) o.colRadius = 0.5f * std::max({o.colSize[0], o.colSize[1], o.colSize[2]});
}

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

static std::string fnum(float f) { char b[32]; snprintf(b, sizeof b, "%g", f); return b; }
// anchors (and the Main Camera, which has the same parts) as JSON objects, each on its own line
static std::string markersJson(const std::vector<Marker>& list) {
    std::string out;
    for (size_t m = 0; m < list.size(); ++m) {
        const Marker& k = list[m];
        Json::Writer w;
        w.i("id", k.id).s("name", k.name).s("type", k.type == MK_BARCODE ? "barcode" : "pattern").s("preset", k.preset).s("picture", k.picture)
         .s("letter", k.letter).i("barcode", k.barcode).f("sunStrength", k.sunStrength).f("sunYaw", k.sunYaw).f("sunPitch", k.sunPitch)
         .b("portal", k.portal).b("expanded", k.expanded);
        if (!k.patt.empty()) w.s("patt", k.patt);
        w.s("target", k.target).i("facePoint", k.facePoint);
        w.key("ambient"); w.out += arr(k.ambient, 3);
        w.key("sun"); w.out += arr(k.sun, 3);
        w.key("portalSize"); w.out += arr(k.portalSize, 2);
        w.i("arucoId", k.arucoId);
        w.b("sunShadows", k.sunShadows).b("shadowCatcher", k.shadowCatcher).b("physicsFloor", k.physicsFloor);
        w.i("planeDetect", k.planeDetect).b("showPlanes", k.showPlanes).s("planePrefab", k.planePrefab).i("placement", k.placement);
        w.f("webcamTilt", k.webcamTilt).f("webcamHeight", k.webcamHeight).f("webcamFov", k.webcamFov).i("webcamSurface", k.webcamSurface);
        w.key("planeColour"); w.out += arr(k.planeColour, 4);
        w.key("objects"); w.out += "[";
        for (size_t i = 0; i < k.objects.size(); ++i) {
            const Object& o = k.objects[i];
            Json::Writer ow;
            ow.i("id", o.id).i("parent", o.parent).s("name", o.name).s("kind", KIND_NAMES[o.kind]).b("active", o.active).b("expanded", o.expanded)
              .s("file", o.file).b("lit", o.lit).b("glow", o.glow).b("billboard", o.billboard).s("anim", ANIM_NAMES[o.anim])
              .f("animSpeed", o.animSpeed).f("animAmount", o.animAmount).i("count", o.count).f("spriteSize", o.spriteSize).f("flap", o.flap)
              .i("rays", o.rays).s("text", o.text).s("clip", o.clip).f("clipSpeed", o.clipSpeed).b("clipLoop", o.clipLoop)
              .b("lookOn", o.lookOn).b("hasAnim", o.hasAnim).b("animOn", o.animOn).s("prefab", o.prefab).s("material", o.material);
            ow.f("metallic", o.metallic).f("roughness", o.roughness).f("emissiveStrength", o.emissiveStrength).b("castShadows", o.castShadows).b("receiveShadows", o.receiveShadows).b("lightShadows", o.lightShadows);
            ow.key("emissive"); ow.out += arr(o.emissive, 3);
            ow.b("occluder", o.occluder)
              .b("hasLight", o.hasLight).b("lightOn", o.lightOn).i("lightType", o.lightType).f("lightIntensity", o.lightIntensity).f("lightRange", o.lightRange).f("spotAngle", o.spotAngle)
              .b("hasAudio", o.hasAudio).b("audioOn", o.audioOn).s("audioFile", o.audioFile).f("volume", o.volume).b("audioLoop", o.audioLoop).b("playOnStart", o.playOnStart)
              .b("spatial", o.spatial).f("audioNear", o.audioNear).f("audioFar", o.audioFar)
              .b("hasVideo", o.hasVideo).b("videoOn", o.videoOn).s("videoFile", o.videoFile).b("videoLoop", o.videoLoop).b("videoPlayOnStart", o.videoPlayOnStart)
              .b("videoMuted", o.videoMuted).f("videoVolume", o.videoVolume).f("videoSpeed", o.videoSpeed)
              .i("videoAlpha", o.videoAlpha).f("videoKeyTol", o.videoKeyTol).f("videoKeySoft", o.videoKeySoft)
              .b("hasPath", o.hasPath).b("pathOn", o.pathOn).b("pathLoop", o.pathLoop).b("pathLine", o.pathLine).s("pathName", o.pathName)
              .f("arrowSpacing", o.arrowSpacing).f("arrowSize", o.arrowSize)
              .b("hasFollower", o.hasFollower).b("followerOn", o.followerOn).b("followLoop", o.followLoop).b("followOnStart", o.followOnStart)
              .s("followPath", o.followPath).f("followSpeed", o.followSpeed)
              .b("hasCanvas", o.hasCanvas).b("canvasWithAnchor", o.canvasWithAnchor).s("canvasCss", o.canvasCss)
              .s("ui", UI_NAMES[std::clamp(o.ui, 0, UI_COUNT - 1)]).b("uiOn", o.uiOn).i("uiAnchor", o.uiAnchor).s("uiText", o.uiText).f("uiFont", o.uiFont).i("uiAlign", o.uiAlign)
              .f("uiMin", o.uiMin).f("uiMax", o.uiMax).f("uiValue", o.uiValue).b("uiChecked", o.uiChecked).s("uiFile", o.uiFile).s("uiFontFile", o.uiFontFile).i("uiRole", o.uiRole).s("uiClass", o.uiClass).s("uiOnClick", o.uiOnClick)
              .b("hasParticles", o.hasParticles).b("particlesOn", o.particlesOn).s("particleFile", o.particleFile).f("pRate", o.pRate).f("pLife", o.pLife).f("pSpeed", o.pSpeed)
              .f("pSpread", o.pSpread).f("pSizeStart", o.pSizeStart).f("pSizeEnd", o.pSizeEnd).f("pGravity", o.pGravity).b("pGlow", o.pGlow)
              .b("hasLine", o.hasLine).b("lineOn", o.lineOn).b("lineLoop", o.lineLoop).f("lineWidth", o.lineWidth)
              .b("hasTrail", o.hasTrail).b("trailOn", o.trailOn).f("trailTime", o.trailTime).f("trailWidth", o.trailWidth)
              .b("hasBody", o.hasBody).f("mass", o.mass).f("drag", o.drag).f("angularDrag", o.angularDrag).b("useGravity", o.useGravity)
              .b("isKinematic", o.isKinematic).i("freeze", o.freeze)
              .b("hasCollider", o.hasCollider).b("colliderOn", o.colliderOn).b("isTrigger", o.isTrigger).i("colShape", o.colShape)
              .f("colRadius", o.colRadius).f("colHeight", o.colHeight).f("friction", o.friction).f("bounciness", o.bounciness);
            ow.key("colCentre"); ow.out += arr(o.colCentre, 3);
            ow.key("colSize"); ow.out += arr(o.colSize, 3);
            ow.key("lightColour"); ow.out += arr(o.lightColour, 3);
            ow.key("pColourStart"); ow.out += arr(o.pColourStart, 4);
            ow.key("pColourEnd"); ow.out += arr(o.pColourEnd, 4);
            ow.key("lineColour"); ow.out += arr(o.lineColour, 4);
            ow.key("trailColour"); ow.out += arr(o.trailColour, 4);
            ow.key("uiRect"); ow.out += arr(o.uiRect, 4);
            ow.key("videoKey"); ow.out += arr(o.videoKey, 3);
            ow.key("pathColour"); ow.out += arr(o.pathColour, 4);
            ow.key("pathPoints"); ow.out += arr(o.pathPoints.data(), (int)o.pathPoints.size());
            ow.key("uiColour"); ow.out += arr(o.uiColour, 4);
            ow.key("uiBg"); ow.out += arr(o.uiBg, 4);
            ow.key("linePoints"); ow.out += arr(o.linePoints.data(), (int)o.linePoints.size());
            ow.key("scripts"); ow.out += "[";
            for (size_t si = 0; si < o.scripts.size(); ++si) ow.out += std::string(si ? "," : "") + "{\"file\":" + Json::quote(o.scripts[si].file) + ",\"on\":" + (o.scripts[si].on ? "true" : "false") + "}";
            ow.out += "]";
            ow.key("position"); ow.out += arr(o.pos, 3);
            ow.key("rotation"); ow.out += arr(o.rot, 3);
            ow.key("scale"); ow.out += arr(o.scl, 3);
            ow.key("colour"); ow.out += arr(o.colour, 4);
            w.out += std::string(i ? "," : "") + "\n        " + ow.str();
        }
        w.out += "]";
        out += std::string(m ? "," : "") + "\n    " + w.str();
    }
    return out;
}
static Marker markerFrom(const Json::Value& mv, Project& p) {
    Marker k;
    k.id = (int)mv.num("id", 0); k.name = mv.str("name", k.name); k.type = mv.str("type") == "barcode" ? MK_BARCODE : MK_PATTERN; k.preset = mv.str("preset", "");
    k.picture = mv.str("picture"); k.letter = mv.str("letter"); k.barcode = (int)mv.num("barcode", 0);
    k.target = mv.str("target"); k.facePoint = (int)mv.num("facePoint", 1);
    k.sunStrength = (float)mv.num("sunStrength", k.sunStrength); k.sunYaw = (float)mv.num("sunYaw", k.sunYaw); k.sunPitch = (float)mv.num("sunPitch", k.sunPitch);
    k.portal = readBool(mv, "portal", k.portal); k.expanded = readBool(mv, "expanded", true);
    readArr(mv, "ambient", k.ambient, 3); readArr(mv, "sun", k.sun, 3); readArr(mv, "portalSize", k.portalSize, 2);
    k.webcamTilt = (float)mv.num("webcamTilt", k.webcamTilt); k.webcamHeight = (float)mv.num("webcamHeight", k.webcamHeight); k.webcamFov = (float)mv.num("webcamFov", k.webcamFov); k.webcamSurface = (int)mv.num("webcamSurface", 0);
    k.planeDetect = (int)mv.num("planeDetect", 2);
    k.arucoId = (int)mv.num("arucoId", 0);
    k.patt = mv.str("patt", "");   // a build's pattern file (the native player reads it)
    k.sunShadows = readBool(mv, "sunShadows", true); k.shadowCatcher = readBool(mv, "shadowCatcher", true); k.physicsFloor = readBool(mv, "physicsFloor", true); k.showPlanes = readBool(mv, "showPlanes", true); k.planePrefab = mv.str("planePrefab"); k.placement = (int)mv.num("placement", 0);
    readArr(mv, "planeColour", k.planeColour, 4);
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
            o.lookOn = readBool(ov, "lookOn", true); o.animOn = readBool(ov, "animOn", true);
            o.prefab = ov.str("prefab"); o.material = ov.str("material");
            o.occluder = readBool(ov, "occluder", false);
                o.metallic = (float)ov.num("metallic", 0); o.roughness = (float)ov.num("roughness", 0.6); o.emissiveStrength = (float)ov.num("emissiveStrength", 0);
                readArr(ov, "emissive", o.emissive, 3);
                o.castShadows = readBool(ov, "castShadows", true); o.receiveShadows = readBool(ov, "receiveShadows", true); o.lightShadows = readBool(ov, "lightShadows", false);
            o.hasLight = readBool(ov, "hasLight", false); o.lightOn = readBool(ov, "lightOn", true); o.lightType = (int)ov.num("lightType", 0);
            o.lightIntensity = (float)ov.num("lightIntensity", o.lightIntensity); o.lightRange = (float)ov.num("lightRange", o.lightRange); o.spotAngle = (float)ov.num("spotAngle", o.spotAngle);
            o.hasAudio = readBool(ov, "hasAudio", false); o.audioOn = readBool(ov, "audioOn", true); o.audioFile = ov.str("audioFile"); o.volume = (float)ov.num("volume", 1);
            o.audioLoop = readBool(ov, "audioLoop", false); o.playOnStart = readBool(ov, "playOnStart", true); o.spatial = readBool(ov, "spatial", true);
            o.audioNear = (float)ov.num("audioNear", o.audioNear); o.audioFar = (float)ov.num("audioFar", o.audioFar);
            o.hasVideo = readBool(ov, "hasVideo", false); o.videoOn = readBool(ov, "videoOn", true); o.videoFile = ov.str("videoFile");
            o.videoLoop = readBool(ov, "videoLoop", true); o.videoPlayOnStart = readBool(ov, "videoPlayOnStart", true); o.videoMuted = readBool(ov, "videoMuted", false);
            o.videoVolume = (float)ov.num("videoVolume", 1); o.videoSpeed = (float)ov.num("videoSpeed", 1);
                o.videoAlpha = (int)ov.num("videoAlpha", 0); o.videoKeyTol = (float)ov.num("videoKeyTol", o.videoKeyTol); o.videoKeySoft = (float)ov.num("videoKeySoft", o.videoKeySoft);
                readArr(ov, "videoKey", o.videoKey, 3);
            o.hasPath = readBool(ov, "hasPath", false); o.pathOn = readBool(ov, "pathOn", true); o.pathLoop = readBool(ov, "pathLoop", false); o.pathLine = readBool(ov, "pathLine", true);
            o.pathName = ov.str("pathName", o.pathName); o.arrowSpacing = (float)ov.num("arrowSpacing", o.arrowSpacing); o.arrowSize = (float)ov.num("arrowSize", o.arrowSize);
            readArr(ov, "pathColour", o.pathColour, 4);
            if (const Json::Value* pp = ov.get("pathPoints")) if (pp->type == Json::Value::ARR && pp->a.size() >= 3) {
                o.pathPoints.clear(); for (const auto& v : pp->a) o.pathPoints.push_back((float)v.n); o.pathPoints.resize(o.pathPoints.size() / 3 * 3);
            }
            o.hasFollower = readBool(ov, "hasFollower", false); o.followerOn = readBool(ov, "followerOn", true); o.followLoop = readBool(ov, "followLoop", true);
            o.followOnStart = readBool(ov, "followOnStart", true); o.followPath = ov.str("followPath", o.followPath); o.followSpeed = (float)ov.num("followSpeed", o.followSpeed);
            o.hasCanvas = readBool(ov, "hasCanvas", false); o.canvasWithAnchor = readBool(ov, "canvasWithAnchor", false); o.canvasCss = ov.str("canvasCss");
            { const std::string u = ov.str("ui"); o.ui = UI_NONE; for (int k = 1; k < UI_COUNT; ++k) if (u == UI_NAMES[k]) o.ui = k; }
            o.uiOn = readBool(ov, "uiOn", true); o.uiAnchor = (int)ov.num("uiAnchor", 4); o.uiText = ov.str("uiText", o.uiText); o.uiFont = (float)ov.num("uiFont", 20);
            o.uiAlign = (int)ov.num("uiAlign", 1); o.uiMin = (float)ov.num("uiMin", 0); o.uiMax = (float)ov.num("uiMax", 1); o.uiValue = (float)ov.num("uiValue", 0.5);
            o.uiChecked = readBool(ov, "uiChecked", false); o.uiFile = ov.str("uiFile"); o.uiFontFile = ov.str("uiFontFile", ""); o.uiRole = (int)ov.num("uiRole", 0); o.uiClass = ov.str("uiClass"); o.uiOnClick = ov.str("uiOnClick");
            readArr(ov, "uiRect", o.uiRect, 4); readArr(ov, "uiColour", o.uiColour, 4); readArr(ov, "uiBg", o.uiBg, 4);
            o.hasParticles = readBool(ov, "hasParticles", false); o.particlesOn = readBool(ov, "particlesOn", true); o.particleFile = ov.str("particleFile");
            o.pRate = (float)ov.num("pRate", o.pRate); o.pLife = (float)ov.num("pLife", o.pLife); o.pSpeed = (float)ov.num("pSpeed", o.pSpeed); o.pSpread = (float)ov.num("pSpread", o.pSpread);
            o.pSizeStart = (float)ov.num("pSizeStart", o.pSizeStart); o.pSizeEnd = (float)ov.num("pSizeEnd", o.pSizeEnd); o.pGravity = (float)ov.num("pGravity", o.pGravity); o.pGlow = readBool(ov, "pGlow", true);
            o.hasLine = readBool(ov, "hasLine", false); o.lineOn = readBool(ov, "lineOn", true); o.lineLoop = readBool(ov, "lineLoop", false); o.lineWidth = (float)ov.num("lineWidth", o.lineWidth);
            o.hasTrail = readBool(ov, "hasTrail", false); o.trailOn = readBool(ov, "trailOn", true); o.trailTime = (float)ov.num("trailTime", o.trailTime); o.trailWidth = (float)ov.num("trailWidth", o.trailWidth);
            readArr(ov, "lightColour", o.lightColour, 3); readArr(ov, "pColourStart", o.pColourStart, 4); readArr(ov, "pColourEnd", o.pColourEnd, 4);
            readArr(ov, "lineColour", o.lineColour, 4); readArr(ov, "trailColour", o.trailColour, 4);
            o.hasBody = readBool(ov, "hasBody", false); o.mass = (float)ov.num("mass", 1); o.drag = (float)ov.num("drag", 0); o.angularDrag = (float)ov.num("angularDrag", 0.05);
            o.useGravity = readBool(ov, "useGravity", true); o.isKinematic = readBool(ov, "isKinematic", false); o.freeze = (int)ov.num("freeze", 0);
            o.hasCollider = readBool(ov, "hasCollider", false); o.colliderOn = readBool(ov, "colliderOn", true); o.isTrigger = readBool(ov, "isTrigger", false);
            o.colShape = (int)ov.num("colShape", 0); o.colRadius = (float)ov.num("colRadius", 0.5); o.colHeight = (float)ov.num("colHeight", 2);
            o.friction = (float)ov.num("friction", 0.6); o.bounciness = (float)ov.num("bounciness", 0);
            readArr(ov, "colCentre", o.colCentre, 3); readArr(ov, "colSize", o.colSize, 3);
            if (const Json::Value* lp = ov.get("linePoints")) if (lp->type == Json::Value::ARR && lp->a.size() >= 6) {
                o.linePoints.clear(); for (const auto& v : lp->a) o.linePoints.push_back((float)v.n);
                o.linePoints.resize(o.linePoints.size() / 3 * 3);
            }
            o.hasAnim = readBool(ov, "hasAnim", o.anim != A_STILL);
            if (const Json::Value* ss = ov.get("scripts"))
                for (const Json::Value& sv : ss->a) { Script sc; sc.file = sv.str("file"); sc.on = readBool(sv, "on", true); if (!sc.file.empty()) o.scripts.push_back(sc); }
            readArr(ov, "position", o.pos, 3); readArr(ov, "rotation", o.rot, 3); readArr(ov, "scale", o.scl, 3); readArr(ov, "colour", o.colour, 4);
            k.objects.push_back(o);
            p.nextId = std::max(p.nextId, o.id + 1);
        }
    p.nextId = std::max(p.nextId, k.id + 1);
    return k;
}

// the camera's calibration, as camera.json has it
static std::string calibJson(const Project& p) {
    std::string s = ",\n  \"calibration\": {\"width\": " + std::to_string(p.calibW) + ", \"height\": " + std::to_string(p.calibH) + ", \"K\": [";
    for (int i = 0; i < 4; ++i) s += (i ? ", " : "") + fnum((float)p.calibK[i]);
    s += "], \"dist\": [";
    char b[32];
    for (size_t i = 0; i < p.calibDist.size(); ++i) { std::snprintf(b, sizeof b, "%s%.6f", i ? ", " : "", p.calibDist[i]); s += b; }
    std::snprintf(b, sizeof b, "%.4f", p.calibRms);
    return s + "], \"rms\": " + b + "}";
}

std::string toJson(const Project& p) {
    std::string out = "{\n  \"diorama\": 1,\n  \"title\": " + Json::quote(p.title) + ",\n  \"tracking\": \"" + TRACKING_NAMES[p.tracking] +
                      "\",\n  \"gps\": " + (p.gps ? "true" : "false") + ",\n  \"nextId\": " + std::to_string(p.nextId) + ",\n  \"scene\": " + Json::quote(p.scene) + ",\n  \"scenes\": [";
    for (size_t i = 0; i < p.scenes.size(); ++i) out += (i ? ", " : "") + Json::quote(p.scenes[i]);
    out += "],\n  \"platform\": " + Json::quote(p.platform) + ", \"windowsRuntime\": " + std::to_string(p.windowsRuntime) + ", \"arucoDict\": " + std::to_string(p.arucoDict) + ", \"renderer\": " + std::to_string(p.renderer) +
           (p.calibW > 0 ? calibJson(p) : std::string()) +
           ",\n  \"shadows\": " + (p.shadows ? "true" : "false") + ", \"shadowQuality\": " + std::to_string(p.shadowQuality) +
           ", \"bloom\": " + (p.bloom ? "true" : "false") + ", \"bloomStrength\": " + fnum(p.bloomStrength) + ", \"bloomThreshold\": " + fnum(p.bloomThreshold) +
           ", \"toneMapping\": " + std::to_string(p.toneMapping) + ", \"exposure\": " + fnum(p.exposure) +
           ",\n  \"company\": " + Json::quote(p.company) + ", \"version\": " + Json::quote(p.version) + ", \"packageId\": " + Json::quote(p.packageId) +
           ",\n  \"icon\": " + Json::quote(p.icon) + ", \"splash\": " + Json::quote(p.splash) + ", \"intro\": " + Json::quote(p.intro) +
           ",\n  \"skipSplash\": " + (p.skipSplash ? "true" : "false") + ", \"fullscreen\": " + (p.fullscreen ? "true" : "false") +
           ", \"orientation\": " + std::to_string(p.orientation);
    out += ",\n  \"markers\": [";
    out += markersJson(p.markers);
    // the Main Camera: its children ride with the phone; and how it draws
    out += "\n  ],\n  \"camera\": " + markersJson({p.camera}).substr(5) +
           ",\n  \"camClear\": " + std::to_string(p.camClear) + ", \"camNear\": " + fnum(p.camNear) + ", \"camFar\": " + fnum(p.camFar) +
           ", \"camFov\": " + fnum(p.camFov) + ", \"camColour\": " + arr(p.camColour, 3) + ", \"gravity\": " + arr(p.gravity, 3);
    return out + "\n}\n";
}

bool fromJson(const std::string& text, Project& p, std::string& err) {
    Json::Value v;
    if (!Json::parse(text, v) || v.type != Json::Value::OBJ) { err = "Not a Diorama project"; return false; }
    p = Project();
    p.title = v.str("title", p.title);
    p.nextId = (int)v.num("nextId", 1);
    p.tracking = indexOf(TRACKING_NAMES, TRACKING_COUNT, v.str("tracking", "markers"), TRACK_MARKERS);
    { const Json::Value* g = v.get("gps"); p.gps = g && g->type == Json::Value::BOOL && g->b; }
    if (const Json::Value* sc = v.get("scenes")) if (sc->type == Json::Value::ARR) {
        std::vector<std::string> names; for (const auto& n : sc->a) if (n.type == Json::Value::STR && !n.s.empty()) names.push_back(n.s);
        if (!names.empty()) p.scenes = names;
    }
    p.scene = v.str("scene", p.scenes[0]);
    p.platform = v.str("platform", "web"); p.windowsRuntime = (int)v.num("windowsRuntime", 0); p.renderer = (int)v.num("renderer", 0); p.arucoDict = (int)v.num("arucoDict", 0);
    if (const Json::Value* c = v.get("calibration")) if (c->type == Json::Value::OBJ) {
        const Json::Value* K = c->get("K");
        if (K && K->type == Json::Value::ARR && K->a.size() == 4) {
            p.calibW = (int)c->num("width", 0); p.calibH = (int)c->num("height", 0); p.calibRms = c->num("rms", 0);
            for (int i = 0; i < 4; ++i) p.calibK[i] = K->a[i].n;
            if (const Json::Value* d = c->get("dist")) if (d->type == Json::Value::ARR) for (const auto& x : d->a) p.calibDist.push_back(x.n);
        }
    }
    { const Json::Value* b = v.get("shadows"); p.shadows = !b || b->type != Json::Value::BOOL || b->b; }
    { const Json::Value* b = v.get("bloom"); p.bloom = !b || b->type != Json::Value::BOOL || b->b; }
    p.shadowQuality = (int)v.num("shadowQuality", 1); p.bloomStrength = (float)v.num("bloomStrength", 0.6); p.bloomThreshold = (float)v.num("bloomThreshold", 0.9);
    p.toneMapping = (int)v.num("toneMapping", 1); p.exposure = (float)v.num("exposure", 1);
    p.company = v.str("company", p.company); p.version = v.str("version", p.version); p.packageId = v.str("packageId");
    p.icon = v.str("icon"); p.splash = v.str("splash"); p.intro = v.str("intro");
    { const Json::Value* b = v.get("skipSplash"); p.skipSplash = b && b->type == Json::Value::BOOL && b->b; }
    { const Json::Value* b = v.get("fullscreen"); p.fullscreen = b && b->type == Json::Value::BOOL && b->b; }
    p.orientation = (int)v.num("orientation", 0);
    if (std::find(p.scenes.begin(), p.scenes.end(), p.scene) == p.scenes.end()) p.scene = p.scenes[0];
    if (const Json::Value* cv = v.get("camera")) if (cv->type == Json::Value::OBJ) p.camera = markerFrom(*cv, p);
    p.camClear = (int)v.num("camClear", 0); p.camNear = (float)v.num("camNear", p.camNear); p.camFar = (float)v.num("camFar", p.camFar); p.camFov = (float)v.num("camFov", p.camFov);
    readArr(v, "camColour", p.camColour, 3);
    readArr(v, "gravity", p.gravity, 3);
    const Json::Value* ms = v.get("markers");
    if (!ms || ms->type != Json::Value::ARR) return true;
    for (const Json::Value& mv : ms->a) p.markers.push_back(markerFrom(mv, p));
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
// plane detection: the planes found shown, a tap on one puts a cube there (the gui adds PlaceCubes.js to the Spawner)
Project planeDemoTemplate() {
    Project p;
    p.title = "Plane detection demo";
    p.tracking = TRACK_SURFACES;
    p.intro = "Move the camera slowly over the floor or a table until the dots show the planes found.\nThen tap a plane: a cube lands there.";
    Marker k; k.id = newId(p); k.name = "Room"; k.preset.clear(); k.placement = 1; k.planeDetect = 2;
    Object cube = make(p, CUBE, -1); cube.name = "Cube"; cube.scl[0] = cube.scl[1] = cube.scl[2] = 0.1f; cube.pos[1] = 0.05f;
    cube.colour[0] = 0; cube.colour[1] = 0.48f; cube.colour[2] = 0.72f;
    Object spawner = make(p, EMPTY, -1); spawner.name = "Spawner";
    Object canvas = make(p, EMPTY, -1); canvas.name = "Canvas"; canvas.hasCanvas = true;
    k.objects = {cube, spawner, canvas};
    Object count = make(p, EMPTY, canvas.id); count.name = "Count"; count.ui = UI_TEXT; count.uiText = "Tap a plane to place a cube";
    count.uiAnchor = 1; count.uiRect[1] = 24; count.uiRect[2] = 360; count.uiRect[3] = 40; count.uiFont = 20;
    Object clear = make(p, EMPTY, canvas.id); clear.name = "Clear"; clear.ui = UI_BUTTON; clear.uiText = "Clear"; clear.uiFont = 16;
    clear.uiAnchor = 2; clear.uiRect[0] = -12; clear.uiRect[1] = 16; clear.uiRect[2] = 110; clear.uiRect[3] = 38;
    clear.uiOnClick = "find('Spawner').clear()";
    k.objects.push_back(count); k.objects.push_back(clear);
    p.markers.push_back(k);
    return p;
}
// AR indoor navigation, as in "Augmented Reality in Indoor Navigation" (IJSREM, 2023): planes found, a route marked with
// arrows, a guide that walks it; buttons to save and follow paths (the game records walked paths too)
Project navigationTemplate() {
    Project p;
    p.title = "Indoor navigation";
    p.tracking = TRACK_SURFACES;
    p.intro = "Walk slowly and look at the floor until it's found (dots).\nThen follow the arrows, or the guide.";
    Marker k; k.id = newId(p); k.name = "Floor"; k.preset.clear(); k.placement = 1;
    Object route = make(p, EMPTY, -1); route.name = "Route"; route.hasPath = true;
    route.pathPoints = {0, 0, 0.6f, 0, 0, -0.6f, -0.5f, 0, -1.4f, -1.3f, 0, -1.8f, -1.6f, 0, -2.2f};   // round the table, to the wall
    Object guide = make(p, CAPSULE, -1); guide.name = "Guide"; guide.hasFollower = true; guide.followPath = "Path 1";
    guide.scl[0] = guide.scl[1] = guide.scl[2] = 0.2f; guide.pos[1] = 0.2f; guide.colour[0] = 0.95f; guide.colour[1] = 0.95f; guide.colour[2] = 0.95f;
    Object sign = make(p, WORDS, -1); sign.name = "Sign"; sign.text = "Meeting room"; sign.pos[0] = -1.6f; sign.pos[1] = 1.0f; sign.pos[2] = -2.45f;
    sign.scl[0] = sign.scl[1] = sign.scl[2] = 0.8f; sign.billboard = true;
    // the paper's buttons, as a Canvas
    Object canvas = make(p, EMPTY, -1); canvas.name = "Canvas"; canvas.hasCanvas = true;
    k.objects = {route, guide, sign, canvas};
    const char* names[] = {"Follow Path 1", "Save Path 1", "Follow Path 2", "Save Path 2", "Reset"};
    // the first tap on a Save button starts walking it in; the second saves it
    const char* clicks[] = {"nav.follow('Path 1')", "button.getComponent('Button').text = nav.toggle('Path 1') === 'recording' ? 'Stop + save' : 'Save Path 1'",
                            "nav.follow('Path 2')", "button.getComponent('Button').text = nav.toggle('Path 2') === 'recording' ? 'Stop + save' : 'Save Path 2'", "nav.reset()"};
    for (int i = 0; i < 5; ++i) {
        Object b = make(p, EMPTY, canvas.id); b.name = names[i]; b.ui = UI_BUTTON; b.uiText = names[i]; b.uiFont = 15; b.uiOnClick = clicks[i];
        b.uiAnchor = 2; b.uiRect[0] = -12; b.uiRect[1] = 16 + i * 46.0f; b.uiRect[2] = 140; b.uiRect[3] = 38;
        b.uiBg[0] = 0.92f; b.uiBg[1] = 0.92f; b.uiBg[2] = 0.92f; b.uiBg[3] = 0.9f; b.uiColour[0] = b.uiColour[1] = b.uiColour[2] = 0.15f;
        k.objects.push_back(b);
    }
    p.markers.push_back(k);
    return p;
}
Project physicsTemplate() {
    // Rigidbodies and Colliders on Hiro: things fall onto the paper, a paddle (kinematic, spinning) sweeps a stack over,
    // a trigger zone glows while something's in it, a tap drops another ball (the scripts: gui.cpp writes them)
    Project p;
    p.title = "Physics";
    Marker k; k.id = newId(p); k.name = "Hiro"; k.preset = "hiro";
    auto body = [&](int kind, const char* name, float x, float y, float z, float size, int shape) {
        Object o = make(p, kind, -1); o.name = name; o.pos[0] = x; o.pos[1] = y; o.pos[2] = z;
        o.scl[0] = o.scl[1] = o.scl[2] = size;
        if (kind == CAPSULE) o.scl[1] = size;   // Unity's capsule: 2 high
        o.hasBody = true; o.hasCollider = true; fitCollider(o, shape);
        return o;
    };
    for (int i = 0; i < 4; ++i) {   // a stack
        Object c = body(CUBE, "Crate", -0.25f, 0.1f + 0.2f * i, -0.1f, 0.2f, COL_BOX); c.name += " " + std::to_string(i + 1);
        const float sh = i % 2 ? 0.85f : 1.0f;
        c.colour[0] = 0.62f * sh; c.colour[1] = 0.45f * sh; c.colour[2] = 0.28f * sh; c.mass = 0.5f; k.objects.push_back(c);
    }
    Object ball = body(SPHERE, "Ball", 0.25f, 0.9f, 0.05f, 0.15f, COL_SPHERE);
    ball.colour[0] = 0; ball.colour[1] = 0.48f; ball.colour[2] = 0.72f; ball.bounciness = 0.7f; ball.metallic = 0.2f; ball.roughness = 0.3f;
    k.objects.push_back(ball);
    Object pill = body(CAPSULE, "Pill", 0.05f, 0.7f, 0.3f, 0.08f, COL_CAPSULE); pill.rot[2] = 70;
    pill.colour[0] = 0.9f; pill.colour[1] = 0.9f; pill.colour[2] = 0.92f; k.objects.push_back(pill);
    Object paddle = make(p, CUBE, -1); paddle.name = "Paddle";   // kinematic: moved by Animate, pushes, isn't pushed; its end sweeps the stack
    paddle.pos[0] = 0.1f; paddle.pos[1] = 0.04f; paddle.pos[2] = -0.1f; paddle.scl[0] = 0.8f; paddle.scl[1] = 0.08f; paddle.scl[2] = 0.06f;
    paddle.colour[0] = 0.25f; paddle.colour[1] = 0.25f; paddle.colour[2] = 0.27f;
    paddle.hasAnim = true; paddle.anim = A_SPIN; paddle.animSpeed = 0.15f;
    paddle.hasBody = true; paddle.isKinematic = true; paddle.hasCollider = true; fitCollider(paddle, COL_BOX);
    k.objects.push_back(paddle);
    Object zone = make(p, CUBE, -1); zone.name = "Zone";   // a trigger: things pass through, its script hears them
    zone.pos[0] = 0.3f; zone.pos[1] = 0.125f; zone.pos[2] = -0.3f; zone.scl[0] = zone.scl[1] = zone.scl[2] = 0.25f;
    zone.colour[0] = 0.2f; zone.colour[1] = 0.9f; zone.colour[2] = 0.5f; zone.colour[3] = 0.35f; zone.castShadows = false;
    zone.hasCollider = true; zone.isTrigger = true; fitCollider(zone, COL_BOX);
    k.objects.push_back(zone);
    Object words = make(p, WORDS, -1); words.name = "Tap"; words.text = "Tap to drop a ball"; words.pos[1] = 1.0f; words.scl[0] = words.scl[1] = words.scl[2] = 0.35f;
    k.objects.push_back(words);
    Object spawner = make(p, EMPTY, -1); spawner.name = "Spawner"; k.objects.push_back(spawner);
    p.markers.push_back(k);
    return p;
}

Project basicTemplate() {
    Project p;
    p.title = "My diorama";
    Marker k; k.id = newId(p); k.name = "Hiro"; k.preset = "hiro";
    Object cube = make(p, CUBE, -1); cube.colour[0] = 0; cube.colour[1] = 0.48f; cube.colour[2] = 0.72f; cube.anim = A_SPIN; cube.hasAnim = true; cube.animSpeed = 0.5f;
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
    rays1.anim = A_PULSE; rays1.hasAnim = true; rays1.animSpeed = 0.4f; rays1.animAmount = 0.25f;
    Object rays2 = pic("Godrays", "tex_godrays.png", 0.116f, -0.175f, 0, 1.9155f); rays2.glow = true; rays2.colour[3] = 0.6f;
    rays2.anim = A_PULSE; rays2.hasAnim = true; rays2.animSpeed = 0.3f; rays2.animAmount = 0.3f;
    Object woman = pic("Woman", "tex_woman00.png", 0.288f, -0.118f, -0.278f, 1.3644f);
    woman.anim = A_FLOAT; woman.hasAnim = true; woman.animSpeed = 0.5f; woman.animAmount = 0.015f;
    Object bf = make(p, SPRITES, root.id); bf.name = "Butterflies"; bf.file = dir + "tex_butterfly.png"; bf.count = 10; bf.spriteSize = 0.22f;
    bf.scl[0] = 1.6f; bf.scl[2] = 1.1f; bf.scl[1] = 1.1f; bf.pos[1] = 0.15f; bf.animSpeed = 0.6f;
    k.objects = {root, bg, rays1, rays2, woman, frame, bf};
    p.markers.push_back(k);
    return p;
}

}
