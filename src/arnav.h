// Diorama - surfaces and paths (AR Foundation's plane detection, path marking and path following), the maths shared by
// the editor, the native player and the web player (assets/web/diorama.js has the same functions, line for line).
#pragma once
#include <utility>
#include <vector>

namespace ArNav {

struct V3 { float x = 0, y = 0, z = 0; };
struct Pose { V3 p; float yaw = 0; };   // yaw: radians round +Y; 0 faces -Z (forward), as Diorama's arrows do

std::vector<V3> fromFlat(const std::vector<float>& xyz);      // x, y, z, x, y, z, ... as points
float length(const std::vector<V3>& pts, bool loop = false);
// the point s along the path (and which way it goes there): a follower's walk, an arrow's place
Pose at(const std::vector<V3>& pts, float s, bool loop = false);
// arrows every `spacing` along it (path marking), the first half a spacing in
std::vector<Pose> arrows(const std::vector<V3>& pts, float spacing, bool loop = false);
// a walked path, tidied: a point only every `spacing` (recording keeps the phone's position as it moves)
std::vector<V3> resample(const std::vector<V3>& pts, float spacing);

// a detected plane, AR Foundation's way: its boundary in its own space (x, z; y up out of it), convex
using Polygon = std::vector<std::pair<float, float>>;
bool inside(const Polygon& poly, float x, float z);
// the dotted look (the plane visualizer): dots on a triangular lattice inside the boundary
std::vector<std::pair<float, float>> dots(const Polygon& poly, float spacing);
// the plane's fill as triangles (a fan: ARCore's boundaries are convex)
std::vector<unsigned> fan(const Polygon& poly);

}
