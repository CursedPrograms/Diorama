// Diorama - surfaces and paths: see arnav.h. Keep in step with ArNav in assets/web/diorama.js.
#include "arnav.h"

#include <algorithm>
#include <cmath>

namespace ArNav {

static float dist(const V3& a, const V3& b) { const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
static float yawOf(float dx, float dz) { return std::atan2(-dx, -dz); }   // so that -Z turned by yaw points along (dx, dz)

std::vector<V3> fromFlat(const std::vector<float>& xyz) {
    std::vector<V3> out;
    for (size_t i = 0; i + 2 < xyz.size(); i += 3) out.push_back({xyz[i], xyz[i + 1], xyz[i + 2]});
    return out;
}
float length(const std::vector<V3>& pts, bool loop) {
    float L = 0;
    for (size_t i = 1; i < pts.size(); ++i) L += dist(pts[i - 1], pts[i]);
    if (loop && pts.size() > 2) L += dist(pts.back(), pts.front());
    return L;
}
Pose at(const std::vector<V3>& pts, float s, bool loop) {
    Pose r;
    if (pts.empty()) return r;
    if (pts.size() == 1) { r.p = pts[0]; return r; }
    const size_t n = pts.size(), segs = loop && n > 2 ? n : n - 1;
    const float L = length(pts, loop);
    if (L <= 1e-6f) { r.p = pts[0]; return r; }
    if (loop) { s = std::fmod(s, L); if (s < 0) s += L; } else s = std::clamp(s, 0.0f, L);
    for (size_t i = 0; i < segs; ++i) {
        const V3& a = pts[i]; const V3& b = pts[(i + 1) % n];
        const float d = dist(a, b);
        if (s <= d || i + 1 == segs) {
            const float f = d > 1e-6f ? std::clamp(s / d, 0.0f, 1.0f) : 0;
            r.p = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
            r.yaw = yawOf(b.x - a.x, b.z - a.z);
            return r;
        }
        s -= d;
    }
    r.p = pts.back();
    return r;
}
std::vector<Pose> arrows(const std::vector<V3>& pts, float spacing, bool loop) {
    std::vector<Pose> out;
    const float L = length(pts, loop);
    if (spacing <= 0.01f || L <= 0) return out;
    for (float s = spacing * 0.5f; s < L && out.size() < 2000; s += spacing) out.push_back(at(pts, s, loop));
    return out;
}
std::vector<V3> resample(const std::vector<V3>& pts, float spacing) {
    std::vector<V3> out;
    if (pts.empty()) return out;
    out.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) if (dist(out.back(), pts[i]) >= spacing) out.push_back(pts[i]);
    if (pts.size() > 1 && dist(out.back(), pts.back()) > 1e-4f) out.push_back(pts.back());
    return out;
}

bool inside(const Polygon& poly, float x, float z) {   // even-odd crossing
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const float xi = poly[i].first, zi = poly[i].second, xj = poly[j].first, zj = poly[j].second;
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) in = !in;
    }
    return in;
}
std::vector<std::pair<float, float>> dots(const Polygon& poly, float spacing) {
    std::vector<std::pair<float, float>> out;
    if (poly.size() < 3 || spacing <= 0.005f) return out;
    float x0 = poly[0].first, x1 = x0, z0 = poly[0].second, z1 = z0;
    for (const auto& p : poly) { x0 = std::min(x0, p.first); x1 = std::max(x1, p.first); z0 = std::min(z0, p.second); z1 = std::max(z1, p.second); }
    const float rowH = spacing * 0.8660254f;   // a triangular lattice: rows half a step apart
    int row = 0;
    for (float z = std::floor(z0 / rowH) * rowH; z <= z1 && out.size() < 20000; z += rowH, ++row) {
        const float off = (row & 1) ? spacing * 0.5f : 0;
        for (float x = std::floor(x0 / spacing) * spacing + off; x <= x1; x += spacing) if (inside(poly, x, z)) out.push_back({x, z});
    }
    return out;
}
std::vector<unsigned> fan(const Polygon& poly) {
    std::vector<unsigned> idx;
    for (unsigned i = 1; i + 1 < poly.size(); ++i) idx.insert(idx.end(), {0u, i, i + 1});
    return idx;
}

}
