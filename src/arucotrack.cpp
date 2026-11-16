// Diorama - ArUco markers: see arucotrack.h. Keep in step with assets/web/arucotrack.js.
#include "arucotrack.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace ArucoTrack {

static const int DICTS[] = {cv::aruco::DICT_4X4_50, cv::aruco::DICT_5X5_100, cv::aruco::DICT_6X6_250, cv::aruco::DICT_ARUCO_ORIGINAL};

Tracker::Tracker(const Settings& s)
    : s_(s), det_(cv::aruco::getPredefinedDictionary(DICTS[std::clamp(s.dict, 0, 3)]), cv::aruco::DetectorParameters(), cv::aruco::RefineParameters(10, 3, true)) {}

std::vector<Found> Tracker::detect(const cv::Mat& frame) {
    std::vector<Found> out;
    if (frame.empty()) return out;
    cv::Mat g;
    if (frame.channels() == 3) cv::cvtColor(frame, g, cv::COLOR_BGR2GRAY); else if (frame.channels() == 4) cv::cvtColor(frame, g, cv::COLOR_BGRA2GRAY); else g = frame;
    const int w = std::min(s_.width, g.cols), h = (int)std::lround(g.rows * (double)w / g.cols);
    const double k = (double)g.cols / w;
    cv::Mat gray; cv::resize(g, gray, cv::Size(w, h), 0, 0, cv::INTER_AREA);
    // the lens: from a calibration (for the frame's own size), else from the field of view
    cv::Matx33d K;
    if (s_.K(0, 0) > 0) { const double c = (double)(s_.calibWidth ? s_.calibWidth : g.cols) / w; K = cv::Matx33d(s_.K(0, 0) / c, 0, s_.K(0, 2) / c, 0, s_.K(1, 1) / c, s_.K(1, 2) / c, 0, 0, 1); }
    else { const double f = (w / 2.0) / std::tan(s_.fovDeg * 3.14159265358979 / 360.0); K = cv::Matx33d(f, 0, w / 2.0, 0, f, h / 2.0, 0, 0, 1); }
    std::vector<std::vector<cv::Point2f>> corners, rejected; std::vector<int> ids;
    det_.detectMarkers(gray, corners, ids, rejected);
    const float hs = (float)(s_.size / 2);
    const std::vector<cv::Point3f> obj = {{-hs, hs, 0}, {hs, hs, 0}, {hs, -hs, 0}, {-hs, -hs, 0}};   // OpenCV's corner order
    for (size_t i = 0; i < ids.size(); ++i) {
        cv::Mat rvec, tvec;
        if (!cv::solvePnP(obj, corners[i], cv::Mat(K), s_.dist.empty() ? cv::Mat() : cv::Mat(s_.dist), rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE)) continue;
        cv::Mat Rm; cv::Rodrigues(rvec, Rm);
        Found f; f.id = ids[i]; f.R = cv::Matx33d((double*)Rm.clone().ptr()); f.t = cv::Vec3d(tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2));
        for (const auto& p : corners[i]) f.corners.push_back(p * (float)k);
        out.push_back(f);
    }
    return out;
}

}
