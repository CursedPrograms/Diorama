// Diorama - 3D models (see model.h), read with cgltf (MIT)
#include "model.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace Model {

// ---- 4x4 matrices, column-major (as glTF)
static void ident(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void mul(const float* a, const float* b, float* o) {
    float r[16];
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) { float s = 0; for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
    memcpy(o, r, 64);
}
static void trs(const float* t, const float* q, const float* s, float* m) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0]; m[2] = (2 * (x * z - y * w)) * s[0]; m[3] = 0;
    m[4] = (2 * (x * y - z * w)) * s[1]; m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1]; m[7] = 0;
    m[8] = (2 * (x * z + y * w)) * s[2]; m[9] = (2 * (y * z - x * w)) * s[2]; m[10] = (1 - 2 * (x * x + y * y)) * s[2]; m[11] = 0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
}

static int texFor(const cgltf_texture_view& tv, cgltf_data* d, std::map<const cgltf_image*, int>& made, Model& m, const fs::path& dir) {
    if (!tv.texture || !tv.texture->image) return -1;
    const cgltf_image* im = tv.texture->image;
    auto it = made.find(im);
    if (it != made.end()) return it->second;
    int w = 0, h = 0, n = 0;
    unsigned char* px = nullptr;
    if (im->buffer_view) {
        const unsigned char* src = (const unsigned char*)im->buffer_view->buffer->data + im->buffer_view->offset;
        px = stbi_load_from_memory(src, (int)im->buffer_view->size, &w, &h, &n, 4);
    } else if (im->uri && strncmp(im->uri, "data:", 5) != 0) {
        std::string uri = im->uri;
        cgltf_decode_uri(&uri[0]); uri.resize(strlen(uri.c_str()));
        std::ifstream f(dir / fs::u8path(uri), std::ios::binary);
        std::stringstream ss; ss << f.rdbuf(); const std::string b = ss.str();
        if (!b.empty()) px = stbi_load_from_memory((const unsigned char*)b.data(), (int)b.size(), &w, &h, &n, 4);
    }
    (void)d;
    int idx = -1;
    if (px) { Texture t; t.w = w; t.h = h; t.rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px); m.textures.push_back(std::move(t)); idx = (int)m.textures.size() - 1; }
    made[im] = idx;
    return idx;
}

bool load(const std::string& path, Model& m) {
    m = Model();
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    const std::string bytes = ss.str();
    if (bytes.empty()) { m.error = "Couldn't read " + path; return false; }
    cgltf_options opt{};
    cgltf_data* d = nullptr;
    if (cgltf_parse(&opt, bytes.data(), bytes.size(), &d) != cgltf_result_success) { m.error = "Not a glTF / GLB file"; return false; }
    const fs::path dir = fs::u8path(path).parent_path();
    if (cgltf_load_buffers(&opt, d, dir.u8string().empty() ? "./" : (dir.u8string() + "/").c_str()) != cgltf_result_success) { cgltf_free(d); m.error = "Its buffers didn't load"; return false; }

    // nodes
    std::map<const cgltf_node*, int> nodeIdx;
    for (cgltf_size i = 0; i < d->nodes_count; ++i) nodeIdx[&d->nodes[i]] = (int)i;
    m.nodes.resize(d->nodes_count);
    for (cgltf_size i = 0; i < d->nodes_count; ++i) {
        const cgltf_node& n = d->nodes[i];
        Node& o = m.nodes[i];
        o.parent = n.parent ? nodeIdx[n.parent] : -1;
        if (n.has_matrix) { o.hasMatrix = true; memcpy(o.matrix, n.matrix, 64); }
        if (n.has_translation) memcpy(o.t, n.translation, 12);
        if (n.has_rotation) memcpy(o.r, n.rotation, 16);
        if (n.has_scale) memcpy(o.s, n.scale, 12);
    }
    // skins
    std::map<const cgltf_skin*, int> skinIdx;
    for (cgltf_size i = 0; i < d->skins_count; ++i) {
        const cgltf_skin& s = d->skins[i];
        Skin k;
        for (cgltf_size j = 0; j < s.joints_count; ++j) k.joints.push_back(nodeIdx[s.joints[j]]);
        k.inverseBind.resize(s.joints_count * 16);
        for (cgltf_size j = 0; j < s.joints_count; ++j) {
            if (s.inverse_bind_matrices) cgltf_accessor_read_float(s.inverse_bind_matrices, j, &k.inverseBind[j * 16], 16);
            else ident(&k.inverseBind[j * 16]);
        }
        skinIdx[&s] = (int)m.skins.size();
        m.skins.push_back(std::move(k));
    }
    // meshes, as their nodes draw them
    std::map<const cgltf_image*, int> made;
    for (cgltf_size ni = 0; ni < d->nodes_count; ++ni) {
        const cgltf_node& n = d->nodes[ni];
        if (!n.mesh) continue;
        for (cgltf_size pi = 0; pi < n.mesh->primitives_count; ++pi) {
            const cgltf_primitive& pr = n.mesh->primitives[pi];
            if (pr.type != cgltf_primitive_type_triangles) continue;
            Prim p;
            p.node = (int)ni;
            p.skin = n.skin ? skinIdx[n.skin] : -1;
            size_t vc = 0;
            for (cgltf_size a = 0; a < pr.attributes_count; ++a) {
                const cgltf_attribute& at = pr.attributes[a];
                const cgltf_accessor* acc = at.data;
                vc = acc->count;
                if (at.type == cgltf_attribute_type_position) { p.pos.resize(vc * 3); for (size_t v = 0; v < vc; ++v) cgltf_accessor_read_float(acc, v, &p.pos[v * 3], 3); }
                else if (at.type == cgltf_attribute_type_normal) { p.nrm.resize(vc * 3); for (size_t v = 0; v < vc; ++v) cgltf_accessor_read_float(acc, v, &p.nrm[v * 3], 3); }
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) { p.uv.resize(vc * 2); for (size_t v = 0; v < vc; ++v) cgltf_accessor_read_float(acc, v, &p.uv[v * 2], 2); }
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) {
                    p.joints.resize(vc * 4);
                    for (size_t v = 0; v < vc; ++v) { cgltf_uint j[4] = {0, 0, 0, 0}; cgltf_accessor_read_uint(acc, v, j, 4); for (int k = 0; k < 4; ++k) p.joints[v * 4 + k] = (unsigned short)j[k]; }
                } else if (at.type == cgltf_attribute_type_weights && at.index == 0) { p.weights.resize(vc * 4); for (size_t v = 0; v < vc; ++v) cgltf_accessor_read_float(acc, v, &p.weights[v * 4], 4); }
            }
            if (p.pos.empty()) continue;
            vc = p.pos.size() / 3;
            if (pr.indices) { p.idx.resize(pr.indices->count); for (cgltf_size k = 0; k < pr.indices->count; ++k) p.idx[k] = (unsigned)cgltf_accessor_read_index(pr.indices, k); }
            else { p.idx.resize(vc); for (size_t k = 0; k < vc; ++k) p.idx[k] = (unsigned)k; }
            if (p.nrm.size() != vc * 3) {   // flat normals when it has none
                p.nrm.assign(vc * 3, 0);
                for (size_t k = 0; k + 2 < p.idx.size(); k += 3) {
                    const float* a = &p.pos[p.idx[k] * 3]; const float* b = &p.pos[p.idx[k + 1] * 3]; const float* c = &p.pos[p.idx[k + 2] * 3];
                    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    const float nn[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                    for (int e = 0; e < 3; ++e) for (int q = 0; q < 3; ++q) p.nrm[p.idx[k + e] * 3 + q] += nn[q];
                }
            }
            if (p.uv.size() != vc * 2) p.uv.assign(vc * 2, 0);
            if (p.joints.size() != vc * 4 || p.weights.size() != vc * 4) { p.joints.clear(); p.weights.clear(); p.skin = -1; }
            if (const cgltf_material* mt = pr.material) {
                if (mt->has_pbr_metallic_roughness) {
                    memcpy(p.colour, mt->pbr_metallic_roughness.base_color_factor, 16);
                    p.texture = texFor(mt->pbr_metallic_roughness.base_color_texture, d, made, m, dir);
                }
                p.blend = mt->alpha_mode == cgltf_alpha_mode_blend;
            }
            m.prims.push_back(std::move(p));
        }
    }
    // animation clips
    for (cgltf_size ai = 0; ai < d->animations_count; ++ai) {
        const cgltf_animation& an = d->animations[ai];
        Clip c;
        c.name = an.name ? an.name : ("Clip " + std::to_string(ai + 1));
        for (cgltf_size ci = 0; ci < an.channels_count; ++ci) {
            const cgltf_animation_channel& ch = an.channels[ci];
            if (!ch.target_node || !ch.sampler) continue;
            Channel o;
            o.node = nodeIdx[ch.target_node];
            if (ch.target_path == cgltf_animation_path_type_translation) o.path = 0;
            else if (ch.target_path == cgltf_animation_path_type_rotation) o.path = 1;
            else if (ch.target_path == cgltf_animation_path_type_scale) o.path = 2;
            else continue;
            o.interp = ch.sampler->interpolation == cgltf_interpolation_type_step ? 1 : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 2 : 0;
            const cgltf_accessor* in = ch.sampler->input; const cgltf_accessor* out = ch.sampler->output;
            o.times.resize(in->count);
            for (cgltf_size k = 0; k < in->count; ++k) cgltf_accessor_read_float(in, k, &o.times[k], 1);
            const int comps = o.path == 1 ? 4 : 3;
            o.values.resize(out->count * comps);
            for (cgltf_size k = 0; k < out->count; ++k) cgltf_accessor_read_float(out, k, &o.values[k * comps], comps);
            if (!o.times.empty()) c.duration = std::max(c.duration, o.times.back());
            c.channels.push_back(std::move(o));
        }
        m.clips.push_back(std::move(c));
    }
    cgltf_free(d);
    m.ok = !m.prims.empty();
    if (!m.ok) { m.error = "No meshes in it"; return false; }
    pose(m, -1, 0, true);
    // the rest pose's bounds
    bool first = true;
    for (const Prim& p : m.prims)
        for (size_t v = 0; v + 2 < p.outPos.size(); v += 3)
            for (int c = 0; c < 3; ++c) {
                if (first) { m.lo[c] = m.hi[c] = p.outPos[v + c]; }
                m.lo[c] = std::min(m.lo[c], p.outPos[v + c]); m.hi[c] = std::max(m.hi[c], p.outPos[v + c]);
                if (c == 2) first = false;
            }
    return true;
}

int findClip(const Model& m, const std::string& name) { for (size_t i = 0; i < m.clips.size(); ++i) if (m.clips[i].name == name) return (int)i; return -1; }

static void sample(const Channel& c, float t, float* out) {
    const int comps = c.path == 1 ? 4 : 3;
    const size_t n = c.times.size();
    auto val = [&](size_t k, int e) { return c.interp == 2 ? c.values[(k * 3 + 1) * comps + e] : c.values[k * comps + e]; };   // cubic: the middle of in/value/out
    if (t <= c.times[0] || n == 1) { for (int e = 0; e < comps; ++e) out[e] = val(0, e); return; }
    if (t >= c.times[n - 1]) { for (int e = 0; e < comps; ++e) out[e] = val(n - 1, e); return; }
    size_t k = 0;
    while (k + 1 < n && c.times[k + 1] < t) ++k;
    const float a = (t - c.times[k]) / std::max(1e-6f, c.times[k + 1] - c.times[k]);
    if (c.interp == 1) { for (int e = 0; e < comps; ++e) out[e] = val(k, e); return; }
    if (c.path == 1) {   // rotations: the shorter way round, normalised
        float d = 0; for (int e = 0; e < 4; ++e) d += val(k, e) * val(k + 1, e);
        const float sgn = d < 0 ? -1.0f : 1.0f;
        float len = 0;
        for (int e = 0; e < 4; ++e) { out[e] = val(k, e) * (1 - a) + sgn * val(k + 1, e) * a; len += out[e] * out[e]; }
        len = std::sqrt(len); if (len > 0) for (int e = 0; e < 4; ++e) out[e] /= len;
    } else for (int e = 0; e < comps; ++e) out[e] = val(k, e) * (1 - a) + val(k + 1, e) * a;
}

void pose(Model& m, int clip, float t, bool loop) {
    std::vector<Node> nodes = m.nodes;
    if (clip >= 0 && clip < (int)m.clips.size()) {
        const Clip& c = m.clips[clip];
        float tt = t;
        if (c.duration > 0) tt = loop ? std::fmod(std::max(0.0f, t), c.duration) : std::min(t, c.duration);
        for (const Channel& ch : c.channels) {
            Node& n = nodes[ch.node];
            n.hasMatrix = false;
            sample(ch, tt, ch.path == 0 ? n.t : ch.path == 1 ? n.r : n.s);
        }
    }
    // world matrices, parents first (glTF doesn't promise that order: walk up)
    std::vector<float> world(nodes.size() * 16);
    std::vector<char> done(nodes.size(), 0);
    std::function<void(int)> get = [&](int i) {
        if (done[i]) return;
        float local[16];
        if (nodes[i].hasMatrix) memcpy(local, nodes[i].matrix, 64); else trs(nodes[i].t, nodes[i].r, nodes[i].s, local);
        if (nodes[i].parent >= 0) { get(nodes[i].parent); mul(&world[nodes[i].parent * 16], local, &world[i * 16]); }
        else memcpy(&world[i * 16], local, 64);
        done[i] = 1;
    };
    for (size_t i = 0; i < nodes.size(); ++i) get((int)i);
    for (Prim& p : m.prims) {
        const size_t vc = p.pos.size() / 3;
        p.outPos.resize(vc * 3); p.outNrm.resize(vc * 3);
        if (p.skin >= 0) {
            const Skin& sk = m.skins[p.skin];
            std::vector<float> jm(sk.joints.size() * 16);
            for (size_t j = 0; j < sk.joints.size(); ++j) mul(&world[sk.joints[j] * 16], &sk.inverseBind[j * 16], &jm[j * 16]);
            for (size_t v = 0; v < vc; ++v) {
                float M[12] = {0};   // the blend of the joint matrices (rows of the upper 3x4)
                float ws = 0;
                for (int k = 0; k < 4; ++k) {
                    const float w = p.weights[v * 4 + k];
                    if (w <= 0) continue;
                    const unsigned j = p.joints[v * 4 + k];
                    if (j >= sk.joints.size()) continue;
                    const float* J = &jm[j * 16];
                    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) M[r * 4 + c] += J[c * 4 + r] * w;
                    for (int r = 0; r < 3; ++r) M[r * 4 + 3] += J[12 + r] * w;
                    ws += w;
                }
                const float* P = &p.pos[v * 3]; const float* N = &p.nrm[v * 3];
                if (ws <= 0) { memcpy(&p.outPos[v * 3], P, 12); memcpy(&p.outNrm[v * 3], N, 12); continue; }
                for (int r = 0; r < 3; ++r) {
                    p.outPos[v * 3 + r] = M[r * 4] * P[0] + M[r * 4 + 1] * P[1] + M[r * 4 + 2] * P[2] + M[r * 4 + 3];
                    p.outNrm[v * 3 + r] = M[r * 4] * N[0] + M[r * 4 + 1] * N[1] + M[r * 4 + 2] * N[2];
                }
            }
        } else {
            const float* W = &world[p.node * 16];
            for (size_t v = 0; v < vc; ++v) {
                const float* P = &p.pos[v * 3]; const float* N = &p.nrm[v * 3];
                for (int r = 0; r < 3; ++r) {
                    p.outPos[v * 3 + r] = W[r] * P[0] + W[4 + r] * P[1] + W[8 + r] * P[2] + W[12 + r];
                    p.outNrm[v * 3 + r] = W[r] * N[0] + W[4 + r] * N[1] + W[8 + r] * N[2];
                }
            }
        }
    }
}

}
