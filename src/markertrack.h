// Diorama - ARToolKit's square markers, natively: the same .patt files and 3x3 barcodes the games find with jsartoolkit5
// (assets/web/lib/artoolkit.min.js), for the C++ player. As ARToolKit does it: the threshold (Otsu), the dark square
// outlines, the inside (the middle half: pattRatio 0.5) sampled 16 x 16 and matched against each pattern's colour
// template in its four turns (mean-subtracted, normalised); barcodes read cell by cell. The pose: solvePnP (IPPE for
// squares). Poses as src/arucotrack.h: marker -> camera in OpenCV's axes; the marker's x right, y up its picture, z out.
#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace MarkerTrack {

struct Settings {
    double fovDeg = 60;                      // without a calibration
    int width = 640;                         // frames are worked on at this width
    double size = 1;                         // the black square, across
    double minConfidence = 0.5;              // ARToolKit's for patterns
    cv::Matx33d K = cv::Matx33d::zeros();    // a calibration for frames calibWidth across (0: the frame's own)
    int calibWidth = 0;
    std::vector<double> dist;
};
struct Found { int id = 0; bool barcode = false; double confidence = 0; cv::Matx33d R; cv::Vec3d t; std::vector<cv::Point2f> corners; };

class Tracker {
public:
    explicit Tracker(const Settings& s = Settings()) : s_(s) {}
    // a .patt file (its text): returns the pattern's id (0, 1, ...), -1 if it isn't one
    int addPattern(const std::string& pattText);
    int addPatternFile(const std::string& path);
    // the markers in this frame: patterns (id = addPattern's) and barcodes 0..63 (barcode = true)
    std::vector<Found> detect(const cv::Mat& frame);
private:
    Settings s_;
    std::vector<cv::Mat> patts_;            // each: 16 x 16 x 3 (B G R as the file has them), the first turn, mean-subtracted, unit length
};

}
