// Diorama - physics on Jolt Physics (MIT). See physics.h; the web game's twin is Physics in assets/web/diorama.js.
#include "physics.h"
#include "model.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

using namespace JPH;
using Scene::Object;

namespace Physics {

static const float STEP = 1.0f / 60;   // Unity's fixed timestep is 1/50; 60 matches the screen
static const int MAX_STEPS = 4;        // a slow frame catches up at most this much (then the world slows down)
static const uint64 FLOOR = ~uint64(0);
static const float D2R = 3.14159265f / 180;

// ---- once: Jolt's allocator, its factory and its shapes' types
static void joltOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
}

// ---- transforms: Diorama's (position, rotation in degrees applied z, x, then y, scale) <-> Jolt's
static Quat quatOf(const float r[3]) {   // Ry * Rx * Rz (three.js's 'YXZ')
    return Quat::sRotation(Vec3::sAxisY(), r[1] * D2R) * Quat::sRotation(Vec3::sAxisX(), r[0] * D2R) * Quat::sRotation(Vec3::sAxisZ(), r[2] * D2R);
}
static void eulerOf(Quat q, float out[3]) {   // back to degrees, as three.js's Euler.setFromQuaternion(q, 'YXZ')
    const Mat44 m = Mat44::sRotation(q.Normalized());
    const float m13 = m(0, 2), m21 = m(1, 0), m22 = m(1, 1), m23 = m(1, 2), m31 = m(2, 0), m11 = m(0, 0), m33 = m(2, 2);
    const float x = std::asin(-std::clamp(m23, -1.0f, 1.0f));
    float y, z;
    if (std::fabs(m23) < 0.9999999f) { y = std::atan2(m13, m33); z = std::atan2(m21, m22); }
    else { y = std::atan2(-m31, m11); z = 0; }
    out[0] = x / D2R; out[1] = y / D2R; out[2] = z / D2R;
}
// an object's own transform, with Animate added on (as the viewer draws it), dynamic bodies without (physics moves them)
static Mat44 localOf(const Object& o, float t, bool withAnim) {
    float pos[3] = {o.pos[0], o.pos[1], o.pos[2]}, rot[3] = {o.rot[0], o.rot[1], o.rot[2]};
    if (withAnim && o.hasAnim && o.animOn) {
        const float ph = t * o.animSpeed * 6.2831853f;
        if (o.anim == Scene::A_FLOAT) pos[1] += std::sin(ph) * o.animAmount;
        else if (o.anim == Scene::A_SPIN) rot[1] += t * o.animSpeed * 360.0f;
        else if (o.anim == Scene::A_SWAY) rot[2] += std::sin(ph) * o.animAmount * 90.0f;
    }
    return Mat44::sRotationTranslation(quatOf(rot), Vec3(pos[0], pos[1], pos[2])) * Mat44::sScale(Vec3(o.scl[0], o.scl[1], o.scl[2]));
}
struct Pose { Vec3 p; Quat q; Vec3 s; };
static Pose poseOf(const Mat44& m) {   // position, rotation and (lossy) scale of a world matrix
    Pose r;
    r.p = m.GetTranslation();
    const Vec3 c0 = m.GetAxisX(), c1 = m.GetAxisY(), c2 = m.GetAxisZ();
    r.s = Vec3(c0.Length(), c1.Length(), c2.Length());
    Mat44 n = Mat44::sIdentity();
    n.SetAxisX(r.s.GetX() > 1e-9f ? c0 / r.s.GetX() : Vec3::sAxisX());
    n.SetAxisY(r.s.GetY() > 1e-9f ? c1 / r.s.GetY() : Vec3::sAxisY());
    n.SetAxisZ(r.s.GetZ() > 1e-9f ? c2 / r.s.GetZ() : Vec3::sAxisZ());
    r.q = n.GetQuaternion().Normalized();
    return r;
}

// ---- shapes
static std::map<std::string, std::vector<Vec3>> hullCache;   // a model's points (its rest pose), by file
static std::vector<Vec3> pointsOf(const Object& o) {
    std::vector<Vec3> v;
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        for (int i = 0; i < 8; ++i) v.push_back(Vec3(i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0));
    };
    auto round = [&](float r, float y0, float y1, bool caps) {   // a cylinder (caps: a capsule's half spheres)
        for (int k = 0; k < 16; ++k) {
            const float a = k * 6.2831853f / 16, x = std::cos(a) * r, z = std::sin(a) * r;
            v.push_back(Vec3(x, y0, z)); v.push_back(Vec3(x, y1, z));
        }
        if (caps) for (int j = 1; j <= 3; ++j) for (int k = 0; k < 16; ++k) {
            const float e = j * 1.5707963f / 4, a = k * 6.2831853f / 16, rr = std::cos(e) * r, h = std::sin(e) * r;
            v.push_back(Vec3(std::cos(a) * rr, y1 + h, std::sin(a) * rr)); v.push_back(Vec3(std::cos(a) * rr, y0 - h, std::sin(a) * rr));
        }
        if (caps) { v.push_back(Vec3(0, y1 + r, 0)); v.push_back(Vec3(0, y0 - r, 0)); }
    };
    switch (o.kind) {
    case Scene::SPHERE:
        for (int j = 0; j <= 8; ++j) for (int k = 0; k < 16; ++k) {
            const float e = -1.5707963f + j * 3.14159265f / 8, a = k * 6.2831853f / 16;
            v.push_back(Vec3(std::cos(e) * std::cos(a) * 0.5f, std::sin(e) * 0.5f, std::cos(e) * std::sin(a) * 0.5f));
        }
        break;
    case Scene::CYLINDER: round(0.5f, -1, 1, false); break;
    case Scene::CAPSULE: round(0.5f, -0.5f, 0.5f, true); break;
    case Scene::PLANE: box(-5, 0, -5, 5, 0, 5); break;
    case Scene::QUAD: case Scene::PICTURE: case Scene::WORDS: box(-0.5f, -0.5f, 0, 0.5f, 0.5f, 0); break;
    case Scene::MODEL: {
        auto it = hullCache.find(o.file);
        if (it == hullCache.end()) {
            std::vector<Vec3> pts;
            Model::Model m;
            if (Model::load(o.file, m)) {
                Model::pose(m, -1, 0, false);
                size_t n = 0; for (const auto& pr : m.prims) n += pr.outPos.size() / 3;
                const size_t every = std::max<size_t>(1, n / 3000);   // a few thousand points make the same hull
                size_t k = 0;
                for (const auto& pr : m.prims) for (size_t i = 0; i + 2 < pr.outPos.size(); i += 3, ++k)
                    if (k % every == 0) pts.push_back(Vec3(pr.outPos[i], pr.outPos[i + 1], pr.outPos[i + 2]));
            }
            it = hullCache.emplace(o.file, std::move(pts)).first;
        }
        v = it->second;
        break;
    }
    default: box(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f); break;
    }
    if (v.size() < 4) { v.clear(); box(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f); }
    return v;
}
// the collider's shape in the anchor's space: its size times the object's scale there (s)
static RefConst<Shape> shapeOf(const Object& o, Vec3 s) {
    s = s.Abs();
    RefConst<Shape> shape;
    auto boxOf = [](Vec3 half) {
        half = Vec3::sMax(half, Vec3::sReplicate(0.0005f));
        return RefConst<Shape>(new BoxShape(half, std::min(0.05f, half.ReduceMin() * 0.5f)));
    };
    switch (o.colShape) {
    case Scene::COL_SPHERE:
        shape = new SphereShape(std::max(0.0005f, o.colRadius * s.ReduceMax()));
        break;
    case Scene::COL_CAPSULE: {
        const float r = std::max(0.0005f, o.colRadius * std::max(s.GetX(), s.GetZ()));
        const float half = o.colHeight * s.GetY() / 2 - r;
        shape = half > 1e-4f ? RefConst<Shape>(new CapsuleShape(half, r)) : RefConst<Shape>(new SphereShape(r));
        break;
    }
    case Scene::COL_MESH: {
        Array<Vec3> pts;
        for (const Vec3& p : pointsOf(o)) pts.push_back(p * s);
        Vec3 lo = Vec3::sReplicate(1e30f), hi = Vec3::sReplicate(-1e30f);
        for (const Vec3& p : pts) { lo = Vec3::sMin(lo, p); hi = Vec3::sMax(hi, p); }
        const float thin = (hi - lo).ReduceMin();
        if (thin > 0.002f) {   // flat things (a plane, a quad) can't be hulls: a thin box instead
            ConvexHullShapeSettings hs(pts, std::min(0.05f, thin * 0.1f));
            Shape::ShapeResult r = hs.Create();
            if (r.IsValid()) { shape = r.Get(); return shape; }   // a mesh collider has no Centre (it's the mesh)
        }
        shape = new RotatedTranslatedShape((lo + hi) / 2, Quat::sIdentity(), boxOf((hi - lo) / 2));
        return shape;
    }
    default:
        shape = boxOf(Vec3(o.colSize[0], o.colSize[1], o.colSize[2]) * s / 2);
        break;
    }
    const Vec3 c = Vec3(o.colCentre[0], o.colCentre[1], o.colCentre[2]) * s;
    if (c.LengthSq() > 1e-12f) shape = new RotatedTranslatedShape(c, Quat::sIdentity(), shape);
    return shape;
}

// ---- the world
struct Rec {
    BodyID id;
    int anchor = 0, object = 0;
    bool dynamic = false, trigger = false;
    std::string sig;                    // what it was made from: a change makes it again
    float wrotePos[3] = {0, 0, 0}, wroteRot[3] = {0, 0, 0};   // dynamic: what was written into the Transform last
    Vec3 target = Vec3::sZero(); Quat targetQ = Quat::sIdentity();   // kinematic: where it's going this frame
    bool seen = false;
};
struct Event { bool add; uint64 a, b; bool trigger; Vec3 point, normal; float speed; };
struct Listener : ContactListener {
    std::vector<Event>* out = nullptr;
    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const ContactManifold& m, ContactSettings&) override {
        Event e; e.add = true; e.a = b1.GetUserData(); e.b = b2.GetUserData(); e.trigger = b1.IsSensor() || b2.IsSensor();
        e.point = Vec3(m.GetWorldSpaceContactPointOn1(0)); e.normal = m.mWorldSpaceNormal;
        e.speed = (b2.GetLinearVelocity() - b1.GetLinearVelocity()).Length();
        out->push_back(e);
    }
    void OnContactRemoved(const SubShapeIDPair& pair) override {
        Event e; e.add = false; e.a = pair.GetBody1ID().GetIndexAndSequenceNumber(); e.b = pair.GetBody2ID().GetIndexAndSequenceNumber();   // bodies: named below
        e.trigger = false; e.point = Vec3::sZero(); e.normal = Vec3::sAxisY(); e.speed = 0;
        out->push_back(e);
    }
};
struct World {
    int anchors = 1;
    std::unique_ptr<BroadPhaseLayerInterfaceTable> bp;
    std::unique_ptr<ObjectLayerPairFilterTable> pairs;
    std::unique_ptr<ObjectVsBroadPhaseLayerFilterTable> objVsBp;
    std::unique_ptr<PhysicsSystem> sys;
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystemSingleThreaded> jobs;   // one thread: the same order of events as the web game
    Listener listener;
    std::vector<Event> events;
    std::unordered_map<int, Rec> bodies;            // by object id
    std::unordered_map<uint32, uint64> byBody;       // a body (index and sequence) -> its user data (object id, or FLOOR)
    std::vector<BodyID> floors;                      // each anchor's floor (or invalid)
    std::map<std::pair<uint64, uint64>, std::pair<int, bool>> touching;   // a pair in contact: how many contacts, trigger
    std::vector<Contact> contacts;
    float acc = 0;
    Vec3 gravity = Vec3(0, -9.81f, 0);
};
static ObjectLayer layerOf(int anchor, bool moving) { return ObjectLayer(anchor * 2 + (moving ? 1 : 0)); }

bool any(const Scene::Project& p) {
    for (const auto& m : p.markers) for (const auto& o : m.objects) if (o.hasCollider) return true;
    return false;
}

World* create(const Scene::Project& p) {
    joltOnce();
    World* w = new World();
    w->anchors = std::max<int>(1, (int)p.markers.size());
    const uint layers = uint(w->anchors * 2);
    w->bp = std::make_unique<BroadPhaseLayerInterfaceTable>(layers, 2);
    w->pairs = std::make_unique<ObjectLayerPairFilterTable>(layers);
    for (int a = 0; a < w->anchors; ++a) {
        w->bp->MapObjectToBroadPhaseLayer(layerOf(a, false), BroadPhaseLayer(0));
        w->bp->MapObjectToBroadPhaseLayer(layerOf(a, true), BroadPhaseLayer(1));
        w->pairs->EnableCollision(layerOf(a, true), layerOf(a, false));
        w->pairs->EnableCollision(layerOf(a, true), layerOf(a, true));
    }
    w->objVsBp = std::make_unique<ObjectVsBroadPhaseLayerFilterTable>(*w->bp, 2, *w->pairs, layers);
    w->sys = std::make_unique<PhysicsSystem>();
    w->sys->Init(4096, 0, 8192, 8192, *w->bp, *w->objVsBp, *w->pairs);
    w->temp = std::make_unique<TempAllocatorImpl>(8 * 1024 * 1024);
    w->jobs = std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);
    w->listener.out = &w->events;
    w->sys->SetContactListener(&w->listener);
    w->gravity = Vec3(p.gravity[0], p.gravity[1], p.gravity[2]);
    w->sys->SetGravity(w->gravity);
    BodyInterface& bi = w->sys->GetBodyInterface();
    w->floors.assign(w->anchors, BodyID());
    for (int a = 0; a < (int)p.markers.size(); ++a) {   // the paper, table or floor: a big slab under y = 0
        if (!p.markers[a].physicsFloor || p.tracking == Scene::TRACK_FACES) continue;
        BodyCreationSettings bs(new BoxShape(Vec3(100, 0.5f, 100)), RVec3(0, -0.5f, 0), Quat::sIdentity(), EMotionType::Static, layerOf(a, false));
        bs.mUserData = FLOOR; bs.mFriction = 0.6f;
        w->floors[a] = bi.CreateAndAddBody(bs, EActivation::DontActivate);
        w->byBody[w->floors[a].GetIndexAndSequenceNumber()] = FLOOR;
    }
    return w;
}

static void removeBody(World* w, std::unordered_map<int, Rec>::iterator it) {
    BodyInterface& bi = w->sys->GetBodyInterface();
    const uint64 me = uint64(uint32(it->first));
    for (auto t = w->touching.begin(); t != w->touching.end();) {   // what it touched hears it leave
        if (t->first.first == me || t->first.second == me) {
            Contact c; c.a = int(int32(uint32(t->first.first))); c.b = t->first.second == FLOOR ? -1 : int(int32(uint32(t->first.second)));
            c.enter = false; c.trigger = t->second.second;
            w->contacts.push_back(c);
            t = w->touching.erase(t);
        } else ++t;
    }
    w->byBody.erase(it->second.id.GetIndexAndSequenceNumber());
    bi.RemoveBody(it->second.id); bi.DestroyBody(it->second.id);
    w->bodies.erase(it);
}

void destroy(World* w) {
    if (!w) return;
    while (!w->bodies.empty()) removeBody(w, w->bodies.begin());
    BodyInterface& bi = w->sys->GetBodyInterface();
    for (BodyID f : w->floors) if (!f.IsInvalid()) { bi.RemoveBody(f); bi.DestroyBody(f); }
    delete w;
}

static std::string signature(const Object& o, Vec3 s) {
    std::ostringstream k;
    k << o.colShape << ' ' << o.colSize[0] << ' ' << o.colSize[1] << ' ' << o.colSize[2] << ' ' << o.colCentre[0] << ' ' << o.colCentre[1] << ' '
      << o.colCentre[2] << ' ' << o.colRadius << ' ' << o.colHeight << ' ' << o.isTrigger << ' ' << o.friction << ' ' << o.bounciness << ' '
      << o.hasBody << ' ' << o.mass << ' ' << o.drag << ' ' << o.angularDrag << ' ' << o.useGravity << ' ' << o.isKinematic << ' ' << o.freeze << ' '
      << (o.colShape == Scene::COL_MESH ? o.kind : 0) << ' ' << (o.colShape == Scene::COL_MESH ? o.file : "") << ' '
      << int(std::round(s.GetX() * 1e4f)) << ' ' << int(std::round(s.GetY() * 1e4f)) << ' ' << int(std::round(s.GetZ() * 1e4f));
    return k.str();
}

void step(World* w, Scene::Project& p, float dt, float t) {
    if (!w) return;
    BodyInterface& bi = w->sys->GetBodyInterface();
    const Vec3 g(p.gravity[0], p.gravity[1], p.gravity[2]);
    if (g != w->gravity) { w->gravity = g; w->sys->SetGravity(g); }
    for (auto& kv : w->bodies) kv.second.seen = false;

    // 1. the bodies matched to the objects: made, changed, moved by scripts, or gone
    struct Item { int anchor; Object* o; Mat44 world, parent; bool hasParent; };
    std::vector<Item> items;
    for (int a = 0; a < (int)p.markers.size() && a < w->anchors; ++a) {
        auto& objs = p.markers[a].objects;
        std::unordered_map<int, std::pair<Mat44, bool>> worldOf;   // id -> world (anchor space), active
        for (auto& o : objs) {   // parents come before their children
            Mat44 parent = Mat44::sIdentity(); bool on = o.active;
            if (o.parent != -1) {
                auto pi = worldOf.find(o.parent);
                if (pi == worldOf.end()) on = false; else { parent = pi->second.first; on = on && pi->second.second; }
            }
            const bool dyn = o.hasBody && !o.isKinematic && o.freeze != 63;
            const Mat44 world = parent * localOf(o, t, !dyn);
            worldOf[o.id] = {world, on};
            if (on && o.hasCollider && o.colliderOn) items.push_back({a, &o, world, parent, o.parent != -1});
        }
    }
    for (auto& it : items) {
        Object& o = *it.o;
        const Pose pose = poseOf(it.world);
        const std::string sig = signature(o, pose.s);
        auto found = w->bodies.find(o.id);
        Vec3 keepV = Vec3::sZero(), keepW = Vec3::sZero();
        if (found != w->bodies.end() && (found->second.sig != sig || found->second.anchor != it.anchor)) {   // changed: made again, still moving
            keepV = bi.GetLinearVelocity(found->second.id); keepW = bi.GetAngularVelocity(found->second.id);
            removeBody(w, found); found = w->bodies.end();
        }
        const bool dyn = o.hasBody && !o.isKinematic && o.freeze != 63;
        if (found == w->bodies.end()) {
            const EMotionType mt = dyn ? EMotionType::Dynamic : EMotionType::Kinematic;
            BodyCreationSettings bs(shapeOf(o, pose.s), RVec3(pose.p), pose.q, mt, layerOf(it.anchor, true));
            bs.mUserData = uint64(uint32(o.id));
            bs.mIsSensor = o.isTrigger;
            bs.mFriction = std::max(0.0f, o.friction); bs.mRestitution = std::clamp(o.bounciness, 0.0f, 1.0f);
            bs.mLinearDamping = std::max(0.0f, o.drag); bs.mAngularDamping = std::max(0.0f, o.angularDrag);
            bs.mGravityFactor = o.hasBody && o.useGravity ? 1.0f : 0.0f;
            bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            bs.mMassPropertiesOverride.mMass = std::max(0.001f, o.hasBody ? o.mass : 1.0f);
            if (dyn) {
                bs.mMotionQuality = EMotionQuality::LinearCast;   // small, fast things don't pass through the floor
                bs.mAllowedDOFs = EAllowedDOFs(int(EAllowedDOFs::All) & ~o.freeze);
            }
            bs.mLinearVelocity = keepV; bs.mAngularVelocity = keepW;
            Rec b; b.anchor = it.anchor; b.object = o.id; b.dynamic = dyn; b.trigger = o.isTrigger; b.sig = sig;
            b.id = bi.CreateAndAddBody(bs, EActivation::Activate);
            if (b.id.IsInvalid()) continue;   // the world is full
            std::memcpy(b.wrotePos, o.pos, sizeof b.wrotePos); std::memcpy(b.wroteRot, o.rot, sizeof b.wroteRot);
            b.target = pose.p; b.targetQ = pose.q;
            w->byBody[b.id.GetIndexAndSequenceNumber()] = bs.mUserData;
            found = w->bodies.emplace(o.id, b).first;
        } else if (dyn) {   // a script (or the Inspector) put it somewhere: it goes there
            Rec& b = found->second;
            if (std::memcmp(b.wrotePos, o.pos, sizeof b.wrotePos) || std::memcmp(b.wroteRot, o.rot, sizeof b.wroteRot)) {
                bi.SetPositionAndRotation(b.id, RVec3(pose.p), pose.q, EActivation::Activate);
                std::memcpy(b.wrotePos, o.pos, sizeof b.wrotePos); std::memcpy(b.wroteRot, o.rot, sizeof b.wroteRot);
            }
        } else { found->second.target = pose.p; found->second.targetQ = pose.q; }
        found->second.seen = true;
    }
    for (auto it = w->bodies.begin(); it != w->bodies.end();) {   // gone: destroyed, switched off, its collider removed
        if (!it->second.seen) { auto next = std::next(it); removeBody(w, it); it = next; } else ++it;
    }

    // 2. the world, at 60 Hz
    w->acc = std::min(w->acc + std::max(0.0f, dt), STEP * MAX_STEPS);
    while (w->acc >= STEP) {
        w->acc -= STEP;
        for (auto& kv : w->bodies) {   // things moved by their Transform (Animate, scripts) get there, pushing what's in the way
            Rec& b = kv.second;
            if (b.dynamic) continue;
            RVec3 cp; Quat cq; bi.GetPositionAndRotation(b.id, cp, cq);
            if (!Vec3(cp).IsClose(b.target, 1e-12f) || !cq.IsClose(b.targetQ, 1e-12f)) bi.MoveKinematic(b.id, RVec3(b.target), b.targetQ, STEP);
            else if (bi.IsActive(b.id)) { bi.SetLinearVelocity(b.id, Vec3::sZero()); bi.SetAngularVelocity(b.id, Vec3::sZero()); }
        }
        w->sys->Update(STEP, 1, w->temp.get(), w->jobs.get());
    }

    // 3. where the moving ones got to, into their Transforms (in their parent's space)
    for (auto& it : items) {
        auto f = w->bodies.find(it.o->id);
        if (f == w->bodies.end() || !f->second.dynamic) continue;
        Rec& b = f->second; Object& o = *it.o;
        RVec3 cp; Quat cq; bi.GetPositionAndRotation(b.id, cp, cq);
        Vec3 lp = Vec3(cp); Quat lq = cq;
        if (it.hasParent) {
            lp = it.parent.Inversed() * lp;
            lq = poseOf(it.parent).q.Conjugated() * cq;
        }
        o.pos[0] = lp.GetX(); o.pos[1] = lp.GetY(); o.pos[2] = lp.GetZ();
        float e[3]; eulerOf(lq, e);
        o.rot[0] = e[0]; o.rot[1] = e[1]; o.rot[2] = e[2];
        std::memcpy(b.wrotePos, o.pos, sizeof b.wrotePos); std::memcpy(b.wroteRot, o.rot, sizeof b.wroteRot);
    }

    // 4. contacts that began or ended: each pair once (shapes touch at several places)
    for (const Event& e : w->events) {
        uint64 a = e.a, b = e.b;
        if (!e.add) {   // removed: the bodies by their ids
            auto ia = w->byBody.find(uint32(a)), ib = w->byBody.find(uint32(b));
            if (ia == w->byBody.end() || ib == w->byBody.end()) continue;   // already gone (it said so when it went)
            a = ia->second; b = ib->second;
        }
        if (a == FLOOR) std::swap(a, b);
        if (a == FLOOR) continue;
        const auto key = b != FLOOR && b < a ? std::make_pair(b, a) : std::make_pair(a, b);
        auto& tc = w->touching[key];
        auto idOf = [](uint64 u) { return u == FLOOR ? -1 : int(int32(uint32(u))); };
        if (e.add) {
            if (tc.first++ == 0) {
                tc.second = e.trigger;
                Contact c; c.a = idOf(key.first); c.b = idOf(key.second); c.enter = true; c.trigger = e.trigger; c.speed = e.speed;
                // Jolt's normal points from body 1 to body 2; a is pushed the other way when it's body 1
                const Vec3 n = key.first == e.a ? -e.normal : e.normal;
                c.point[0] = e.point.GetX(); c.point[1] = e.point.GetY(); c.point[2] = e.point.GetZ();
                c.normal[0] = n.GetX(); c.normal[1] = n.GetY(); c.normal[2] = n.GetZ();
                w->contacts.push_back(c);
            }
        } else if (tc.first > 0 && --tc.first == 0) {
            Contact c; c.a = idOf(key.first); c.b = idOf(key.second); c.enter = false; c.trigger = tc.second;
            w->contacts.push_back(c);
            w->touching.erase(key);
        } else if (tc.first == 0) w->touching.erase(key);
    }
    w->events.clear();
}

std::vector<Contact> takeContacts(World* w) {
    std::vector<Contact> c;
    if (w) c.swap(w->contacts);
    return c;
}

static Rec* bodyOf(World* w, int id) {
    if (!w) return nullptr;
    auto it = w->bodies.find(id);
    return it == w->bodies.end() ? nullptr : &it->second;
}
bool velocity(World* w, int id, bool angular, float v[3]) {
    Rec* b = bodyOf(w, id); if (!b) return false;
    BodyInterface& bi = w->sys->GetBodyInterface();
    const Vec3 r = angular ? bi.GetAngularVelocity(b->id) : bi.GetLinearVelocity(b->id);
    v[0] = r.GetX(); v[1] = r.GetY(); v[2] = r.GetZ();
    return true;
}
bool setVelocity(World* w, int id, bool angular, const float v[3]) {
    Rec* b = bodyOf(w, id); if (!b || !b->dynamic) return false;
    BodyInterface& bi = w->sys->GetBodyInterface();
    if (angular) bi.SetAngularVelocity(b->id, Vec3(v[0], v[1], v[2])); else bi.SetLinearVelocity(b->id, Vec3(v[0], v[1], v[2]));
    bi.ActivateBody(b->id);
    return true;
}
bool addForce(World* w, int id, const float f[3], int mode, bool torque) {
    Rec* b = bodyOf(w, id); if (!b || !b->dynamic) return false;
    BodyInterface& bi = w->sys->GetBodyInterface();
    const Vec3 v(f[0], f[1], f[2]);
    BodyLockWrite lock(w->sys->GetBodyLockInterface(), b->id);
    if (!lock.Succeeded()) return false;
    JPH::Body& body = lock.GetBody();
    MotionProperties* mp = body.GetMotionProperties();
    if (!torque) {
        const float mass = 1.0f / std::max(1e-9f, mp->GetInverseMass());
        switch (mode) {
        case 1: body.AddImpulse(v); break;                               // impulse: a kick (mass x velocity)
        case 2: body.AddForce(v * mass); break;                          // acceleration: the same for any mass
        case 3: body.SetLinearVelocity(body.GetLinearVelocity() + v); break;   // velocity change: a kick, whatever its mass
        default: body.AddForce(v); break;                                // force: a push for this step
        }
    } else {
        switch (mode) {
        case 1: body.AddAngularImpulse(v); break;
        case 2: { const Mat44 I = body.GetInverseInertia().Inversed3x3(); body.AddTorque(I * v); break; }
        case 3: body.SetAngularVelocity(body.GetAngularVelocity() + v); break;
        default: body.AddTorque(v); break;
        }
    }
    lock.ReleaseLock();
    bi.ActivateBody(b->id);
    return true;
}
bool sleeping(World* w, int id) { Rec* b = bodyOf(w, id); return b && !w->sys->GetBodyInterface().IsActive(b->id); }
bool wake(World* w, int id, bool on) {
    Rec* b = bodyOf(w, id); if (!b) return false;
    if (on) w->sys->GetBodyInterface().ActivateBody(b->id); else w->sys->GetBodyInterface().DeactivateBody(b->id);
    return true;
}
bool raycast(World* w, int anchor, const float from[3], const float dir[3], float maxDistance, Hit& hit) {
    if (!w) return false;
    Vec3 d(dir[0], dir[1], dir[2]);
    if (d.LengthSq() < 1e-12f || maxDistance <= 0) return false;
    d = d.Normalized() * std::min(maxDistance, 1e4f);
    RRayCast ray(RVec3(from[0], from[1], from[2]), d);
    AllHitCollisionCollector<CastRayCollector> all;
    RayCastSettings rs; rs.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
    w->sys->GetNarrowPhaseQuery().CastRay(ray, rs, all);
    all.Sort();
    const BodyLockInterface& li = w->sys->GetBodyLockInterface();
    for (const RayCastResult& r : all.mHits) {
        BodyLockRead lock(li, r.mBodyID);
        if (!lock.Succeeded()) continue;
        const JPH::Body& body = lock.GetBody();
        if (body.IsSensor() || body.GetObjectLayer() / 2 != anchor) continue;
        const RVec3 at = ray.GetPointOnRay(r.mFraction);
        const Vec3 n = body.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, at);
        hit.floor = body.GetUserData() == FLOOR;
        hit.id = hit.floor ? -1 : int(int32(uint32(body.GetUserData())));
        hit.point[0] = float(at.GetX()); hit.point[1] = float(at.GetY()); hit.point[2] = float(at.GetZ());
        hit.normal[0] = n.GetX(); hit.normal[1] = n.GetY(); hit.normal[2] = n.GetZ();
        hit.distance = r.mFraction * d.Length();
        return true;
    }
    return false;
}
int bodies(World* w) { return w ? (int)w->bodies.size() : 0; }

}
