// Diorama - 3D models (glTF / GLB): meshes, materials and textures, the node tree, skins (bones) and animation clips.
// pose() plays a clip at a time and skins the meshes on the CPU; the viewer draws what comes out.
#pragma once
#include <string>
#include <vector>

namespace Model {

struct Texture { int w = 0, h = 0; std::vector<unsigned char> rgba; };

struct Prim {
    std::vector<float> pos, nrm, uv;           // as loaded (rest pose), in the mesh's own space
    std::vector<unsigned short> joints;        // 4 a vertex, into the skin's joints
    std::vector<float> weights;                // 4 a vertex
    std::vector<unsigned> idx;
    float colour[4] = {1, 1, 1, 1};
    int texture = -1;                          // into Model::textures
    bool blend = false;                        // alpha blended
    int node = -1;                             // the node that draws it
    int skin = -1;
    // posed, in the model's space (what's drawn)
    std::vector<float> outPos, outNrm;
};

struct Node {
    int parent = -1;
    float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};   // rest
    bool hasMatrix = false; float matrix[16];
};

struct Skin { std::vector<int> joints; std::vector<float> inverseBind; };   // 16 floats a joint

struct Channel { int node = -1, path = 0, interp = 0; std::vector<float> times, values; };   // path 0 t, 1 r, 2 s; interp 0 linear, 1 step, 2 cubic
struct Clip { std::string name; float duration = 0; std::vector<Channel> channels; };

struct Model {
    std::vector<Prim> prims;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Clip> clips;
    std::vector<Texture> textures;
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};   // the rest pose's bounds (model space)
    bool ok = false;
    std::string error;
};

bool load(const std::string& path, Model& m);
// play clip (-1: the rest pose) at t seconds (looped or held at the end); fills each prim's outPos / outNrm
void pose(Model& m, int clip, float t, bool loop);
int findClip(const Model& m, const std::string& name);

}
