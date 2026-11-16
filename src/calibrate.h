// Diorama - camera calibration with a ChArUco board (a chessboard with ArUco markers in its white squares): the
// camera's own focal length, centre and lens distortion, so markers and surfaces sit exactly where they are.
// The JavaScript twin is assets/web/calibrate.js: the same board, the same steps.
//   1. print the board (Diorama's Project Settings > Camera, or `calibrate --board board.png`)
//   2. show it to the camera from 10-20 different angles: each view with enough corners is kept
//   3. calibrate: K (fx, fy, cx, cy), the distortion (k1 k2 p1 p2 k3) and the error (RMS, pixels)
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <string>
#include <vector>

namespace Calib {

// the board everyone uses: 5 x 7 squares, the markers 0.7 of a square, ArUco 4x4 (ids 0..16)
struct Board { int cols = 5, rows = 7; float square = 1.f, marker = 0.7f; };
struct Result {
    bool ok = false; double rms = 0; int views = 0;
    int width = 0, height = 0;            // the frames' size the numbers are for
    double fx = 0, fy = 0, cx = 0, cy = 0;
    std::vector<double> dist;             // k1 k2 p1 p2 k3
    double fovDeg() const;                // across
};

class Calibrator {
public:
    explicit Calibrator(const Board& b = Board());
    // looks for the board; keeps the view when it has enough corners and differs from the views kept. Returns the
    // corners found (0: no board), `kept` says whether it was kept
    int addView(const cv::Mat& frame, bool* kept = nullptr);
    int views() const { return (int)obj_.size(); }
    Result calibrate();
    cv::Mat boardImage(int pxPerSquare) const;   // to print (white margin of half a square)
private:
    Board b_;
    cv::aruco::CharucoBoard board_;
    cv::aruco::CharucoDetector det_;
    std::vector<std::vector<cv::Point3f>> obj_;
    std::vector<std::vector<cv::Point2f>> img_;
    std::vector<cv::Point2f> lastCentre_;
    cv::Size size_;
};

std::string toJson(const Result& r);
bool fromJson(const std::string& text, Result& r);

}
