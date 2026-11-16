// Diorama - SLAM for the native player: see slam.h.
#include "slam.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

#ifdef DIORAMA_SLAM
#include <stella_vslam/config.h>
#include <stella_vslam/data/landmark.h>
#include <stella_vslam/publish/map_publisher.h>
#include <stella_vslam/system.h>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>
#include <set>
#endif
#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#endif

namespace fs = std::filesystem;

namespace Slam {

#ifdef DIORAMA_SLAM
bool available() { return true; }
#else
bool available() { return false; }
#endif

std::string findVocabulary(const std::string& exeDir, bool download) {
    std::vector<fs::path> where = {fs::u8path(exeDir) / "slam" / "orb_vocab.fbow", "F:/AIModels/slam/orb_vocab.fbow"};
    fs::path local;
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA")) { local = fs::u8path(la) / "Diorama" / "slam" / "orb_vocab.fbow"; where.push_back(local); }
#endif
    std::error_code ec;
    for (const auto& p : where) if (fs::exists(p, ec) && fs::file_size(p, ec) > 1000000) return p.u8string();
#ifdef _WIN32
    if (download && !local.empty()) {   // once: stella_vslam's ORB vocabulary (45 MB)
        fs::create_directories(local.parent_path(), ec);
        const std::wstring url = L"https://github.com/stella-cv/FBoW_orb_vocab/raw/main/orb_vocab.fbow";
        if (SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), local.wstring().c_str(), 0, nullptr)) && fs::exists(local, ec)) return local.u8string();
    }
#else
    (void)download;
#endif
    return "";
}

// ---- planes from points: RANSAC, one plane at a time, its points taken out, again
static std::vector<Plane> fitPlanes(std::vector<cv::Point3d> pts, const cv::Vec3d& up, double tol, int minPts, int& nextId, const std::vector<Plane>& before) {
    std::vector<Plane> out;
    std::mt19937 rng(7);
    for (int round = 0; round < 6 && (int)pts.size() >= minPts; ++round) {
        int best = 0; cv::Vec3d bn; double bd = 0;
        std::uniform_int_distribution<size_t> pick(0, pts.size() - 1);
        for (int it = 0; it < 300; ++it) {
            const cv::Point3d a = pts[pick(rng)], b = pts[pick(rng)], c = pts[pick(rng)];
            cv::Vec3d n = cv::Vec3d(b - a).cross(cv::Vec3d(c - a));
            const double l = cv::norm(n); if (l < 1e-9) continue; n /= l;
            const double ad = std::fabs(n.dot(up));
            if (cv::norm(up) > 0.5 && ad < 0.85 && ad > 0.35) continue;   // floors/tables (up) and walls (sideways) only
            const double d = -n.dot(cv::Vec3d(a));
            int cnt = 0; for (const auto& p : pts) if (std::fabs(n.dot(cv::Vec3d(p)) + d) < tol) ++cnt;
            if (cnt > best) { best = cnt; bn = n; bd = d; }
        }
        if (best < minPts) break;
        bool again = false;   // a layer of a plane already found (a noisy map): its points go with it, not a new plane
        for (const auto& q : out) {
            const cv::Vec3d qy(q.toWorld(0, 1), q.toWorld(1, 1), q.toWorld(2, 1)), qc(q.toWorld(0, 3), q.toWorld(1, 3), q.toWorld(2, 3));
            if (std::fabs(qy.dot(bn)) > 0.97 && std::fabs(bn.dot(qc) + bd) < tol * 4) { again = true; break; }
        }
        if (again) { std::vector<cv::Point3d> rest; for (const auto& p : pts) if (std::fabs(bn.dot(cv::Vec3d(p)) + bd) >= tol * 2) rest.push_back(p); pts.swap(rest); continue; }
        std::vector<cv::Point3d> in, rest;
        for (const auto& p : pts) (std::fabs(bn.dot(cv::Vec3d(p)) + bd) < tol ? in : rest).push_back(p);
        pts.swap(rest);
        Plane P; P.vertical = std::fabs(bn.dot(up)) < 0.5; P.points = (int)in.size();
        // its frame: y out of it towards the camera's side (up for a floor), x as level as can be
        cv::Vec3d y = bn; if (!P.vertical && y.dot(up) < 0) y = -y;
        cv::Point3d c(0, 0, 0); for (const auto& p : in) c += p; c *= 1.0 / in.size();
        if (P.vertical && y.dot(-cv::Vec3d(c)) < 0) y = -y;   // a wall: its front faces where the camera began
        cv::Vec3d x = P.vertical ? up.cross(y) : cv::Vec3d(1, 0, 0) - y * y[0]; x /= std::max(1e-9, cv::norm(x));
        const cv::Vec3d z = x.cross(y);
        const cv::Vec3d o = cv::Vec3d(c) - y * (y.dot(cv::Vec3d(c)) + 0);   // (the centroid lies on it already)
        (void)o;
        P.toWorld = cv::Matx44d(x[0], y[0], z[0], c.x, x[1], y[1], z[1], c.y, x[2], y[2], z[2], c.z, 0, 0, 0, 1);
        std::vector<cv::Point2f> flat; for (const auto& p : in) { const cv::Vec3d v = cv::Vec3d(p - c); flat.push_back(cv::Point2f((float)v.dot(x), (float)v.dot(z))); }
        cv::convexHull(flat, P.boundary);
        // the same plane as before (it keeps its id: scripts and placed things stay with it)
        P.id = 0;
        for (const auto& q : before) {
            const cv::Vec3d qy(q.toWorld(0, 1), q.toWorld(1, 1), q.toWorld(2, 1)), qc(q.toWorld(0, 3), q.toWorld(1, 3), q.toWorld(2, 3));
            if (std::fabs(qy.dot(y)) > 0.95 && std::fabs(y.dot(cv::Vec3d(c) - qc)) < tol * 3) { P.id = q.id; break; }
        }
        if (!P.id) P.id = nextId++;
        out.push_back(P);
    }
    return out;
}

#ifdef DIORAMA_SLAM
struct Session::Impl {
    std::unique_ptr<stella_vslam::system> sys;
    double height = 0.6, scale = 0;   // scale: metres per map unit (0: not yet)
    cv::Vec3d up = cv::Vec3d(0, -1, 0);   // the first view's up (OpenCV: y down), until the map's floor says otherwise
    bool upFixed = false;
    std::vector<cv::Point3d> pts;
    std::vector<Plane> planes;
    int nextId = 1, mapPoints = 0;
    double lastFit = -1;
    bool started = false;
    cv::Vec3d camPos;
};
Session::Session() : d_(new Impl) {}
Session::~Session() { if (d_->sys) d_->sys->shutdown(); }

bool Session::start(int w, int h, double fx, double fy, double cx, double cy, const std::string& vocab, double height, std::string& err) {
    try {
        YAML::Node y;
        y["Camera"]["name"] = "webcam"; y["Camera"]["setup"] = "monocular"; y["Camera"]["model"] = "perspective";
        y["Camera"]["fx"] = fx; y["Camera"]["fy"] = fy; y["Camera"]["cx"] = cx; y["Camera"]["cy"] = cy;
        for (const char* k : {"k1", "k2", "p1", "p2", "k3"}) y["Camera"][k] = 0.0;
        y["Camera"]["fps"] = 30.0; y["Camera"]["cols"] = w; y["Camera"]["rows"] = h; y["Camera"]["color_order"] = "BGR";
        y["Preprocessing"]["min_size"] = 800; y["Preprocessing"]["max_num_keypoints"] = 2000;
        y["Feature"]["name"] = "default ORB feature extraction setting"; y["Feature"]["scale_factor"] = 1.2; y["Feature"]["num_levels"] = 8;
        y["Feature"]["ini_fast_threshold"] = 20; y["Feature"]["min_fast_threshold"] = 7;
        y["Mapping"]["baseline_dist_thr_ratio"] = 0.02; y["Mapping"]["redundant_obs_ratio_thr"] = 0.9;
        // the start of the map: enough parallax first (a small baseline on a flat table picks the wrong of its two answers)
        double par = 3.0; if (const char* e = std::getenv("DIORAMA_SLAM_PARALLAX")) par = std::atof(e);
        y["Initializer"]["parallax_deg_threshold"] = par; y["Initializer"]["min_num_triangulated_pts"] = 80;
        y["Initializer"]["num_ransac_iterations"] = 300; y["Initializer"]["use_fixed_seed"] = true;
        auto cfg = std::make_shared<stella_vslam::config>(y);
        d_->sys = std::make_unique<stella_vslam::system>(cfg, vocab);
        d_->sys->startup();
        d_->height = height;
        return true;
    } catch (const std::exception& e) { err = e.what(); d_->sys.reset(); return false; }
}

bool Session::feed(const cv::Mat& frame, double t, cv::Matx44d& camFromWorld) {
    if (!d_->sys) return false;
    const auto pose = d_->sys->feed_monocular_frame(frame, t);
    if (!pose) return false;
    d_->started = true;
    cv::Matx44d T; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) T(r, c) = (*pose)(r, c);
    // the camera in the map (for the scale and for walls' fronts)
    const cv::Matx33d R(T(0, 0), T(0, 1), T(0, 2), T(1, 0), T(1, 1), T(1, 2), T(2, 0), T(2, 1), T(2, 2));
    const cv::Vec3d tt(T(0, 3), T(1, 3), T(2, 3));
    d_->camPos = -(R.t() * tt);
    // the planes, now and then, from the map's points
    if (t - d_->lastFit > 0.5) {
        d_->lastFit = t;
        std::vector<std::shared_ptr<stella_vslam::data::landmark>> lms; std::set<std::shared_ptr<stella_vslam::data::landmark>> local;
        d_->sys->get_map_publisher()->get_landmarks(lms, local);
        std::vector<cv::Point3d> pts;
        for (const auto& l : lms) if (l && !l->will_be_erased()) { const auto p = l->get_pos_in_world(); pts.push_back(cv::Point3d(p(0), p(1), p(2))); }
        d_->mapPoints = (int)pts.size();
        d_->pts = pts;
        if (pts.size() > 40) {
            // the tolerance: 1% of the map's size (no scale yet: the map's own units)
            std::vector<double> dd; for (const auto& p : pts) dd.push_back(cv::norm(cv::Vec3d(p) - d_->camPos));
            std::nth_element(dd.begin(), dd.begin() + dd.size() / 2, dd.end());
            const double tol = std::max(1e-4, dd[dd.size() / 2] * 0.04);
            // the planes found in map units, then into metres
            std::vector<Plane> before = d_->planes;
            if (d_->scale > 0) for (auto& b : before) { for (int r = 0; r < 3; ++r) b.toWorld(r, 3) /= d_->scale; }
            if (!d_->upFixed) {   // which way is up: the map's biggest plane, unless it faces the first view head-on (a wall)
                int dummy = 1000000;
                std::vector<Plane> any = fitPlanes(pts, cv::Vec3d(0, 0, 0), tol, std::max(40, (int)pts.size() / 5), dummy, {});
                if (!any.empty()) {
                    cv::Vec3d n(any[0].toWorld(0, 1), any[0].toWorld(1, 1), any[0].toWorld(2, 1));
                    if (n.dot(cv::Vec3d(0, -1, 0)) < 0) n = -n;   // the side the camera's up is on
                    if (std::fabs(n[2]) < 0.85) { d_->up = n; d_->upFixed = true; }
                }
            }
            std::vector<Plane> found = fitPlanes(pts, d_->up, tol, std::max(40, (int)pts.size() / 10), d_->nextId, before);
            if (d_->scale <= 0)   // the scale: the camera's height above the first floor or table
                for (const auto& p : found) if (!p.vertical) {
                    const cv::Vec3d y(p.toWorld(0, 1), p.toWorld(1, 1), p.toWorld(2, 1)), c(p.toWorld(0, 3), p.toWorld(1, 3), p.toWorld(2, 3));
                    const double hgt = std::fabs(y.dot(d_->camPos - c));
                    if (hgt > 1e-6) d_->scale = d_->height / hgt;
                    break;
                }
            const double s = d_->scale > 0 ? d_->scale : 1.0;
            for (auto& p : found) { for (int r = 0; r < 3; ++r) p.toWorld(r, 3) *= s; for (auto& b : p.boundary) b *= (float)s; }
            d_->planes = found;
        }
    }
    const double s = d_->scale > 0 ? d_->scale : 1.0;
    camFromWorld = T;
    for (int r = 0; r < 3; ++r) camFromWorld(r, 3) *= s;
    return true;
}
bool Session::started() const { return d_->started; }
int Session::mapPoints() const { return d_->mapPoints; }
std::vector<cv::Point3d> Session::points() const { return d_->pts; }
const std::vector<Plane>& Session::planes() const { return d_->planes; }
void Session::reset() { if (d_->sys) d_->sys->request_reset(); d_->planes.clear(); d_->scale = 0; d_->started = false; }
#else
struct Session::Impl { std::vector<Plane> planes; };
std::vector<cv::Point3d> Session::points() const { return {}; }
Session::Session() : d_(new Impl) {}
Session::~Session() = default;
bool Session::start(int, int, double, double, double, double, const std::string&, double, std::string& err) { err = "Built without SLAM"; return false; }
bool Session::feed(const cv::Mat&, double, cv::Matx44d&) { return false; }
bool Session::started() const { return false; }
int Session::mapPoints() const { return 0; }
const std::vector<Plane>& Session::planes() const { return d_->planes; }
void Session::reset() {}
#endif

}
