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
    double height = 0.6;         // its height above the surface when locking, metres
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
private:
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
};

}
