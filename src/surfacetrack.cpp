// Diorama - a surface tracked with any camera: see surfacetrack.h. Keep in step with assets/web/surfacetrack.js.
#include "surfacetrack.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace SurfaceTrack {

static const double D2R = 3.14159265358979 / 180;

// the surface at the lock: the camera at height h, looking down by tilt; the surface's origin where the middle of the view
// meets it, its z towards the camera. In three.js's axes first (y up), then turned into OpenCV's camera axes (y down, z ahead).
Pose Tracker::lockPose(const Settings& s) {
    const cv::Matx33d F(1, 0, 0, 0, -1, 0, 0, 0, -1);           // three's camera (y up, z back) -> OpenCV's (y down, z ahead)
    if (s.facing) {   // a wall facing the camera, s.height ahead: its up (y) towards the camera, its z down the wall
        const cv::Matx33d R(1, 0, 0, 0, 0, -1, 0, 1, 0);
        const cv::Vec3d t(0, 0, -s.height);
        Pose p; p.R = F * R; p.t = F * t;
        return p;
    }
    const double th = s.tiltDeg * D2R, h = s.height;
    // the camera in the world: at (0, h, 0), turned down by th about x (three: it looks along -z)
    const cv::Matx33d Rcw(1, 0, 0, 0, std::cos(th), std::sin(th), 0, -std::sin(th), std::cos(th));   // camera -> world (rotation by -th about x)
    const cv::Vec3d camPos(0, h, 0);
    const cv::Vec3d origin(0, 0, -h / std::tan(th));               // where the middle of the view meets the surface
    // surface -> three's camera: R = Rcw^T, t = Rcw^T (origin - camPos)
    const cv::Matx33d Rwc = Rcw.t();
    cv::Matx33d R = Rwc; cv::Vec3d t = Rwc * (origin - camPos);
    Pose p; p.R = F * R; p.t = F * t;
    return p;
}

void Tracker::prepare(const cv::Mat& frame, cv::Mat& gray) {
    cv::Mat g;
    if (frame.channels() == 3) cv::cvtColor(frame, g, cv::COLOR_BGR2GRAY); else if (frame.channels() == 4) cv::cvtColor(frame, g, cv::COLOR_BGRA2GRAY); else g = frame;
    scale_ = (double)s_.width / g.cols;
    cv::resize(g, g, cv::Size(s_.width, (int)std::lround(g.rows * scale_)), 0, 0, cv::INTER_AREA);
    // CLAHE: contrast evened out, so plain, smooth surfaces show more features
    static cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    clahe->apply(g, gray);
    const double f = (gray.cols / 2.0) / std::tan(s_.fovDeg * 0.5 * D2R);
    K_ = cv::Matx33d(f, 0, gray.cols / 2.0, 0, f, gray.rows / 2.0, 0, 0, 1);
    if (!orb_) orb_ = cv::ORB::create(s_.features);
}

// the reference: the features in the middle of this (prepared) frame, each placed on the surface seen at pose P
bool Tracker::reference(const cv::Mat& gray, const Pose& P, double margin) {
    cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8U);
    mask(cv::Rect((int)(gray.cols * margin), (int)(gray.rows * margin), (int)(gray.cols * (1 - 2 * margin)), (int)(gray.rows * (1 - 2 * margin)))).setTo(255);
    std::vector<cv::KeyPoint> kps; cv::Mat desc;
    orb_->detectAndCompute(gray, mask, kps, desc);
    if ((int)kps.size() < s_.minInliers * 2) return false;
    // each feature's place on the surface: its ray from the camera, met with the surface
    const cv::Matx33d Ki = K_.inv();
    const cv::Vec3d n = P.R * cv::Vec3d(0, 1, 0);        // the surface's up, in the camera
    refPx_.clear(); refX_.clear(); seen_.clear(); cv::Mat keep;
    for (int i = 0; i < (int)kps.size(); ++i) {
        const cv::Vec3d d = Ki * cv::Vec3d(kps[i].pt.x, kps[i].pt.y, 1);
        const double den = n.dot(d);
        if (std::fabs(den) < 1e-6) continue;
        const double sd = n.dot(P.t) / den;
        if (sd <= 0) continue;                             // behind the camera: the ray never meets it (looking above the horizon)
        const cv::Vec3d Xc = d * sd, Xs = P.R.t() * (Xc - P.t);
        refPx_.push_back(kps[i].pt);
        refX_.push_back(cv::Point3f((float)Xs[0], 0.0f, (float)Xs[2]));
        keep.push_back(desc.row(i));
        seen_.push_back(1);
    }
    candDesc_ = cv::Mat(); candX_.clear(); candAge_.clear();
    refDesc_ = keep;
    refCount_ = (int)refX_.size();
    return refCount_ >= s_.minInliers * 2;
}

int Tracker::preview(const cv::Mat& frame, std::vector<cv::Point2f>* points) {
    cv::Mat gray; prepare(frame, gray);
    std::vector<cv::KeyPoint> kps; orb_->detect(gray, kps);
    if (points) { points->clear(); for (const auto& k : kps) points->push_back(k.pt * (1.0 / scale_)); }
    return (int)kps.size();
}

// the frame's features the map doesn't have yet: placed on the plane; seen in the same place twice, they join the map
void Tracker::grow(const cv::Mat& gray, const std::vector<cv::KeyPoint>& kps, const cv::Mat& desc, const std::vector<bool>& matched, const Pose& P) {
    (void)gray;
    const cv::Matx33d Ki = K_.inv();
    const cv::Vec3d n = P.R * cv::Vec3d(0, 1, 0);
    cv::Mat newDesc; std::vector<cv::Point3f> newX;
    for (size_t i = 0; i < kps.size(); ++i) {
        if (matched[i]) continue;
        const cv::Vec3d d = Ki * cv::Vec3d(kps[i].pt.x, kps[i].pt.y, 1);
        const double den = n.dot(d);
        if (std::fabs(den) < 1e-6) continue;
        const double sd = n.dot(P.t) / den;
        if (sd <= 0 || sd > 8) continue;                   // behind, or too far to trust
        const cv::Vec3d Xs = P.R.t() * (d * sd - P.t);
        if (std::hypot(Xs[0], Xs[2]) > 4) continue;         // a plane is a few metres at most
        newDesc.push_back(desc.row((int)i)); newX.push_back(cv::Point3f((float)Xs[0], 0.0f, (float)Xs[2]));
    }
    if (newX.empty()) return;
    // against the candidates: the same feature, in the same place on the plane (2 cm, or a hair more far off), joins the map
    std::vector<bool> used(newX.size(), false), promoted(candX_.size(), false);
    if (!candDesc_.empty()) {
        cv::BFMatcher matcher(cv::NORM_HAMMING);
        std::vector<std::vector<cv::DMatch>> knn;
        matcher.knnMatch(newDesc, candDesc_, knn, 2);
        for (const auto& m : knn) {
            if (m.empty() || (m.size() == 2 && m[0].distance >= s_.ratio * m[1].distance) || m[0].distance > 50) continue;
            const cv::Point3f& a = newX[m[0].queryIdx]; const cv::Point3f& c = candX_[m[0].trainIdx];
            const double tol = 0.02 + 0.01 * std::hypot(a.x, a.z);
            if (std::hypot(a.x - c.x, a.z - c.z) > tol || promoted[m[0].trainIdx]) continue;
            refDesc_.push_back(newDesc.row(m[0].queryIdx)); refX_.push_back(cv::Point3f((a.x + c.x) / 2, 0.0f, (a.z + c.z) / 2)); seen_.push_back(2);
            used[m[0].queryIdx] = true; promoted[m[0].trainIdx] = true;
        }
    }
    // the candidates: the promoted and the old (unconfirmed for 20 frames) go; this frame's new ones join them
    cv::Mat keepD; std::vector<cv::Point3f> keepX; std::vector<int> keepA;
    for (size_t i = 0; i < candX_.size(); ++i) if (!promoted[i] && candAge_[i] < 20) { keepD.push_back(candDesc_.row((int)i)); keepX.push_back(candX_[i]); keepA.push_back(candAge_[i] + 1); }
    for (size_t i = 0; i < newX.size(); ++i) if (!used[i] && keepX.size() < 2500) { keepD.push_back(newDesc.row((int)i)); keepX.push_back(newX[i]); keepA.push_back(0); }
    candDesc_ = keepD; candX_ = keepX; candAge_ = keepA;
    // the map's size kept in hand: the points matched least go first
    const size_t cap = 5000;
    if (refX_.size() > cap) {
        std::vector<int> order(refX_.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int x, int y) { return seen_[x] > seen_[y]; });
        order.resize(cap * 9 / 10);
        cv::Mat d2; std::vector<cv::Point3f> x2; std::vector<int> s2;
        for (int i : order) { d2.push_back(refDesc_.row(i)); x2.push_back(refX_[i]); s2.push_back(seen_[i]); }
        refDesc_ = d2; refX_ = x2; seen_ = s2;
    }
    refCount_ = (int)refX_.size();
}

std::vector<cv::Point2f> Tracker::boundary() const {
    std::vector<cv::Point2f> pts, hull;
    for (size_t i = 0; i < refX_.size(); ++i) if (seen_[i] >= 2) pts.push_back(cv::Point2f(refX_[i].x, refX_[i].z));
    if (pts.size() < 3) for (const auto& p : refX_) pts.push_back(cv::Point2f(p.x, p.z));
    if (pts.size() < 3) return hull;
    cv::convexHull(pts, hull);
    return hull;
}

bool Tracker::lockable(const cv::Mat& frame, const Settings& s) {
    Tracker t(s);
    cv::Mat gray; t.prepare(frame, gray);
    cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8U);
    mask(cv::Rect(gray.cols / 4, gray.rows / 4, gray.cols / 2, gray.rows / 2)).setTo(255);
    std::vector<cv::KeyPoint> kps; t.orb_->detect(gray, kps, mask);
    return (int)kps.size() >= s.minInliers * 5;
}

bool Tracker::lock(const cv::Mat& frame) {
    cv::Mat gray; prepare(frame, gray);
    const Pose P = lockPose(s_);
    // features in the middle 70% only: the surface, not the room round it
    if (!reference(gray, P, 0.15)) { refX_.clear(); return false; }
    last_ = P; lastGood_ = true; inliers_ = refCount_;
    return true;
}

bool Tracker::track(const cv::Mat& frame, Pose& pose) {
    pose = last_;
    if (!locked()) return false;
    cv::Mat gray; prepare(frame, gray);
    std::vector<cv::KeyPoint> kps; cv::Mat desc;
    orb_->detectAndCompute(gray, cv::noArray(), kps, desc);
    inliers_ = 0;
    if (desc.empty()) return false;
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(refDesc_, desc, knn, 2);
    std::vector<cv::Point2f> a, b; std::vector<cv::Point3f> X; std::vector<int> mi, fi;
    for (const auto& m : knn) if (m.size() == 2 && m[0].distance < s_.ratio * m[1].distance) {
        const cv::Point3f& p = refX_[m[0].queryIdx];
        a.push_back(cv::Point2f(p.x, p.z)); b.push_back(kps[m[0].trainIdx].pt); X.push_back(p); mi.push_back(m[0].queryIdx); fi.push_back(m[0].trainIdx);
    }
    if ((int)a.size() < s_.minInliers) { lastGood_ = false; return false; }
    // RANSAC: only the matches that move together as one flat surface
    std::vector<unsigned char> in;
    const cv::Mat H = cv::findHomography(a, b, cv::RANSAC, s_.ransacPx, in);
    if (H.empty()) { lastGood_ = false; return false; }
    std::vector<cv::Point2f> pb; std::vector<cv::Point3f> pX;
    std::vector<bool> matched(kps.size(), false);
    for (size_t i = 0; i < in.size(); ++i) if (in[i]) { pb.push_back(b[i]); pX.push_back(X[i]); matched[fi[i]] = true; ++seen_[mi[i]]; }
    inliers_ = (int)pb.size();
    if (inliers_ < s_.minInliers) { lastGood_ = false; return false; }
    // the pose: from the last one when it was good (smooth), else afresh (IPPE: made for flat things)
    cv::Mat rvec, tvec;
    cv::Matx33d R0 = last_.R; cv::Rodrigues(cv::Mat(R0), rvec); tvec = cv::Mat(last_.t).clone();
    const bool ok = lastGood_ ? cv::solvePnP(pX, pb, cv::Mat(K_), cv::noArray(), rvec, tvec, true, cv::SOLVEPNP_ITERATIVE)
                              : cv::solvePnP(pX, pb, cv::Mat(K_), cv::noArray(), rvec, tvec, false, cv::SOLVEPNP_IPPE);
    if (!ok) { lastGood_ = false; return false; }
    cv::Mat Rm; cv::Rodrigues(rvec, Rm);
    pose.R = cv::Matx33d((double*)Rm.clone().ptr()); pose.t = cv::Vec3d(tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2));
    last_ = pose; lastGood_ = true;
    if (inliers_ >= s_.minInliers * 2 && ++growTick_ % 3 == 0) grow(gray, kps, desc, matched, pose);   // well found: the map grows from this view
    return true;
}

}
