// physicstest: Diorama's physics, measured. Makes a test scene (things dropped on a marker's floor: a cube turned
// over, a bouncing sphere, a capsule, a stack, a kinematic paddle that spins into them, a trigger zone), writes it as
// the game's scene.json, steps it at 60 frames a second and prints where everything is (and the contacts) as JSON
// lines. tools/physicstest/test.js does the same with the web game's physics.js (Jolt as WASM): the two should agree.
//
//   physicstest [out-folder] [seconds]     (writes out-folder/scene.json and prints the trace)
#include "physics.h"
#include "scene.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Scene;

static Object thing(Project& p, int kind, const char* name, float x, float y, float z) {
    Object o = make(p, kind, -1);
    o.name = name; o.pos[0] = x; o.pos[1] = y; o.pos[2] = z;
    return o;
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const double seconds = argc > 2 ? atof(argv[2]) : 3;
    Project p;
    p.title = "physicstest";
    Marker m; m.name = "Hiro"; m.id = newId(p);
    p.markers.push_back(m);
    Marker& mk = p.markers[0];
    auto add = [&](Object o) { mk.objects.push_back(o); return (int)mk.objects.size() - 1; };

    { Object o = thing(p, CUBE, "Tumbler", -0.3f, 0.6f, 0); o.rot[0] = 30; o.rot[2] = 20; o.hasBody = true; fitCollider(o, COL_BOX); o.hasCollider = true; add(o); }
    { Object o = thing(p, SPHERE, "Ball", 0.3f, 0.8f, 0); o.hasBody = true; fitCollider(o, COL_SPHERE); o.hasCollider = true; o.bounciness = 0.6f; add(o); }
    { Object o = thing(p, CAPSULE, "Pill", 0, 0.7f, 0.35f); o.rot[2] = 70; o.hasBody = true; fitCollider(o, COL_CAPSULE); o.hasCollider = true; add(o); }
    for (int i = 0; i < 3; ++i) {   // a stack
        Object o = thing(p, CUBE, "Stack", 0, 0.15f + 0.3f * i, -0.4f);
        o.name += std::to_string(i); o.hasBody = true; fitCollider(o, COL_BOX); o.hasCollider = true; o.mass = 0.5f; add(o);
    }
    {   // a paddle that spins (Animate) and sweeps the stack: kinematic, pushes, isn't pushed
        Object o = thing(p, CUBE, "Paddle", 0, 0.1f, -0.4f);
        o.scl[0] = 0.9f; o.scl[1] = 0.1f; o.scl[2] = 0.08f; o.pos[0] = -0.45f;
        o.hasAnim = true; o.animOn = true; o.anim = A_SPIN; o.animSpeed = 0.25f;
        fitCollider(o, COL_BOX); o.hasCollider = true; add(o);
    }
    {   // a trigger zone the ball falls through on its way down
        Object o = thing(p, CUBE, "Zone", 0.3f, 0.4f, 0); o.scl[0] = o.scl[1] = o.scl[2] = 0.3f; o.lookOn = false;
        fitCollider(o, COL_BOX); o.hasCollider = true; o.isTrigger = true; add(o);
    }
    {   // a mesh collider (a cylinder's hull) sliding off a tilted, fixed ramp
        Object r = thing(p, CUBE, "Ramp", 0.6f, 0.1f, 0.4f); r.scl[0] = 0.6f; r.scl[1] = 0.04f; r.scl[2] = 0.3f; r.rot[2] = -20;
        fitCollider(r, COL_BOX); r.hasCollider = true; r.friction = 0.1f; add(r);
        Object c = thing(p, CYLINDER, "Can", 0.5f, 0.45f, 0.4f); c.scl[0] = c.scl[2] = 0.1f; c.scl[1] = 0.08f;
        c.hasBody = true; fitCollider(c, COL_MESH); c.hasCollider = true; add(c);
    }

    std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(out), ec);
    { std::ofstream f(std::filesystem::u8path(out) / "scene.json", std::ios::binary); f << toJson(p); }

    Physics::World* w = Physics::create(p);
    const int frames = (int)std::lround(seconds * 60);
    for (int fr = 1; fr <= frames; ++fr) {
        const float t = fr / 60.0f;
        Physics::step(w, p, 1.0f / 60, t);
        for (const auto& c : Physics::takeContacts(w))
            printf("{\"t\": %.4f, \"contact\": [%d, %d], \"enter\": %s, \"trigger\": %s}\n", t, c.a, c.b, c.enter ? "true" : "false", c.trigger ? "true" : "false");
        if (fr % 30 == 0) {
            printf("{\"t\": %.4f, \"objects\": {", t);
            bool first = true;
            for (const auto& o : p.markers[0].objects) {
                printf("%s\"%s\": [%.4f, %.4f, %.4f, %.2f, %.2f, %.2f]", first ? "" : ", ", o.name.c_str(), o.pos[0], o.pos[1], o.pos[2], o.rot[0], o.rot[1], o.rot[2]);
                first = false;
            }
            printf("}}\n");
        }
    }
    Physics::Hit h;
    const float from[3] = {0.3f, 2, 0}, down[3] = {0, -1, 0};
    if (Physics::raycast(w, 0, from, down, 10, h)) printf("{\"raycast\": {\"id\": %d, \"floor\": %s, \"y\": %.4f, \"distance\": %.4f}}\n", h.id, h.floor ? "true" : "false", h.point[1], h.distance);
    else printf("{\"raycast\": null}\n");
    printf("{\"bodies\": %d}\n", Physics::bodies(w));
    Physics::destroy(w);
    return 0;
}
