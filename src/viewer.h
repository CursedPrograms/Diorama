// Diorama - the Scene view: the marker printed on a desk, the selected marker's objects on it as the phone will
// show them (the window into the paper, animations, sprites flying, models playing their clips). Drawn into its own
// texture, shown inside the window. Drag turns round the marker, right-drag moves, the wheel zooms.
#pragma once
#include "scene.h"

#include <string>
#include <vector>

namespace Viewer {

bool init(const std::string& exeDir);
struct Look { bool desk = true, gizmo = true, animate = true; float time = 0; };
Look& look();
// draws marker m of the project (its printed marker from markerPicture: RGBA, top row first), selected: an object id
// (-1 none); returns the texture (draw it with uv (0,1)-(1,0)). Clicking an object picks it: picked gets its id.
unsigned render(const Scene::Project& p, int marker, int selected, int w, int h, bool hovered, int& picked);
void setMarkerPicture(const std::vector<unsigned char>& rgba, int w, int h);   // the printed marker (or picture) on the desk
// what the anchor is: 0 a printed marker, 1 a picture (any shape), 2 a face (a head is drawn; head: its middle from the
// anchor point, in face widths)
void setAnchorMode(int mode, const float head[3]);
void reload(const std::string& file);    // a picture or model changed on disk
void frameMarker();                      // look at the marker from the front, a little above
std::vector<std::string> clipsOf(const std::string& modelFile);
unsigned pictureTexture(const std::string& file, int* w, int* h);
// the Main Camera's view of the same scene (as the phone would see it), into its own texture (draw it with uv (0,1)-(1,0))
unsigned renderCameraPreview(const Scene::Project& p, int marker, int w, int h);   // a picture's GL texture (the UI overlay's images)   // a model's animation clips (for the Inspector)
void readPixels(std::vector<unsigned char>& rgba, int& w, int& h);
// the Profiler's numbers for the last view drawn: CPU and GPU time, what was drawn
struct Stats { float cpuMs = 0, gpuMs = -1; int objects = 0, draws = 0, culled = 0, lights = 0, shadowMaps = 0, textures = 0, models = 0, width = 0, height = 0; };
const Stats& stats();

// ---- the native AR player: the real camera's picture with each found anchor's world on it
struct ArAnchor { int marker; float view[16]; };   // which anchor (project.markers), and anchor -> camera (OpenGL's camera axes), column-major
// draws the anchors seen (each lit and shadowed in its own frame), the Main Camera's objects, then the camera picture
// (bg: a texture, top row first; bgRect: the part of it the window shows, x y w h in 0..1) under what was drawn
unsigned renderAR(const Scene::Project& p, const std::vector<ArAnchor>& anchors, const float proj[16], unsigned bg, const float bgRect[4], int w, int h, float t);
void present(int winW, int winH);                  // the last picture, onto the window
// the planes found (AR Foundation's plane visualiser): each its view (plane -> camera), colour, and size (metres)
struct ArPlane { float view[16]; float colour[4]; float size; std::vector<float> boundary; };   // boundary: x z pairs (empty: a size x size square)
void setArPlanes(const std::vector<ArPlane>& planes);
unsigned uploadBGR(unsigned tex, const unsigned char* bgr, int w, int h);   // a camera frame into a texture (0: a new one)
// for the editing handles: an object's world matrix and its parent's, as last drawn (column-major); a point in the
// world on the view (pixels from its top-left; false behind the camera); the camera's position; looking at a point
bool matrices(int id, float world[16], float parent[16]);
bool toScreen(const float p[3], float& x, float& y);
void eyePos(float out[3]);
void focusOn(const float p[3], float size);
void clickAt(int x, int y);   // as if the view were clicked there (for testing)

}
