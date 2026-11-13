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
void setMarkerPicture(const std::vector<unsigned char>& rgba, int w, int h);   // the printed marker on the desk
void reload(const std::string& file);    // a picture or model changed on disk
void frameMarker();                      // look at the marker from the front, a little above
std::vector<std::string> clipsOf(const std::string& modelFile);   // a model's animation clips (for the Inspector)
void readPixels(std::vector<unsigned char>& rgba, int& w, int& h);

}
