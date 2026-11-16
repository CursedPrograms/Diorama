// Diorama's native player: the game's scripts in QuickJS (quickjs-ng), with runtime.js (the web game's script runtime,
// the same hooks and API) on __dio: the C++ scene's objects to read and change. See runtime.js.
#pragma once
#include "scene.h"

#include <functional>
#include <string>

namespace Scripts {

struct Env {
    Scene::Project* proj = nullptr;
    // an anchor's view (anchor -> camera, OpenGL's axes) while it's seen; false: not seen
    std::function<bool(int anchor, float view[16])> anchorView;
    const float* projection = nullptr;          // the camera's projection (column-major)
    int sceneIndex = 0;
    std::function<void(int)> loadScene;         // asked for another scene (done after this frame)
    std::function<void(const std::string& level, const std::string& text)> log;
    std::function<bool(float view[16])> surface;   // Surfaces: the locked surface's view (false: none)
    struct Plane { std::string id; bool vertical; float view[16]; };
    std::function<std::vector<Plane>()> planes;     // Surfaces: every plane locked and seen now
};

bool start(const Env& env, std::string& err);   // the runtime, then every active object's scripts
void frame(double dt, double t, bool tapped, float x, float y);
void seen(int anchor, bool on);                 // anchor -1: the Main Camera
void tap(int objectId);
void ui(int objectId, const char* what, double value, bool isBool);   // a Toggle's value is true / false
void planesChanged(const std::string& eventJson);
void stop();

}
