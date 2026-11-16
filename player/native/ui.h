// Diorama's native player: the game's Canvas UI (Text, Button, Slider, Toggle, Image, Panel), drawn with Dear ImGui
// over the camera, laid out as the web game does (a 400 x 711 phone, scaled: anchor + offset, its size).
#pragma once
#include "scene.h"

#include <vector>

namespace Ui {

// seen[i]: anchor i is found (a Canvas "with its anchor" shows only then). Presses go to the scripts (Scripts::ui).
void draw(Scene::Project& p, const std::vector<bool>& seen);
bool wantsMouse();   // the pointer is on a UI element: not a tap on the 3D

}
