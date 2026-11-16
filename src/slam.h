// Diorama - SLAM for the native player: stella_vslam (BSD-2) keeps one map of the room and the camera in it; planes are
// fitted to the map's points (RANSAC), as ARCore finds them: floors and tables face up, walls face sideways (up: the
// first view's up, a webcam has no gravity), each with its outline (the hull of its points). Monocular SLAM has no
// scale: it's set from the camera's height above the first floor or table (the anchor's "Any camera" setting).
#pragma once
#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Slam {

bool available();                                   // built with stella_vslam
std::string findVocabulary(const std::string& exeDir, bool download);   // orb_vocab.fbow ("" none)

struct Plane {
    int id = 0;
    bool vertical = false;
    cv::Matx44d toWorld = cv::Matx44d::eye();      // plane (x right, y up out of it, z) -> world (metres)
    std::vector<cv::Point2f> boundary;             // on the plane (x, z), metres
    int points = 0;
};

class Session {
public:
    Session();
    ~Session();
    // the camera: picture size, lens (pixels); the vocabulary; height: the camera above the first floor (metres)
    bool start(int w, int h, double fx, double fy, double cx, double cy, const std::string& vocab, double height, std::string& err);
    // a frame: true when the camera is tracked; camFromWorld (OpenCV's camera axes, metres)
    bool feed(const cv::Mat& frame, double t, cv::Matx44d& camFromWorld);
    bool started() const;                           // the map has begun (it initialised)
    int mapPoints() const;
    std::vector<cv::Point3d> points() const;       // the map's points (map units), from the last refresh
    const std::vector<Plane>& planes() const;       // refreshed about twice a second
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}
