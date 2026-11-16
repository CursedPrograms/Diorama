// Diorama - ArUco markers (OpenCV's square markers) found with any camera. The JavaScript twin is assets/web/arucotrack.js:
// the same steps. Each frame: grey, OpenCV's ArUco detector, then each marker's pose (solvePnP, IPPE for squares).
// Poses: marker -> camera in OpenCV's camera axes (x right, y down, z ahead); the marker's axes: x right, y up the marker,
// z out of it; its black square is `size` across.
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <vector>

namespace ArucoTrack {

struct Settings {
    int dict = 0;                // 0 4x4_50, 1 5x5_100, 2 6x6_250, 3 the original
    double fovDeg = 60;          // the camera's field of view, across (without a calibration)
    int width = 640;             // frames are worked on at this width
    double size = 1;             // the marker's black square, across
    cv::Matx33d K = cv::Matx33d::zeros();   // a calibration (for frames calibWidth across; 0: the frame's own); zeros: use fovDeg
    int calibWidth = 0;
    std::vector<double> dist;
};
struct Found { int id = 0; cv::Matx33d R; cv::Vec3d t; std::vector<cv::Point2f> corners; };

class Tracker {
public:
    explicit Tracker(const Settings& s = Settings());
    std::vector<Found> detect(const cv::Mat& frame);
private:
    Settings s_;
    cv::aruco::ArucoDetector det_;
};

}
