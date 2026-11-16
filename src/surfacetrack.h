// Diorama - a surface (a table, a floor) tracked with any camera: no ARCore, no marker. After CursedPrograms/AR-Plane-Detection:
// CLAHE (contrast), ORB features, matching (Lowe's ratio), a RANSAC homography (only what moves as one flat thing),
// then the surface's pose (solvePnP). The JavaScript twin is assets/web/surfacetrack.js: the same steps, line for line.
//
// Lock(): the surface in the middle of the view becomes the reference. How it lies is taken from the camera's tilt
// (a phone's gravity, or a setting for a webcam) and its height above it, so each feature there gets its place on it.
// Track(): every frame, where that surface is now (its pose in the camera, OpenCV's camera axes: x right, y down, z ahead).
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <vector>

namespace SurfaceTrack {

struct Settings {
    double fovDeg = 60;          // the camera's field of view, across
    double tiltDeg = 50;         // how far it looks down at the surface when locking (90: straight down)
    double height = 0.6;         // its height above the surface when locking, metres (facing: its distance from it)
    bool facing = false;         // the surface faces the camera (a wall, a poster, a screen) instead of lying under it
    int width = 320;             // frames are worked on at this width
    int features = 700;
    double ratio = 0.75;         // Lowe's ratio test
    double ransacPx = 3;
    int minInliers = 15;
};

struct Pose { cv::Matx33d R = cv::Matx33d::eye(); cv::Vec3d t; };   // surface -> camera (surface: x right, y up out of it, z towards the viewer)

class Tracker {
public:
    explicit Tracker(const Settings& s = Settings()) : s_(s) {}
    bool lock(const cv::Mat& frame);              // the reference: the surface in the middle of this frame
    bool track(const cv::Mat& frame, Pose& pose);  // false: lost this frame (pose keeps the last)
    bool locked() const { return !refX_.empty(); }
    int inliers() const { return inliers_; }
    cv::Matx33d K() const { return K_; }
    static Pose lockPose(const Settings& s);       // where the surface is at the lock, from the tilt and height
    int preview(const cv::Mat& frame, std::vector<cv::Point2f>* points = nullptr);   // before locking: the features it sees (how many)
    // the plane's map (as ARCore's): its points only ever added, so a place seen before is found again; a new point
    // joins once a second view puts it in the same place on the plane (things standing on it don't). Its boundary: the
    // convex hull of the points, on the plane (x, z), growing as more of it is seen.
    std::vector<cv::Point2f> boundary() const;
    int mapSize() const { return (int)refX_.size(); }
    static bool lockable(const cv::Mat& frame, const Settings& s);   // enough detail in the middle of the view to find a plane there
private:
    void grow(const cv::Mat& gray, const std::vector<cv::KeyPoint>& kps, const cv::Mat& desc, const std::vector<bool>& matched, const Pose& P);
    void prepare(const cv::Mat& frame, cv::Mat& gray);
    bool reference(const cv::Mat& gray, const Pose& P, double margin);   // this frame's features, placed on the surface
    Settings s_;
    cv::Matx33d K_;
    double scale_ = 1;
    cv::Ptr<cv::ORB> orb_;
    cv::Mat refDesc_;
    std::vector<cv::Point2f> refPx_;
    std::vector<cv::Point3f> refX_;               // each reference feature's place on the surface (metres)
    Pose last_;
    bool lastGood_ = false;
    int inliers_ = 0;
    int refCount_ = 0;
    // candidates: points seen once, waiting for a second view
    cv::Mat candDesc_;
    std::vector<cv::Point3f> candX_;
    std::vector<int> candAge_;
    std::vector<int> seen_;                       // each map point: how many times it was matched
    int growTick_ = 0;                            // the map grows every third good frame (the rest just track)
};

}
