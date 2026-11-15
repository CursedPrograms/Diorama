// Diorama - the project, laid out like Unity: AR markers at the top of the hierarchy, objects under them (and under
// each other), each with a Transform (position, rotation in degrees, scale; Y up) and what it is: an empty, a plane,
// cube, sphere, capsule, cylinder or quad, a picture, a 3D model, a flock of sprites, light rays or words.
// On a marker: it lies in the XZ plane, Y comes up out of the paper, -Z is towards the marker's top; it's 1 across.
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
// what a game tracks (one per game: Project Settings); the anchors at the top of the Hierarchy are of that kind
enum Tracking { TRACK_MARKERS, TRACK_IMAGES, TRACK_FACES, TRACK_SURFACES, TRACK_HANDS, TRACKING_COUNT };
extern const char* TRACKING_NAMES[TRACKING_COUNT];
// a face anchor's point: MindAR's face landmarks (of 468) people want things on
struct FacePoint { const char* name; int landmark; float head[3]; };   // head: where the head's middle is from it (face widths)
extern const FacePoint FACE_POINTS[]; extern const int FACE_POINT_COUNT;

struct Script { std::string file; bool on = true; };   // a JavaScript behaviour (assets/...js), muted when off

// an object is an empty with a Transform; the rest are components, each can be switched off or removed:
// its look (kind: a mesh, a picture, a model, sprites, light rays or words, with its material), Animate, Scripts
struct Object {
    int id = 0;                        // unique in the project; parents are named by it
    int parent = -1;                   // -1: right under the marker
    std::string name = "GameObject";
    int kind = EMPTY;
    bool active = true;
    bool lookOn = true;                // the look component's tick (kind EMPTY: none)
    bool animOn = true, hasAnim = false;   // the Animate component: there, and its tick
    std::vector<Script> scripts;
    std::string prefab;                // the prefab it was placed from ("" none)
    bool occluder = false;             // material: invisible, but hides what's behind it (a real thing's stand-in)
    // Light
    bool hasLight = false, lightOn = true;
    int lightType = 0;                 // 0 point, 1 spot, 2 directional (along the object's -Z... its forward)
    float lightColour[3] = {1, 0.95f, 0.85f};
    float lightIntensity = 1.5f, lightRange = 1.5f, spotAngle = 40;
    // Audio Source
    bool hasAudio = false, audioOn = true;
    std::string audioFile;
    float volume = 1;
    bool audioLoop = false, playOnStart = true, spatial = true;   // spatial: quieter with distance from the phone
    float audioNear = 0.5f, audioFar = 6;
    // Particle System (the same maths in the editor and the game: a particle's life is a function of time)
    bool hasParticles = false, particlesOn = true;
    std::string particleFile;          // its picture ("" a soft dot)
    float pRate = 20, pLife = 1.5f, pSpeed = 0.4f, pSpread = 25, pSizeStart = 0.06f, pSizeEnd = 0.01f, pGravity = 0;
    float pColourStart[4] = {1, 0.9f, 0.6f, 1}, pColourEnd[4] = {1, 0.4f, 0.1f, 0};
    bool pGlow = true;
    // Line Renderer: points in the object's space
    bool hasLine = false, lineOn = true, lineLoop = false;
    std::vector<float> linePoints = {0, 0, 0, 0, 0.5f, 0};
    float lineWidth = 0.02f;
    float lineColour[4] = {1, 1, 1, 1};
    // Video Player: plays on the object's own surface (its look's material)
    bool hasVideo = false, videoOn = true;
    std::string videoFile;
    bool videoLoop = true, videoPlayOnStart = true, videoMuted = false;
    float videoVolume = 1, videoSpeed = 1;
    // Canvas (the game's UI, laid over the camera as HTML) and its UI elements (children of a Canvas)
    bool hasCanvas = false, canvasWithAnchor = false;   // withAnchor: shown only while its anchor is found
    std::string canvasCss;             // the game's own style sheet for its UI ("" none)
    int ui = 0;                        // UI_NONE, UI_TEXT, ...
    bool uiOn = true;
    int uiAnchor = 4;                  // 0 top left .. 8 bottom right (3 x 3), on its parent's rectangle
    float uiRect[4] = {0, 0, 160, 48}; // x, y (from the anchor, y down), width, height: in points on a 400 x 711 phone
    std::string uiText = "Text";
    float uiFont = 20;
    int uiAlign = 1;                   // 0 left, 1 centre, 2 right
    float uiColour[4] = {1, 1, 1, 1};  // text
    float uiBg[4] = {0, 0.482f, 0.718f, 1};   // background (buttons, panels, slider fill, ticks)
    float uiMin = 0, uiMax = 1, uiValue = 0.5f;
    bool uiChecked = false;
    std::string uiFile;                // Image's picture
    std::string uiClass;               // extra CSS classes
    // AR Path: a route on the floor (drawn here, or walked and saved in the game), marked with arrows (path marking)
    bool hasPath = false, pathOn = true, pathLoop = false, pathLine = true;
    std::string pathName = "Path 1";
    std::vector<float> pathPoints = {0, 0, 0, 0, 0, -1, 1, 0, -2};   // in the object's space, metres
    float arrowSpacing = 0.5f, arrowSize = 0.25f;
    float pathColour[4] = {0.15f, 0.2f, 0.75f, 0.95f};
    // AR Path Follower: walks a path (path following: a guide the player follows)
    bool hasFollower = false, followerOn = true, followLoop = true, followOnStart = true;
    std::string followPath = "Path 1";
    float followSpeed = 0.6f;          // metres a second
    // Trail Renderer: where it's been
    bool hasTrail = false, trailOn = true;
    float trailTime = 0.5f, trailWidth = 0.03f;
    float trailColour[4] = {0.2f, 0.66f, 0.89f, 1};
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
    std::string target;                // images: the picture to look for
    int facePoint = 1;                 // faces: the landmark it's on (FACE_POINTS)
    bool expanded = true;
    // the light
    float ambient[3] = {0.55f, 0.55f, 0.6f};
    float sun[3] = {1, 0.96f, 0.9f};
    float sunStrength = 0.9f, sunYaw = 35, sunPitch = 55;
    // a window into the marker: what's under the paper only shows through this opening
    bool portal = false;
    float portalSize[2] = {0.9f, 0.9f};
    // Surfaces: AR Foundation's AR Plane Manager (and where this anchor's things go). In metres: 1 = 1 m.
    int planeDetect = 2;               // 0 horizontal (floors, tables), 1 vertical (walls), 2 both
    bool showPlanes = true;            // the dotted look on what's found
    float planeColour[4] = {0.25f, 0.95f, 0.55f, 0.35f};
    std::string planePrefab;           // made on each plane found ("" none)
    int placement = 0;                 // 0 tap to place, 1 on the first floor found, 2 where the phone starts
    std::vector<Object> objects;       // parents before their children
    std::string patt;                  // in a build only: its pattern file there
};

struct Project {
    std::string title = "My diorama";
    int tracking = TRACK_MARKERS;      // Project Settings > Tracking
    bool gps = false;                  // Project Settings > Location (GPS): the player's place for scripts and place anchors
    std::vector<Marker> markers;       // the open scene's anchors and objects
    int nextId = 1;                    // ids are unique in the whole project (all its scenes)
    // the Scene Manager: the game's scenes in build order (scene 0 is the game's index.html), and the open one;
    // each scene is kept in scenes/<name>.scene
    std::vector<std::string> scenes = {"Main"};
    std::string scene = "Main";
    // Build Settings: the platform it builds for, and its runtime
    std::string platform = "web";      // web, windows, linux, pi, android
    int windowsRuntime = 0;            // 0 JavaScript (WebView2), 1 C++ (Diorama's native player)
    // Player Settings
    std::string company = "Cursed Entertainment", version = "1.0";
    std::string packageId;             // Android; "" = com.cursedentertainment.<game>
    std::string icon, splash;          // pictures ("" Diorama's)
    std::string intro;                 // words on the splash screen
    bool skipSplash = false, fullscreen = false;
    int orientation = 0;               // phones: 0 turns with the phone, 1 portrait, 2 landscape
};

int newId(Project& p);
enum { UI_NONE, UI_TEXT, UI_BUTTON, UI_SLIDER, UI_TOGGLE, UI_IMAGE, UI_PANEL, UI_COUNT };
extern const char* UI_NAMES[UI_COUNT];
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
Project navigationTemplate();   // Surfaces: indoor navigation (plane detection, path marking, path following)

}
