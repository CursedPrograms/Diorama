// Diorama - camera calibration: see calibrate.h. Keep in step with assets/web/calibrate.js.
#include "calibrate.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>
#include <regex>

namespace Calib {

double Result::fovDeg() const { return fx > 0 ? 2 * std::atan(width / 2.0 / fx) * 180 / 3.14159265358979 : 0; }

Calibrator::Calibrator(const Board& b)
    : b_(b), board_(cv::Size(b.cols, b.rows), b.square, b.marker, cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50)), det_(board_) {}

int Calibrator::addView(const cv::Mat& frame, bool* kept) {
    if (kept) *kept = false;
    if (frame.empty()) return 0;
    cv::Mat gray;
    if (frame.channels() == 3) cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY); else if (frame.channels() == 4) cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY); else gray = frame;
    if (size_.width && size_ != gray.size()) { obj_.clear(); img_.clear(); }   // another camera size: start again
    size_ = gray.size();
    std::vector<cv::Point2f> corners; std::vector<int> ids;
    det_.detectBoard(gray, corners, ids);
    const int n = (int)ids.size();
    if (n < 8) return n;   // too few to say much about the lens
    std::vector<cv::Point3f> o; std::vector<cv::Point2f> i;
    board_.matchImagePoints(corners, ids, o, i);
    if (o.size() < 8) return n;
    // a different view from those kept: the board's middle moved by 4% of the picture, or its size changed by 10%
    cv::Point2f c(0, 0); for (const auto& p : i) c += p; c *= 1.f / i.size();
    float spread = 0; for (const auto& p : i) spread += (float)cv::norm(p - c); spread /= i.size();
    const float far = 0.04f * size_.width;
    for (size_t k = 0; k < lastCentre_.size(); k += 2) {
        if (cv::norm(lastCentre_[k] - c) < far && std::abs(lastCentre_[k + 1].x - spread) < 0.1f * spread) return n;
    }
    lastCentre_.push_back(c); lastCentre_.push_back(cv::Point2f(spread, 0));
    obj_.push_back(o); img_.push_back(i);
    if (kept) *kept = true;
    return n;
}

Result Calibrator::calibrate() {
    Result r; r.views = views(); r.width = size_.width; r.height = size_.height;
    if (views() < 4) return r;
    cv::Mat K, dist; std::vector<cv::Mat> rv, tv;
    try {
        r.rms = cv::calibrateCamera(obj_, img_, size_, K, dist, rv, tv, cv::CALIB_FIX_K3);   // k3 only fits noise on webcams and phones
    } catch (const cv::Exception&) { return r; }
    r.fx = K.at<double>(0, 0); r.fy = K.at<double>(1, 1); r.cx = K.at<double>(0, 2); r.cy = K.at<double>(1, 2);
    for (int k = 0; k < (int)dist.total() && k < 5; ++k) r.dist.push_back(dist.at<double>(k));
    r.ok = r.fx > 0 && std::isfinite(r.rms);
    return r;
}

cv::Mat Calibrator::boardImage(int px) const {
    cv::Mat img;
    board_.generateImage(cv::Size((b_.cols + 1) * px, (b_.rows + 1) * px), img, px / 2, 1);
    return img;
}

std::string toJson(const Result& r) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "{\"diorama-camera\": 1, \"width\": %d, \"height\": %d, \"K\": [%.4f, %.4f, %.4f, %.4f], \"dist\": [", r.width, r.height, r.fx, r.fy, r.cx, r.cy);
    std::string s = buf;
    for (size_t k = 0; k < r.dist.size(); ++k) { std::snprintf(buf, sizeof buf, "%s%.6f", k ? ", " : "", r.dist[k]); s += buf; }
    std::snprintf(buf, sizeof buf, "], \"rms\": %.4f, \"views\": %d}", r.rms, r.views);
    return s + buf;
}

bool fromJson(const std::string& text, Result& r) {
    auto num = [&](const char* key, double& v) {
        std::smatch m; if (!std::regex_search(text, m, std::regex(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9.eE+-]+)"))) return false;
        v = std::stod(m[1]); return true;
    };
    auto list = [&](const char* key) {
        std::vector<double> o; std::smatch m;
        if (!std::regex_search(text, m, std::regex(std::string("\"") + key + "\"\\s*:\\s*\\[([^\\]]*)\\]"))) return o;
        const std::string in = m[1]; std::regex nr("-?[0-9.]+(?:[eE][+-]?[0-9]+)?");
        for (auto it = std::sregex_iterator(in.begin(), in.end(), nr); it != std::sregex_iterator(); ++it) o.push_back(std::stod(it->str()));
        return o;
    };
    double w = 0, h = 0, rms = 0, views = 0;
    if (!num("width", w) || !num("height", h)) return false;
    const auto K = list("K"); if (K.size() != 4) return false;
    num("rms", rms); num("views", views);
    r.width = (int)w; r.height = (int)h; r.fx = K[0]; r.fy = K[1]; r.cx = K[2]; r.cy = K[3]; r.dist = list("dist"); r.rms = rms; r.views = (int)views;
    r.ok = r.fx > 0;
    return r.ok;
}

}
