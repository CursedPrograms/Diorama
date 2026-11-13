// Diorama - the project, laid out like Unity: AR markers at the top of the hierarchy, objects under them (and under
// each other), each with a Transform (position, rotation in degrees, scale; Y up) and what it is: an empty, a plane,
// cube, sphere, capsule, cylinder or quad, a picture, a 3D model, a flock of sprites, light rays or words.
// On a marker: it lies in the XZ plane, Y comes up out of the paper, +Z is towards the marker's top; it's 1 across.
// Saved as <folder>/project.diorama (JSON); the files it uses go in <folder>/assets.
#pragma once
#include <string>
#include <vector>

namespace Scene {

enum Kind { EMPTY, PLANE, CUBE, SPHERE, CAPSULE, CYLINDER, QUAD, PICTURE, MODEL, SPRITES, RAYS, WORDS, KIND_COUNT };
extern const char* KIND_NAMES[KIND_COUNT];
bool isPrimitive(int kind);
enum Anim { A_STILL, A_FLOAT, A_SPIN, A_SWAY, A_PULSE, ANIM_COUNT };
extern const char* ANIM_NAMES[ANIM_COUNT];
enum MarkerType { MK_PATTERN, MK_BARCODE, MARKER_TYPE_COUNT };

struct Object {
    int id = 0;                        // unique in the project; parents are named by it
    int parent = -1;                   // -1: right under the marker
    std::string name = "GameObject";
    int kind = EMPTY;
    bool active = true;
    bool expanded = true;              // open in the hierarchy
    // Transform
    float pos[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};          // degrees, applied z, then x, then y (as Unity)
    float scl[3] = {1, 1, 1};
    // what it looks like
    std::string file;                  // a picture (Picture, Sprites, a texture on a primitive) or a model
    float colour[4] = {1, 1, 1, 1};    // the material's colour (and opacity)
    bool lit = true;                   // shaded by the lights (pictures: off, they keep their own colours)
    bool glow = false;                 // added on as light (god rays, sparks)
    bool billboard = false;            // always faces the phone
    // what it does by itself
    int anim = A_STILL;
    float animSpeed = 1, animAmount = 0.1f;
    // sprites: a flock of the picture flying round (butterflies, leaves, sparks) in a box of its scale
    int count = 12;
    float spriteSize = 0.15f;
    float flap = 1;
    // models: their own animation (a clip from the file, played on their bones)
    std::string clip;                  // "" none
    float clipSpeed = 1;
    bool clipLoop = true;
    // light rays
    int rays = 6;
    // words
    std::string text = "Hello";
};

struct Marker {
    int id = 0;
    std::string name = "AR Marker";
    int type = MK_PATTERN;
    std::string preset = "hiro";       // pattern: "hiro", "kanji", or "" for the picture / letter below
    std::string picture;               // pattern: its inside picture
    std::string letter;                // pattern: a letter or short word instead
    int barcode = 0;                   // barcode: 0..63
    bool expanded = true;
    // the light
    float ambient[3] = {0.55f, 0.55f, 0.6f};
    float sun[3] = {1, 0.96f, 0.9f};
    float sunStrength = 0.9f, sunYaw = 35, sunPitch = 55;
    // a window into the marker: what's under the paper only shows through this opening
    bool portal = false;
    float portalSize[2] = {0.9f, 0.9f};
    std::vector<Object> objects;       // parents before their children
};

struct Project {
    std::string title = "My diorama";
    std::vector<Marker> markers;
    int nextId = 1;
};

int newId(Project& p);
Object make(Project& p, int kind, int parent);   // a new object of a kind, with Unity's default size for it
std::string toJson(const Project& p);
bool fromJson(const std::string& text, Project& p, std::string& err);
bool save(const Project& p, const std::string& folder, std::string& err);
bool load(Project& p, const std::string& folder, std::string& err);

// objects: their children, and moving one under another (it keeps its place in the list order after its new parent)
std::vector<int> childrenOf(const Marker& m, int parent);
int indexOfId(const Marker& m, int id);
bool isUnder(const Marker& m, int id, int ancestor);   // id is ancestor or somewhere below it
void reparent(Marker& m, int id, int newParent);
void remove(Marker& m, int id);                        // with its children
int duplicate(Project& p, Marker& m, int id);          // with its children; returns the copy's id

// the art gallery: a frame on the Kanji marker, a forest under it, a person in it, butterflies, light rays
Project galleryTemplate(const std::string& assetsDir);
Project basicTemplate();

}
