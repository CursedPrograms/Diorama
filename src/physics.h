// Diorama - physics: Jolt Physics (MIT) under the Rigidbody and Collider components. The editor's Simulate and the
// native player use this; the web game runs the same engine as WASM (assets/web/diorama.js: Physics), the same way.
//
// Each anchor is its own space (things under one marker only meet each other), gravity along the project's
// Physics > Gravity in that space, with the anchor's floor (y = 0: the paper, table or floor) when it has one.
// Every step the bodies are matched to the objects (spawned, destroyed, switched off, changed), the world steps at a
// fixed 60 Hz, and where the moving bodies got to is written back into their objects' Transforms: everything else
// (the renderer, scripts, trails) sees it as if a script had moved them.
#pragma once
#include "scene.h"

#include <string>
#include <vector>

namespace Physics {

struct World;

// a contact that started or ended this step (each pair once): what the scripts' onCollisionEnter / Exit and
// onTriggerEnter / Exit hear. other -1: the anchor's floor. point / normal in the anchor's space; normal points from
// b towards a (the way a was pushed); speed: how fast they met
struct Contact { int a = 0, b = 0; bool enter = true, trigger = false; float point[3] = {0, 0, 0}, normal[3] = {0, 1, 0}, speed = 0; };
struct Hit { int id = -1; bool floor = false; float point[3] = {0, 0, 0}, normal[3] = {0, 1, 0}, distance = 0; };

World* create(const Scene::Project& p);
void destroy(World* w);
bool any(const Scene::Project& p);   // does anything in it have a Collider?
// after the scripts: bodies matched to the objects, the world stepped (dt in seconds; t the game's time for Animate),
// moving bodies written back into p
void step(World* w, Scene::Project& p, float dt, float t);
std::vector<Contact> takeContacts(World* w);

// for scripts (by object id); false: it has no body
bool velocity(World* w, int id, bool angular, float v[3]);
bool setVelocity(World* w, int id, bool angular, const float v[3]);
// mode: 0 force, 1 impulse, 2 acceleration, 3 velocity change (Unity's ForceMode); torque: turning, not pushing
bool addForce(World* w, int id, const float f[3], int mode, bool torque);
bool sleeping(World* w, int id);
bool wake(World* w, int id, bool on);   // on: wake up, off: sleep
// a ray in anchor's space (from, direction, how far); hits on triggers are left out
bool raycast(World* w, int anchor, const float from[3], const float dir[3], float maxDistance, Hit& hit);
int bodies(World* w);                   // how many (the Profiler)

}
