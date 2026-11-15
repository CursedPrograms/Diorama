// Diorama - the Scene view (see viewer.h). OpenGL 3.3 core, its functions fetched through SDL.
#include "viewer.h"
#include "model.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

#ifndef APIENTRY
#define APIENTRY
#endif
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name name##_ = nullptr;
GLF(GLuint, glCreateShader, (GLenum))
GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLF(void, glCompileShader, (GLuint))
GLF(GLuint, glCreateProgram, (void))
GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glLinkProgram, (GLuint))
GLF(void, glUseProgram, (GLuint))
GLF(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLF(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))
GLF(void, glUniform1i, (GLint, GLint))
GLF(void, glUniform1f, (GLint, GLfloat))
GLF(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))
GLF(void, glGenVertexArrays, (GLsizei, GLuint*))
GLF(void, glBindVertexArray, (GLuint))
GLF(void, glGenBuffers, (GLsizei, GLuint*))
GLF(void, glBindBuffer, (GLenum, GLuint))
GLF(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
GLF(void, glEnableVertexAttribArray, (GLuint))
GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLF(void, glGenFramebuffers, (GLsizei, GLuint*))
GLF(void, glBindFramebuffer, (GLenum, GLuint))
GLF(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLF(void, glGenRenderbuffers, (GLsizei, GLuint*))
GLF(void, glBindRenderbuffer, (GLenum, GLuint))
GLF(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))
GLF(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))
GLF(void, glGenerateMipmap, (GLenum))
GLF(void, glActiveTexture, (GLenum))
#undef GLF
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_TEXTURE0 0x84C0
#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

namespace Viewer {

using namespace Scene;

static Look lk;
Look& look() { return lk; }
static std::string exeDir;

// ---------- small maths: 4x4, column-major ----------
struct M4 { float m[16]; };
static M4 ident() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) { float s = 0; for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k]; r.m[c * 4 + rr] = s; }
    return r;
}
static M4 translate(float x, float y, float z) { M4 r = ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static M4 scale(float x, float y, float z) { M4 r = ident(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
static M4 rotX(float a) { M4 r = ident(); r.m[5] = std::cos(a); r.m[6] = std::sin(a); r.m[9] = -std::sin(a); r.m[10] = std::cos(a); return r; }
static M4 rotY(float a) { M4 r = ident(); r.m[0] = std::cos(a); r.m[2] = -std::sin(a); r.m[8] = std::sin(a); r.m[10] = std::cos(a); return r; }
static M4 rotZ(float a) { M4 r = ident(); r.m[0] = std::cos(a); r.m[1] = std::sin(a); r.m[4] = -std::sin(a); r.m[5] = std::cos(a); return r; }
static const float D2R = 3.14159265f / 180.0f;
// Unity's order: z, then x, then y
static M4 euler(const float* deg) { return mul(rotY(deg[1] * D2R), mul(rotX(deg[0] * D2R), rotZ(deg[2] * D2R))); }
static M4 perspective(float fovy, float aspect, float n, float f) {
    M4 r{}; const float t = 1.0f / std::tan(fovy / 2);
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f); return r;
}
static M4 lookAt(const float* eye, const float* at) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); for (auto& v : f) v /= l;
    float s[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0};   // f x up(0,1,0)
    l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); for (auto& v : s) v /= l;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    M4 r = ident();
    r.m[0] = s[0]; r.m[4] = s[1]; r.m[8] = s[2];
    r.m[1] = u[0]; r.m[5] = u[1]; r.m[9] = u[2];
    r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2];
    r.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r.m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    return r;
}
static void apply(const M4& m, const float* p, float* o) { for (int r = 0; r < 3; ++r) o[r] = m.m[r] * p[0] + m.m[4 + r] * p[1] + m.m[8 + r] * p[2] + m.m[12 + r]; }

// ---------- GL ----------
static bool loadGL() {
#define L(name) name##_ = (PFN_##name)SDL_GL_GetProcAddress(#name); if (!name##_) return false;
    L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glCreateProgram) L(glAttachShader) L(glLinkProgram) L(glUseProgram)
    L(glGetUniformLocation) L(glUniformMatrix4fv) L(glUniform1i) L(glUniform1f) L(glUniform3f) L(glUniform4f) L(glUniform4fv) L(glGenVertexArrays)
    L(glBindVertexArray) L(glGenBuffers) L(glBindBuffer) L(glBufferData) L(glEnableVertexAttribArray) L(glVertexAttribPointer)
    L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glGenRenderbuffers) L(glBindRenderbuffer)
    L(glRenderbufferStorage) L(glFramebufferRenderbuffer) L(glGenerateMipmap) L(glActiveTexture)
#undef L
    return true;
}
static GLuint program(const char* vs, const char* fs) {
    auto sh = [](GLenum t, const char* src) { GLuint s = glCreateShader_(t); glShaderSource_(s, 1, &src, nullptr); glCompileShader_(s); return s; };
    GLuint p = glCreateProgram_();
    glAttachShader_(p, sh(GL_VERTEX_SHADER, vs)); glAttachShader_(p, sh(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    return p;
}

static GLuint prog = 0, progLine = 0;
static GLint uClip, uPick, uPickCol, uNL, uLPos, uLCol, uLDir; static GLint uMVP, uModel, uColour, uUseTex, uLit, uSunDir, uSun, uAmbient, uTex, uLineMVP, uLineColour;

struct Mesh { GLuint vao = 0, vbo = 0, ibo = 0; int count = 0; };   // interleaved pos 3, normal 3, uv 2
static Mesh upload(const std::vector<float>& v, const std::vector<unsigned>& idx, bool dynamic = false) {
    Mesh m;
    glGenVertexArrays_(1, &m.vao); glBindVertexArray_(m.vao);
    glGenBuffers_(1, &m.vbo); glBindBuffer_(GL_ARRAY_BUFFER, m.vbo);
    glBufferData_(GL_ARRAY_BUFFER, v.size() * 4, v.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glGenBuffers_(1, &m.ibo); glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData_(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
    for (int a = 0; a < 3; ++a) glEnableVertexAttribArray_(a);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glVertexAttribPointer_(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    m.count = (int)idx.size();
    return m;
}
static void draw(const Mesh& m) { glBindVertexArray_(m.vao); glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr); }

// ---------- the primitives, Unity's sizes ----------
static Mesh mPlane, mCube, mSphere, mCapsule, mCylinder, mQuad, mPicture, mRay, mPortalBox;
static void addV(std::vector<float>& v, float x, float y, float z, float nx, float ny, float nz, float u, float w) { v.insert(v.end(), {x, y, z, nx, ny, nz, u, w}); }
static Mesh makeQuadXZ(float half) {   // flat, facing up (a plane, a picture)
    std::vector<float> v; std::vector<unsigned> i = {0, 1, 2, 0, 2, 3};
    addV(v, -half, 0, half, 0, 1, 0, 0, 1); addV(v, half, 0, half, 0, 1, 0, 1, 1); addV(v, half, 0, -half, 0, 1, 0, 1, 0); addV(v, -half, 0, -half, 0, 1, 0, 0, 0);
    return upload(v, i);
}
static Mesh makeQuadXY() {   // standing, facing -Z (Unity's quad)
    std::vector<float> v; std::vector<unsigned> i = {0, 1, 2, 0, 2, 3};
    addV(v, -0.5f, -0.5f, 0, 0, 0, -1, 0, 1); addV(v, 0.5f, -0.5f, 0, 0, 0, -1, 1, 1); addV(v, 0.5f, 0.5f, 0, 0, 0, -1, 1, 0); addV(v, -0.5f, 0.5f, 0, 0, 0, -1, 0, 0);
    return upload(v, i);
}
static Mesh makeCube() {
    std::vector<float> v; std::vector<unsigned> idx;
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        float a[3], b[3];   // two axes across the face
        const int ax = std::fabs(n[f][0]) > 0 ? 0 : std::fabs(n[f][1]) > 0 ? 1 : 2;
        const int u = (ax + 1) % 3, w = (ax + 2) % 3;
        for (int k = 0; k < 3; ++k) { a[k] = k == u ? 0.5f : 0; b[k] = k == w ? 0.5f : 0; }
        const unsigned base = (unsigned)(v.size() / 8);
        const float c[3] = {n[f][0] * 0.5f, n[f][1] * 0.5f, n[f][2] * 0.5f};
        const float sg[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (auto& s : sg) addV(v, c[0] + a[0] * s[0] + b[0] * s[1], c[1] + a[1] * s[0] + b[1] * s[1], c[2] + a[2] * s[0] + b[2] * s[1], n[f][0], n[f][1], n[f][2], (s[0] + 1) / 2, (s[1] + 1) / 2);
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return upload(v, idx);
}
// a lathe round Y: profile of (radius, y) points
static Mesh makeLathe(const std::vector<std::pair<float, float>>& prof, int seg = 32) {
    std::vector<float> v; std::vector<unsigned> idx;
    for (size_t i = 0; i < prof.size(); ++i)
        for (int s = 0; s <= seg; ++s) {
            const float a = s * 6.2831853f / seg, r = prof[i].first, y = prof[i].second;
            // the normal from the profile's slope
            const size_t i0 = i ? i - 1 : i, i1 = i + 1 < prof.size() ? i + 1 : i;
            float dr = prof[i1].first - prof[i0].first, dy = prof[i1].second - prof[i0].second;
            float nx = dy, ny = -dr; const float l = std::sqrt(nx * nx + ny * ny); if (l > 0) { nx /= l; ny /= l; }
            addV(v, r * std::cos(a), y, r * std::sin(a), nx * std::cos(a), ny, nx * std::sin(a), (float)s / seg, (float)i / (prof.size() - 1));
        }
    for (size_t i = 0; i + 1 < prof.size(); ++i)
        for (int s = 0; s < seg; ++s) {
            const unsigned a = (unsigned)(i * (seg + 1) + s), b = a + seg + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    return upload(v, idx);
}
static void makePrimitives() {
    mPlane = makeQuadXZ(5); mQuad = makeQuadXY(); mPicture = makeQuadXZ(0.5f); mCube = makeCube();
    std::vector<std::pair<float, float>> sph, cap, cyl;
    for (int i = 0; i <= 16; ++i) { const float a = -1.5707963f + i * 3.14159265f / 16; sph.push_back({0.5f * std::cos(a), 0.5f * std::sin(a)}); }
    for (int i = 0; i <= 8; ++i) { const float a = -1.5707963f + i * 1.5707963f / 8; cap.push_back({0.5f * std::cos(a), -0.5f + 0.5f * std::sin(a)}); }
    for (int i = 0; i <= 8; ++i) { const float a = i * 1.5707963f / 8; cap.push_back({0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}); }
    cyl = {{0, -1}, {0.5f, -1}, {0.5f, -1}, {0.5f, 1}, {0.5f, 1}, {0, 1}};
    mSphere = makeLathe(sph); mCapsule = makeLathe(cap); mCylinder = makeLathe(cyl);
    // a light ray: a long thin quad hanging down from the origin, widening
    { std::vector<float> v; std::vector<unsigned> i = {0, 1, 2, 0, 2, 3};
      addV(v, -0.02f, 0, 0, 0, 0, 1, 0, 0); addV(v, 0.02f, 0, 0, 0, 0, 1, 1, 0); addV(v, 0.12f, -1, 0, 0, 0, 1, 1, 1); addV(v, -0.12f, -1, 0, 0, 0, 1, 0, 1); mRay = upload(v, i); }
    // the inside of the window into the paper: an open-topped box, seen from inside
    { std::vector<float> v; std::vector<unsigned> idx;
      const float d = 1;
      auto face = [&](float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz, float nx, float ny, float nz) {
          const unsigned b = (unsigned)(v.size() / 8);
          addV(v, ax, ay, az, nx, ny, nz, 0, 0); addV(v, bx, by, bz, nx, ny, nz, 1, 0); addV(v, cx, cy, cz, nx, ny, nz, 1, 1); addV(v, dx, dy, dz, nx, ny, nz, 0, 1);
          idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3}); };
      face(-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, -d, -0.5f, -0.5f, -d, -0.5f, 0, 0, 1);
      face(-0.5f, 0, 0.5f, -0.5f, -d, 0.5f, 0.5f, -d, 0.5f, 0.5f, 0, 0.5f, 0, 0, -1);
      face(-0.5f, 0, -0.5f, -0.5f, -d, -0.5f, -0.5f, -d, 0.5f, -0.5f, 0, 0.5f, 1, 0, 0);
      face(0.5f, 0, -0.5f, 0.5f, 0, 0.5f, 0.5f, -d, 0.5f, 0.5f, -d, -0.5f, -1, 0, 0);
      face(-0.5f, -d, -0.5f, 0.5f, -d, -0.5f, 0.5f, -d, 0.5f, -0.5f, -d, 0.5f, 0, 1, 0);
      mPortalBox = upload(v, idx); }
}

// ---------- textures and models, by file ----------
struct Tex { GLuint id = 0; int w = 1, h = 1; };
static std::map<std::string, Tex> textures;
static GLuint texUpload(const unsigned char* rgba, int w, int h, bool mips = true) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (mips) glGenerateMipmap_(GL_TEXTURE_2D);
    return t;
}
static const Tex& texture(const std::string& file) {
    auto it = textures.find(file);
    if (it != textures.end()) return it->second;
    Tex t;
    std::ifstream f(fs::u8path(file), std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); const std::string b = ss.str();
    int w, h, n;
    stbi_set_flip_vertically_on_load(0);
    if (unsigned char* px = b.empty() ? nullptr : stbi_load_from_memory((const unsigned char*)b.data(), (int)b.size(), &w, &h, &n, 4)) {
        t.id = texUpload(px, w, h); t.w = w; t.h = h; stbi_image_free(px);
    }
    return textures[file] = t;
}

struct ModelGL {
    Model::Model m;
    std::vector<Mesh> meshes;
    std::vector<GLuint> tex;
    int posedClip = -2; float posedAt = -1;
    float fit = 1, centre[3] = {0, 0, 0};   // to 1 across, standing on y = 0, centred
};
static std::map<std::string, std::unique_ptr<ModelGL>> models;
static ModelGL* model(const std::string& file) {
    auto it = models.find(file);
    if (it != models.end()) return it->second.get();
    auto g = std::make_unique<ModelGL>();
    if (Model::load(file, g->m)) {
        for (const auto& t : g->m.textures) g->tex.push_back(texUpload(t.rgba.data(), t.w, t.h));
        for (const auto& p : g->m.prims) {
            std::vector<float> v(p.outPos.size() / 3 * 8);
            for (size_t i = 0; i < p.outPos.size() / 3; ++i) {
                memcpy(&v[i * 8], &p.outPos[i * 3], 12); memcpy(&v[i * 8 + 3], &p.outNrm[i * 3], 12); memcpy(&v[i * 8 + 6], &p.uv[i * 2], 8);
            }
            g->meshes.push_back(upload(v, p.idx, true));
        }
        const float ext = std::max({g->m.hi[0] - g->m.lo[0], g->m.hi[1] - g->m.lo[1], g->m.hi[2] - g->m.lo[2], 1e-6f});
        g->fit = 1.0f / ext;
        g->centre[0] = (g->m.lo[0] + g->m.hi[0]) / 2; g->centre[1] = g->m.lo[1]; g->centre[2] = (g->m.lo[2] + g->m.hi[2]) / 2;
    }
    ModelGL* r = g.get();
    models[file] = std::move(g);
    return r;
}
static void poseModel(ModelGL& g, int clip, float t, bool loop) {
    if (clip < 0 && g.posedClip == -1) return;
    if (clip == g.posedClip && std::fabs(t - g.posedAt) < 1e-4f) return;
    Model::pose(g.m, clip, t, loop);
    g.posedClip = clip; g.posedAt = t;
    for (size_t k = 0; k < g.m.prims.size(); ++k) {
        const auto& p = g.m.prims[k];
        std::vector<float> v(p.outPos.size() / 3 * 8);
        for (size_t i = 0; i < p.outPos.size() / 3; ++i) {
            memcpy(&v[i * 8], &p.outPos[i * 3], 12); memcpy(&v[i * 8 + 3], &p.outNrm[i * 3], 12); memcpy(&v[i * 8 + 6], &p.uv[i * 2], 8);
        }
        glBindBuffer_(GL_ARRAY_BUFFER, g.meshes[k].vbo);
        glBufferData_(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_DYNAMIC_DRAW);
    }
}
unsigned pictureTexture(const std::string& file, int* w, int* h) {
    if (file.empty()) return 0;
    const Tex& t = texture(file); if (w) *w = t.w; if (h) *h = t.h; return t.id;
}
std::vector<std::string> clipsOf(const std::string& file) {
    std::vector<std::string> v;
    if (ModelGL* g = model(file)) for (const auto& c : g->m.clips) v.push_back(c.name);
    return v;
}
void reload(const std::string& file) {
    auto t = textures.find(file); if (t != textures.end()) { glDeleteTextures(1, &t->second.id); textures.erase(t); }
    models.erase(file);
}

// words: drawn once into a texture, by text
static std::map<std::string, Tex> wordTex;
static const Tex& words(const std::string& text) {
    auto it = wordTex.find(text);
    if (it != wordTex.end()) return it->second;
    Tex t;
    std::ifstream f("C:/Windows/Fonts/segoeuib.ttf", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); const std::string font = ss.str();
    stbtt_fontinfo fi;
    if (!font.empty() && stbtt_InitFont(&fi, (const unsigned char*)font.data(), 0) && !text.empty()) {
        const float px = 96, sc = stbtt_ScaleForPixelHeight(&fi, px);
        int asc, desc, gap; stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
        float width = 0; for (unsigned char c : text) { int adv, lsb; stbtt_GetCodepointHMetrics(&fi, c, &adv, &lsb); width += adv * sc; }
        const int W = (int)width + 16, H = (int)((asc - desc) * sc) + 16;
        std::vector<unsigned char> rgba((size_t)W * H * 4, 0);
        float x = 8; const float base = 8 + asc * sc;
        for (unsigned char c : text) {
            int adv, lsb, x0, y0, x1, y1;
            stbtt_GetCodepointHMetrics(&fi, c, &adv, &lsb);
            stbtt_GetCodepointBitmapBox(&fi, c, sc, sc, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> g((size_t)gw * gh);
                stbtt_MakeCodepointBitmap(&fi, g.data(), gw, gh, gw, sc, sc, c);
                for (int yy = 0; yy < gh; ++yy) for (int xx = 0; xx < gw; ++xx) {
                    const int dx = (int)x + x0 + xx, dy = (int)base + y0 + yy;
                    if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
                    unsigned char* p = &rgba[((size_t)dy * W + dx) * 4];
                    p[0] = p[1] = p[2] = 255; p[3] = std::max(p[3], g[(size_t)yy * gw + xx]);
                }
            }
            x += adv * sc;
        }
        t.id = texUpload(rgba.data(), W, H); t.w = W; t.h = H;
    }
    return wordTex[text] = t;
}

static GLuint rayTex = 0, markerTex = 0, deskTex = 0, whiteTex = 0, dotTex = 0;
static int markerW = 1, markerH = 1;
static int anchorMode = 0; static float headAt[3] = {0, 0, -0.6f};
void setAnchorMode(int mode, const float head[3]) { anchorMode = mode; if (head) memcpy(headAt, head, sizeof headAt); }
void setMarkerPicture(const std::vector<unsigned char>& rgba, int w, int h) {
    if (markerTex) glDeleteTextures(1, &markerTex);
    markerTex = rgba.empty() ? 0 : texUpload(rgba.data(), w, h);
    markerW = w; markerH = h;
}

bool init(const std::string& dir) {
    exeDir = dir;
    if (!loadGL()) return false;
    prog = program(
        "#version 330\nlayout(location=0) in vec3 p; layout(location=1) in vec3 n; layout(location=2) in vec2 t;\n"
        "uniform mat4 mvp, model; uniform vec4 clipPlane; out vec3 N; out vec2 T; out vec3 W;\n"
        "void main(){ gl_Position = mvp * vec4(p,1); N = mat3(model) * n; T = t; W = (model * vec4(p,1)).xyz; gl_ClipDistance[0] = dot(model * vec4(p,1), clipPlane); }",
        "#version 330\nin vec3 N; in vec2 T; in vec3 W; uniform sampler2D tex; uniform vec4 colour; uniform int useTex, lit;\n"
        "uniform vec3 sunDir, sun, ambient; uniform int pick; uniform vec3 pickCol; out vec4 o;\n"
        "uniform int nLights; uniform vec4 lPos[8], lCol[8], lDir[8];   // xyz + type; rgb + range; direction + cos(cone)\n"
        "void main(){ vec4 c = colour; if (useTex == 1) c *= texture(tex, T); if (c.a < 0.01) discard;\n"
        "  if (pick == 1) { if (c.a < 0.35) discard; o = vec4(pickCol, 1); return; }\n"
        "  if (lit == 1) { vec3 n = normalize(N); if (!gl_FrontFacing) n = -n; vec3 l = ambient + sun * max(dot(n, sunDir), 0.0);\n"
        "    for (int i = 0; i < nLights; ++i) {\n"
        "      vec3 L; float a = 1.0;\n"
        "      if (lPos[i].w > 1.5) L = -lDir[i].xyz; else { vec3 d = lPos[i].xyz - W; float dist = length(d); L = d / max(dist, 1e-4);\n"
        "        a = pow(clamp(1.0 - dist / max(lCol[i].w, 1e-3), 0.0, 1.0), 2.0);\n"
        "        if (lPos[i].w > 0.5) a *= smoothstep(lDir[i].w, lDir[i].w + 0.08, dot(-L, lDir[i].xyz)); }\n"
        "      l += lCol[i].rgb * a * max(dot(n, L), 0.0); }\n"
        "    c.rgb *= l; }\n"
        "  o = c; }");
    uMVP = glGetUniformLocation_(prog, "mvp"); uModel = glGetUniformLocation_(prog, "model"); uColour = glGetUniformLocation_(prog, "colour");
    uUseTex = glGetUniformLocation_(prog, "useTex"); uLit = glGetUniformLocation_(prog, "lit"); uSunDir = glGetUniformLocation_(prog, "sunDir");
    uSun = glGetUniformLocation_(prog, "sun"); uAmbient = glGetUniformLocation_(prog, "ambient"); uTex = glGetUniformLocation_(prog, "tex"); uClip = glGetUniformLocation_(prog, "clipPlane"); uNL = glGetUniformLocation_(prog, "nLights"); uLPos = glGetUniformLocation_(prog, "lPos"); uLCol = glGetUniformLocation_(prog, "lCol"); uLDir = glGetUniformLocation_(prog, "lDir"); uPick = glGetUniformLocation_(prog, "pick"); uPickCol = glGetUniformLocation_(prog, "pickCol");
    progLine = program(
        "#version 330\nlayout(location=0) in vec3 p; uniform mat4 mvp; void main(){ gl_Position = mvp * vec4(p,1); }",
        "#version 330\nuniform vec4 colour; out vec4 o; void main(){ o = colour; }");
    uLineMVP = glGetUniformLocation_(progLine, "mvp"); uLineColour = glGetUniformLocation_(progLine, "colour");
    makePrimitives();
    // a light ray: bright down the middle, soft at the sides, fading downwards
    { std::vector<unsigned char> r(64 * 64 * 4);
      for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
          const float u = std::fabs(x / 31.5f - 1), v = y / 63.0f;
          const float a = std::pow(std::max(0.0f, 1 - u), 2.0f) * (1 - v) * (0.3f + 0.7f * std::min(1.0f, v * 6));
          unsigned char* p = &r[(y * 64 + x) * 4]; p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255); }
      rayTex = texUpload(r.data(), 64, 64); }
    { const unsigned char w[4] = {255, 255, 255, 255}; whiteTex = texUpload(w, 1, 1, false); }
    { std::vector<unsigned char> d(64 * 64 * 4);
      for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
          const float r = std::sqrt((x - 31.5f) * (x - 31.5f) + (y - 31.5f) * (y - 31.5f)) / 31.5f;
          unsigned char* q = &d[(y * 64 + x) * 4]; q[0] = q[1] = q[2] = 255; q[3] = (unsigned char)(255 * std::pow(std::max(0.0f, 1 - r), 1.6f)); }
      dotTex = texUpload(d.data(), 64, 64); }
    // a plain desk: mid grey with a faint grain
    { std::vector<unsigned char> d(256 * 256 * 4);
      for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
          const float g = 0.42f + 0.03f * std::sin(y * 0.21f + std::sin(x * 0.05f) * 3) + 0.015f * std::sin(x * 1.7f + y * 0.3f);
          unsigned char* p = &d[(y * 256 + x) * 4]; p[0] = (unsigned char)(g * 255 * 1.02f); p[1] = (unsigned char)(g * 255); p[2] = (unsigned char)(g * 255 * 0.96f); p[3] = 255; }
      deskTex = texUpload(d.data(), 256, 256); }
    return true;
}

// ---------- the camera ----------
static float yaw = 0.0f, pitch = 0.75f, dist = 2.6f, target[3] = {0, 0.15f, 0};
void frameMarker() { yaw = 0; pitch = 0.75f; dist = 2.6f; target[0] = 0; target[1] = 0.15f; target[2] = 0; }

static GLuint fbo = 0, fboTex = 0, fboDepth = 0; static int fbW = 0, fbH = 0;
static void ensureTarget(int w, int h) {
    if (fbo && w == fbW && h == fbH) return;
    if (!fbo) { glGenFramebuffers_(1, &fbo); glGenTextures(1, &fboTex); glGenRenderbuffers_(1, &fboDepth); }
    fbW = w; fbH = h;
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer_(GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
}

// ---------- drawing ----------
struct Draw { M4 world; const Object* o; float depth; bool inside; bool blend; int part = 0; };
static M4 viewProj, viewM;
static float eye[3];
static const Marker* curMarker = nullptr;

static void setCommon(const M4& world, const float* colour, GLuint tex, bool lit) {
    const M4 mvp = mul(viewProj, world);
    glUniformMatrix4fv_(uMVP, 1, GL_FALSE, mvp.m); glUniformMatrix4fv_(uModel, 1, GL_FALSE, world.m);
    glUniform4f_(uColour, colour[0], colour[1], colour[2], colour[3]);
    glUniform1i_(uUseTex, tex ? 1 : 0); glUniform1i_(uLit, lit ? 1 : 0);
    glActiveTexture_(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex ? tex : whiteTex);
}

// an object's own transform, with what it does by itself added on
static M4 localOf(const Object& o, float t, float* opacityMul) {
    float pos[3] = {o.pos[0], o.pos[1], o.pos[2]}, rot[3] = {o.rot[0], o.rot[1], o.rot[2]};
    const float ph = t * o.animSpeed * 6.2831853f;
    if (opacityMul) *opacityMul = 1;
    switch (o.hasAnim && o.animOn ? o.anim : A_STILL) {
    case A_FLOAT: pos[1] += std::sin(ph) * o.animAmount; break;
    case A_SPIN: rot[1] += t * o.animSpeed * 360.0f; break;
    case A_SWAY: rot[2] += std::sin(ph) * o.animAmount * 90.0f; break;
    case A_PULSE: if (opacityMul) *opacityMul = 1 - o.animAmount * (0.5f + 0.5f * std::sin(ph)); break;
    default: break;
    }
    return mul(translate(pos[0], pos[1], pos[2]), mul(euler(rot), scale(o.scl[0], o.scl[1], o.scl[2])));
}
static float hash(int i, int k) { unsigned h = (unsigned)i * 0x8da6b343u ^ (unsigned)k * 0xd8163841u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xffffff) / 16777215.0f; }

// a world matrix with its rotation replaced by one facing the camera (keeping its size)
static M4 facing(const M4& w, float sx, float sy) {
    M4 r = ident();
    // the camera's right and up, from the view matrix's rows
    for (int k = 0; k < 3; ++k) { r.m[k] = viewM.m[k * 4] * sx; r.m[4 + k] = viewM.m[k * 4 + 1] * sy; r.m[8 + k] = viewM.m[k * 4 + 2]; }
    r.m[12] = w.m[12]; r.m[13] = w.m[13]; r.m[14] = w.m[14];
    return r;
}
static float lengthCol(const M4& w, int c) { return std::sqrt(w.m[c * 4] * w.m[c * 4] + w.m[c * 4 + 1] * w.m[c * 4 + 1] + w.m[c * 4 + 2] * w.m[c * 4 + 2]); }

static void drawExtras(const Draw& d, float t);
static void drawObject(const Draw& d, float t) {
    if (d.part == 1) { drawExtras(d, t); return; }
    const Object& o = *d.o;
    if (o.occluder) glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    float om = 1; localOf(o, t, &om);
    float col[4] = {o.colour[0], o.colour[1], o.colour[2], o.colour[3] * om};
    if (o.glow) glBlendFunc(GL_SRC_ALPHA, GL_ONE); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // a Video Player shows its first frame (made by ffmpeg beside the video) where the look has no texture
    const GLuint tex = !o.file.empty() ? texture(o.file).id
                     : o.hasVideo && o.videoOn && !o.videoFile.empty() ? texture(o.videoFile + ".poster.jpg").id : 0;
    switch (o.kind) {
    case PLANE: setCommon(d.world, col, tex, o.lit); draw(mPlane); break;
    case CUBE: setCommon(d.world, col, tex, o.lit); draw(mCube); break;
    case SPHERE: setCommon(d.world, col, tex, o.lit); draw(mSphere); break;
    case CAPSULE: setCommon(d.world, col, tex, o.lit); draw(mCapsule); break;
    case CYLINDER: setCommon(d.world, col, tex, o.lit); draw(mCylinder); break;
    case QUAD: setCommon(o.billboard ? facing(d.world, lengthCol(d.world, 0), lengthCol(d.world, 1)) : d.world, col, tex, o.lit); draw(mQuad); break;
    case PICTURE: {
        const Tex& tx = texture(o.file);
        const float aspect = tx.w > 0 ? (float)tx.h / tx.w : 1;
        if (o.billboard) { setCommon(mul(facing(d.world, lengthCol(d.world, 0), lengthCol(d.world, 0) * aspect), rotX(1.5707963f)), col, tx.id, o.lit); }
        else setCommon(mul(d.world, scale(1, 1, aspect)), col, tx.id, o.lit);
        draw(mPicture);
        break;
    }
    case WORDS: {
        const Tex& tx = words(o.text);
        const float aspect = tx.w > 0 ? (float)tx.h / tx.w : 0.3f;
        const M4 w = o.billboard ? facing(d.world, lengthCol(d.world, 0), lengthCol(d.world, 0) * aspect) : mul(d.world, scale(1, aspect, 1));
        setCommon(mul(w, rotY(3.14159265f)), col, tx.id, false);
        draw(mQuad);
        break;
    }
    case MODEL: {
        ModelGL* g = o.file.empty() ? nullptr : model(o.file);
        if (!g || g->meshes.empty()) break;
        const int clip = o.clip.empty() ? -1 : Model::findClip(g->m, o.clip);
        poseModel(*g, clip, lk.animate ? t * o.clipSpeed : 0, o.clipLoop);
        const M4& w = d.world;   // the model's own units and origin, as Unity imports it
        for (size_t k = 0; k < g->meshes.size(); ++k) {
            const auto& p = g->m.prims[k];
            float c[4] = {p.colour[0] * col[0], p.colour[1] * col[1], p.colour[2] * col[2], p.colour[3] * col[3]};
            setCommon(w, c, p.texture >= 0 ? g->tex[p.texture] : 0, o.lit);
            draw(g->meshes[k]);
        }
        break;
    }
    case SPRITES: {
        const Tex& tx = texture(o.file);
        const float aspect = tx.w > 0 ? (float)tx.h / tx.w : 1;
        glDepthMask(GL_FALSE);
        for (int i = 0; i < o.count; ++i) {
            // each flies its own loop through the box (-0.5..0.5 of the object's scale)
            const float s = o.animSpeed * (0.6f + 0.8f * hash(i, 7)), ph = hash(i, 1) * 6.28f;
            const float lp[3] = {0.45f * std::sin(t * s * 0.9f + ph) * (0.6f + 0.4f * hash(i, 2)),
                                 0.45f * std::sin(t * s * 1.3f + ph * 1.7f) * (0.6f + 0.4f * hash(i, 3)),
                                 0.45f * std::cos(t * s * 0.7f + ph * 0.6f) * (0.6f + 0.4f * hash(i, 4))};
            float wp[3]; apply(d.world, lp, wp);
            const float flap = o.flap > 0 ? 0.25f + 0.75f * std::fabs(std::cos(t * 9.0f * o.flap + ph * 3)) : 1;
            const float sz = o.spriteSize * (0.7f + 0.6f * hash(i, 5));
            M4 w = facing(translate(wp[0], wp[1], wp[2]), sz * flap, sz * aspect);
            setCommon(mul(w, rotX(1.5707963f)), col, tx.id, false);
            draw(mPicture);
        }
        glDepthMask(GL_TRUE);
        break;
    }
    case RAYS: {
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int i = 0; i < o.rays; ++i) {
            const float a = (i - (o.rays - 1) / 2.0f) * 0.22f + 0.05f * std::sin(t * 0.7f + i);
            const float flick = 0.6f + 0.4f * std::sin(t * 1.3f + i * 2.1f);
            float c[4] = {col[0], col[1], col[2], col[3] * flick};
            setCommon(mul(d.world, mul(rotZ(a), translate(0, 0, (hash(i, 9) - 0.5f) * 0.3f))), c, rayTex, false);
            draw(mRay);
        }
        glDepthMask(GL_TRUE);
        break;
    }
    default: break;
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (o.occluder) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}


// an object's world at another time (its trail): its own animation and its parents', walked up
static M4 worldAt(const Marker& m, const Object& o, float t) {
    M4 w = localOf(o, t, nullptr);
    for (int pid = o.parent; pid != -1;) {
        const int i = indexOfId(m, pid);
        if (i < 0) break;
        w = mul(localOf(m.objects[i], t, nullptr), w);
        pid = m.objects[i].parent;
    }
    return w;
}
// a thin box from a to b (lines and trails), width across
static void segment(const float* a, const float* b, float width, const float* col) {
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-5f) return;
    for (auto& v : d) v /= len;
    // a basis with y along the segment
    float x[3] = {d[1], -d[0], 0}; if (std::fabs(d[2]) < 0.9f) { x[0] = -d[1]; x[1] = d[0]; x[2] = 0; } else { x[0] = 0; x[1] = d[2]; x[2] = -d[1]; }
    float xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); for (auto& v : x) v /= xl;
    const float z[3] = {x[1] * d[2] - x[2] * d[1], x[2] * d[0] - x[0] * d[2], x[0] * d[1] - x[1] * d[0]};
    M4 m = ident();
    for (int k = 0; k < 3; ++k) { m.m[k] = x[k] * width; m.m[4 + k] = d[k] * len; m.m[8 + k] = z[k] * width; m.m[12 + k] = (a[k] + b[k]) / 2; }
    setCommon(m, col, 0, false); draw(mCube);
}
static void drawExtras(const Draw& d, float t) {
    const Object& o = *d.o;
    if (o.hasParticles && o.particlesOn) {
        // each slot k is born at k / rate and lives pLife, then again: a function of time, the same in the game
        const int n = std::min(600, std::max(1, (int)std::ceil(o.pRate * o.pLife)));
        const float T = n / std::max(0.01f, o.pRate);
        const GLuint tx = o.particleFile.empty() ? dotTex : texture(o.particleFile).id;
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, o.pGlow ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        const float spread = o.pSpread * D2R;
        for (int k = 0; k < n; ++k) {
            const float since = t - k / o.pRate;
            if (since < 0) continue;
            const int cycle = (int)std::floor(since / T);
            const float age = since - cycle * T;
            if (age > o.pLife) continue;
            const float u = age / std::max(0.001f, o.pLife);
            const int seed = k * 131 + cycle * 7919;
            const float az = hash(seed, 1) * 6.2831853f, el = spread * std::sqrt(hash(seed, 2));
            const float dir[3] = {std::sin(el) * std::cos(az), std::cos(el), std::sin(el) * std::sin(az)};
            const float sp = o.pSpeed * (0.7f + 0.6f * hash(seed, 3));
            const float lp[3] = {dir[0] * sp * age, dir[1] * sp * age - 0.5f * o.pGravity * age * age, dir[2] * sp * age};
            float wp[3]; apply(d.world, lp, wp);
            float c[4]; for (int q = 0; q < 4; ++q) c[q] = o.pColourStart[q] + (o.pColourEnd[q] - o.pColourStart[q]) * u;
            const float sz = o.pSizeStart + (o.pSizeEnd - o.pSizeStart) * u;
            setCommon(mul(facing(translate(wp[0], wp[1], wp[2]), sz, sz), rotX(1.5707963f)), c, tx, false);
            draw(mPicture);
        }
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (o.hasLine && o.lineOn) {
        const size_t np = o.linePoints.size() / 3;
        for (size_t i = 0; i + 1 < np + (o.lineLoop ? 1 : 0); ++i) {
            float a[3], b[3];
            apply(d.world, &o.linePoints[i * 3], a); apply(d.world, &o.linePoints[((i + 1) % np) * 3], b);
            segment(a, b, o.lineWidth, o.lineColour);
        }
    }
    if (o.hasTrail && o.trailOn && curMarker) {
        const int steps = 20;
        float prev[3] = {0, 0, 0};
        for (int i = 0; i <= steps; ++i) {
            const float tt = t - o.trailTime * i / steps;
            const M4 w = worldAt(*curMarker, o, tt);
            const float cur[3] = {w.m[12], w.m[13], w.m[14]};
            if (i) {
                float c[4] = {o.trailColour[0], o.trailColour[1], o.trailColour[2], o.trailColour[3] * (1 - (float)i / steps)};
                segment(prev, cur, o.trailWidth * (1 - 0.8f * i / steps), c);
            }
            memcpy(prev, cur, sizeof prev);
        }
    }
}

static void lines(const std::vector<float>& pts, const float* colour, const M4& world) {
    static GLuint vao = 0, vbo = 0;
    if (!vao) { glGenVertexArrays_(1, &vao); glGenBuffers_(1, &vbo); }
    glBindVertexArray_(vao); glBindBuffer_(GL_ARRAY_BUFFER, vbo);
    glBufferData_(GL_ARRAY_BUFFER, pts.size() * 4, pts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray_(0); glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glUseProgram_(progLine);
    const M4 mvp = mul(viewProj, world);
    glUniformMatrix4fv_(uLineMVP, 1, GL_FALSE, mvp.m);
    glUniform4f_(uLineColour, colour[0], colour[1], colour[2], colour[3]);
    glDrawArrays(GL_LINES, 0, (GLsizei)(pts.size() / 3));
    glUseProgram_(prog);
}

static int lastPicked = -1;
static int clickX = -1, clickY = -1;   // a click to answer this frame (pixels from the view's top-left)
static bool redrawing = false;          // drawing the picture again after a pick (time doesn't move)
static std::map<int, std::pair<M4, M4>> lastWorld;   // id -> (world, parent world), as last drawn
static std::vector<std::pair<int, std::pair<float, float>>> screenCentres;   // id, its centre on screen (for clicks)

unsigned render(const Project& p, int mi, int selected, int w, int h, bool hovered, int& picked) {
    picked = -1;
    if (w < 8 || h < 8) return 0;
    ImGuiIO& io = ImGui::GetIO();
    if (hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3)) { yaw -= io.MouseDelta.x * 0.008f; pitch = std::clamp(pitch + io.MouseDelta.y * 0.008f, -0.2f, 1.55f); }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            const float k = dist * 0.0015f;
            target[0] -= (std::cos(yaw) * io.MouseDelta.x) * k; target[2] -= (-std::sin(yaw) * io.MouseDelta.x) * k; target[1] += io.MouseDelta.y * k;
        }
        if (io.MouseWheel != 0) dist = std::clamp(dist * std::pow(0.88f, io.MouseWheel), 0.3f, 30.0f);
        // a click (not a drag): what's under the mouse is found after drawing (each object in its own colour)
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0).x == 0 && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0).y == 0) {
            const ImVec2 mp = ImGui::GetMousePos(), wp = ImGui::GetItemRectMin();
            clickX = (int)(mp.x - wp.x); clickY = (int)(mp.y - wp.y);
        }
    }
    if (lk.animate && !redrawing) lk.time += io.DeltaTime;
    const float t = lk.time;

    ensureTarget(w, h);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.10f, 1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_CULL_FACE);

    eye[0] = target[0] + dist * std::cos(pitch) * std::sin(yaw); eye[1] = target[1] + dist * std::sin(pitch); eye[2] = target[2] + dist * std::cos(pitch) * std::cos(yaw);
    viewM = lookAt(eye, target);
    viewProj = mul(perspective(0.8f, (float)w / h, 0.02f, 100), viewM);

    const Marker* m = mi >= 0 && mi < (int)p.markers.size() ? &p.markers[mi] : nullptr;
    curMarker = m;
    glUseProgram_(prog);
    glUniform1i_(uTex, 0); glUniform4f_(uClip, 0, 0, 0, 1); glUniform1i_(uPick, 0);
    const float sy = m ? m->sunYaw * D2R : 0.6f, sp = m ? m->sunPitch * D2R : 0.9f;
    glUniform3f_(uSunDir, std::cos(sp) * std::sin(sy), std::sin(sp), std::cos(sp) * std::cos(sy));
    if (m) { glUniform3f_(uSun, m->sun[0] * m->sunStrength, m->sun[1] * m->sunStrength, m->sun[2] * m->sunStrength); glUniform3f_(uAmbient, m->ambient[0], m->ambient[1], m->ambient[2]); }
    else { glUniform3f_(uSun, 0.9f, 0.9f, 0.9f); glUniform3f_(uAmbient, 0.5f, 0.5f, 0.5f); }

    // the window into the paper: its opening marked in the stencil first, so the desk leaves it open
    const bool portal = m && m->portal && anchorMode != 2;
    const M4 portalM = portal ? scale(m->portalSize[0], 1, m->portalSize[1]) : ident();
    if (portal) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_FALSE);
        const float c[4] = {1, 1, 1, 1};
        setCommon(mul(portalM, scale(1, 1, 1)), c, 0, false); draw(mPicture);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
        glStencilFunc(GL_NOTEQUAL, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }
    // the desk and the printed marker (the marker's black square is 1 across; the print has a white margin)
    if (lk.desk && anchorMode != 2) {
        const float c[4] = {1, 1, 1, 1};
        setCommon(mul(translate(0, -0.002f, 0), scale(1.2f, 1, 1.2f)), c, deskTex, true); draw(mPlane);
    }
    if (anchorMode == 2) {   // a face: a plain head, the anchor point on it (the face looks along +Z, at the viewer)
        const float c[4] = {0.62f, 0.6f, 0.6f, 1};
        setCommon(mul(translate(headAt[0], headAt[1], headAt[2]), scale(1.0f, 1.3f, 1.15f)), c, 0, true); draw(mSphere);
        const float n[4] = {0.55f, 0.53f, 0.53f, 1};   // a nose, so it's clear which way it faces
        setCommon(mul(translate(headAt[0], headAt[1] + 0.02f, headAt[2] + 0.6f), scale(0.14f, 0.22f, 0.18f)), n, 0, true); draw(mSphere);
    } else if (markerTex) {
        const float c[4] = {1, 1, 1, 1};
        // a printed marker has its white margin round the black square (1 across); a picture is just itself, 1 wide
        const float sx = anchorMode == 1 ? 1.0f : 1.25f, sz = anchorMode == 1 ? (float)markerH / std::max(1, markerW) : 1.25f;
        setCommon(scale(sx, 1, sz), c, markerTex, true); draw(mPicture);
    }
    if (portal) {   // inside the window: dark walls going down, only seen through the opening
        glStencilFunc(GL_EQUAL, 1, 0xff);
        const float c[4] = {0.12f, 0.12f, 0.13f, 1};
        setCommon(mul(portalM, scale(1, 1.5f, 1)), c, 0, true); draw(mPortalBox);
    }

    // the objects: world matrices down the hierarchy; opaque first, then see-through ones far to near
    std::vector<Draw> draws;
    screenCentres.clear();
    lastWorld.clear();
    float lp[32] = {0}, lc[32] = {0}, ld[32] = {0}; int nl = 0;
    if (m) {
        std::map<int, M4> worldOf;
        for (const Object& o : m->objects) {
            if (!o.active) continue;
            bool parentOn = o.parent == -1;
            M4 parent = ident();
            if (o.parent != -1) { auto it = worldOf.find(o.parent); if (it == worldOf.end()) continue; parent = it->second; parentOn = true; }
            if (!parentOn) continue;
            const M4 world = mul(parent, localOf(o, t, nullptr));
            worldOf[o.id] = world;
            lastWorld[o.id] = {world, parent};
            const float c[3] = {world.m[12], world.m[13], world.m[14]};
            const float vz = viewM.m[2] * c[0] + viewM.m[6] * c[1] + viewM.m[10] * c[2] + viewM.m[14];
            const bool extras = (o.hasParticles && o.particlesOn) || (o.hasLine && o.lineOn) || (o.hasTrail && o.trailOn);
            if (extras) draws.push_back({world, &o, vz, portal && c[1] < -0.01f, true, 1});
            if (o.hasLight && o.lightOn && nl < 8) {   // its light, for the shader
                const float fwd[3] = {-world.m[8], -world.m[9], -world.m[10]};
                const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]) + 1e-6f;
                float* P = &lp[nl * 4]; float* C = &lc[nl * 4]; float* Dd = &ld[nl * 4];
                P[0] = world.m[12]; P[1] = world.m[13]; P[2] = world.m[14]; P[3] = (float)o.lightType;
                for (int k = 0; k < 3; ++k) { C[k] = o.lightColour[k] * o.lightIntensity; Dd[k] = fwd[k] / fl; }
                C[3] = o.lightRange; Dd[3] = std::cos(o.spotAngle * 0.5f * D2R);
                ++nl;
            }
            if (!o.lookOn || o.kind == EMPTY) continue;   // (its children are placed already)
            const bool blend = !o.occluder && (o.kind == PICTURE || o.kind == SPRITES || o.kind == RAYS || o.kind == WORDS || o.colour[3] < 0.999f || o.glow);
            draws.push_back({world, &o, vz, portal && c[1] < -0.01f, blend, 0});
            // where it is on screen (for clicking it)
            const float cc[4] = {c[0], c[1], c[2], 1};
            float clip[4] = {0, 0, 0, 0};
            for (int r = 0; r < 4; ++r) clip[r] = viewProj.m[r] * cc[0] + viewProj.m[4 + r] * cc[1] + viewProj.m[8 + r] * cc[2] + viewProj.m[12 + r];
            if (clip[3] > 0) screenCentres.push_back({o.id, {(clip[0] / clip[3] * 0.5f + 0.5f) * w, (1 - (clip[1] / clip[3] * 0.5f + 0.5f)) * h}});
        }
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
        if (a.blend != b.blend) return !a.blend;
        if (!a.blend && a.o->occluder != b.o->occluder) return a.o->occluder;   // occluders first: they hide what's behind
        return a.blend ? a.depth < b.depth : false;
    });
    glUniform1i_(uNL, nl); if (nl) { glUniform4fv_(uLPos, nl, lp); glUniform4fv_(uLCol, nl, lc); glUniform4fv_(uLDir, nl, ld); }
    // with a window into the paper, each object is cut at the paper: below it only shows through the opening
    if (portal) glEnable(GL_CLIP_DISTANCE0);
    for (const Draw& d : draws) {
        if (portal) {
            glStencilFunc(GL_EQUAL, 1, 0xff); glUniform4f_(uClip, 0, -1, 0, 0); drawObject(d, t);
            glStencilFunc(GL_ALWAYS, 0, 0xff); glUniform4f_(uClip, 0, 1, 0, 0); drawObject(d, t);
        } else drawObject(d, t);
    }
    glDisable(GL_CLIP_DISTANCE0); glUniform4f_(uClip, 0, 0, 0, 1);
    glDisable(GL_STENCIL_TEST);

    // a click: draw again in id colours and read the pixel under the mouse (0: nothing)
    if (clickX >= 0 && clickY >= 0 && clickX < w && clickY < h) {
        glClearColor(0, 0, 0, 1); glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDisable(GL_BLEND);
        glUniform1i_(uPick, 1);
        if (portal) {   // the opening, as before, and the desk round it hiding what's under the paper
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_FALSE);
            const float c[4] = {1, 1, 1, 1};
            setCommon(portalM, c, 0, false); draw(mPicture);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilFunc(GL_NOTEQUAL, 1, 0xff);
        }
        {   // the desk and paper: they hide what's under them, but aren't objects (black)
            glUniform3f_(uPickCol, 0, 0, 0);
            const float c[4] = {1, 1, 1, 1};
            setCommon(mul(translate(0, -0.002f, 0), scale(1.2f, 1, 1.2f)), c, 0, false); draw(mPlane);
        }
        if (portal) glEnable(GL_CLIP_DISTANCE0);
        for (const Draw& d : draws) {
            const int id = d.o->id;
            glUniform3f_(uPickCol, (id & 255) / 255.0f, ((id >> 8) & 255) / 255.0f, ((id >> 16) & 255) / 255.0f);
            if (portal) {
                glStencilFunc(GL_EQUAL, 1, 0xff); glUniform4f_(uClip, 0, -1, 0, 0); drawObject(d, t);
                glStencilFunc(GL_ALWAYS, 0, 0xff); glUniform4f_(uClip, 0, 1, 0, 0); drawObject(d, t);
            } else drawObject(d, t);
        }
        glDisable(GL_CLIP_DISTANCE0); glUniform4f_(uClip, 0, 0, 0, 1); glDisable(GL_STENCIL_TEST);
        unsigned char px[4] = {0, 0, 0, 0};
        glReadPixels(clickX, h - 1 - clickY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        const int id = px[0] | (px[1] << 8) | (px[2] << 16);
        picked = id > 0 ? id : -2;   // -2: clicked on nothing
        glUniform1i_(uPick, 0);
        glEnable(GL_BLEND);
        // the picture as it was (the pick pass drew over it)
        clickX = clickY = -1;
        glBindFramebuffer_(GL_FRAMEBUFFER, 0);
        redrawing = true; int none = -1; render(p, mi, selected, w, h, false, none); redrawing = false;
        return fboTex;
    }
    clickX = clickY = -1;

    // the selected object: its axes (red x, green y, blue z), drawn over everything
    if (lk.gizmo && selected >= 0) {
        for (const Draw& d : draws) if (d.o->id == selected) {
            glDisable(GL_DEPTH_TEST);
            M4 at = translate(d.world.m[12], d.world.m[13], d.world.m[14]);
            for (int k = 0; k < 3; ++k) for (int r = 0; r < 3; ++r) at.m[k * 4 + r] = d.world.m[k * 4 + r] / std::max(1e-6f, lengthCol(d.world, k)) * 0.25f;
            const float rc[4] = {0.9f, 0.25f, 0.25f, 1}, gc[4] = {0.35f, 0.85f, 0.35f, 1}, bc[4] = {0.3f, 0.55f, 1, 1};
            lines({0, 0, 0, 1, 0, 0}, rc, at); lines({0, 0, 0, 0, 1, 0}, gc, at); lines({0, 0, 0, 0, 0, 1}, bc, at);
            glEnable(GL_DEPTH_TEST);
        }
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    (void)lastPicked;
    return fboTex;
}

bool matrices(int id, float world[16], float parent[16]) {
    auto it = lastWorld.find(id);
    if (it == lastWorld.end()) return false;
    memcpy(world, it->second.first.m, 64); memcpy(parent, it->second.second.m, 64);
    return true;
}
bool toScreen(const float p[3], float& x, float& y) {
    float c[4];
    for (int r = 0; r < 4; ++r) c[r] = viewProj.m[r] * p[0] + viewProj.m[4 + r] * p[1] + viewProj.m[8 + r] * p[2] + viewProj.m[12 + r];
    if (c[3] <= 1e-4f) return false;
    x = (c[0] / c[3] * 0.5f + 0.5f) * fbW; y = (1 - (c[1] / c[3] * 0.5f + 0.5f)) * fbH;
    return true;
}
void clickAt(int x, int y) { clickX = x; clickY = y; }
void eyePos(float out[3]) { out[0] = eye[0]; out[1] = eye[1]; out[2] = eye[2]; }
void focusOn(const float p[3], float size) { target[0] = p[0]; target[1] = p[1]; target[2] = p[2]; dist = std::clamp(size * 3.0f, 0.4f, 30.0f); }

void readPixels(std::vector<unsigned char>& rgba, int& w, int& h) {
    w = fbW; h = fbH; rgba.assign((size_t)w * h * 4, 0);
    if (!fbo) return;
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    std::vector<unsigned char> flip(rgba.size());
    for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    rgba.swap(flip);
}

}
