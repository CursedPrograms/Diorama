// Diorama - ARToolKit's square markers, natively: see markertrack.h.
#include "markertrack.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MarkerTrack {

static const int N = 16;

// mean-subtracted, unit length: matching is then a dot product (ARToolKit's colour template matching)
static cv::Mat normalised(cv::Mat m) {
    m.convertTo(m, CV_32F);
    m -= cv::mean(m.reshape(1))[0];
    const double n = cv::norm(m);
    if (n > 1e-6) m /= n;
    return m;
}

int Tracker::addPattern(const std::string& text) {
    std::istringstream in(text);
    std::vector<int> v; int x;
    while (in >> x) v.push_back(x);
    if (v.size() < (size_t)N * N * 3) return -1;
    // the file: four turns, each blue, green, red planes of 16 x 16; the first turn is the picture as printed
    cv::Mat m(N, N, CV_8UC3);
    for (int ch = 0; ch < 3; ++ch)
        for (int y = 0; y < N; ++y)
            for (int xx = 0; xx < N; ++xx) m.at<cv::Vec3b>(y, xx)[ch] = (uchar)std::clamp(v[(size_t)ch * N * N + y * N + xx], 0, 255);
    patts_.push_back(normalised(m));
    return (int)patts_.size() - 1;
}

int Tracker::addPatternFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f) return -1;
    std::stringstream ss; ss << f.rdbuf();
    return addPattern(ss.str());
}

// the 3x3 barcode (as Diorama prints it, and ARToolKit reads AR_MATRIX_CODE_3x3): black = 1. The corners say which way
// is up: top left and bottom left black, bottom right white; the other six cells are the number, first bit first
static int readBarcode(const cv::Mat& cells /* 3x3 CV_8U, 1 = black */, int& turn) {
    for (int r = 0; r < 4; ++r) {
        // the cells turned r quarters clockwise back: what the printed (r, c) is
        auto at = [&](int row, int col) {
            int y = row, x = col;
            for (int k = 0; k < r; ++k) { const int ny = x, nx = 2 - y; y = ny; x = nx; }
            return cells.at<uchar>(y, x);
        };
        if (at(0, 0) && at(2, 0) && !at(2, 2)) {
            const int order[6][2] = {{0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2}, {2, 1}};
            int id = 0; for (const auto& o : order) id = id * 2 + (at(o[0], o[1]) ? 1 : 0);
            turn = r; return id;
        }
    }
    return -1;
}

std::vector<Found> Tracker::detect(const cv::Mat& frame) {
    std::vector<Found> out;
    if (frame.empty()) return out;
    cv::Mat colour;
    if (frame.channels() == 1) cv::cvtColor(frame, colour, cv::COLOR_GRAY2BGR); else if (frame.channels() == 4) cv::cvtColor(frame, colour, cv::COLOR_BGRA2BGR); else colour = frame;
    const int w = std::min(s_.width, colour.cols), h = (int)std::lround(colour.rows * (double)w / colour.cols);
    const double k = (double)colour.cols / w;
    cv::Mat small; cv::resize(colour, small, cv::Size(w, h), 0, 0, cv::INTER_AREA);
    cv::Mat gray, bin; cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);   // the dark parts white
    std::vector<std::vector<cv::Point>> contours; std::vector<cv::Vec4i> tree;
    cv::findContours(bin, contours, tree, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
    // the lens
    cv::Matx33d K;
    if (s_.K(0, 0) > 0) { const double c = (double)(s_.calibWidth ? s_.calibWidth : colour.cols) / w; K = cv::Matx33d(s_.K(0, 0) / c, 0, s_.K(0, 2) / c, 0, s_.K(1, 1) / c, s_.K(1, 2) / c, 0, 0, 1); }
    else { const double f = (w / 2.0) / std::tan(s_.fovDeg * 3.14159265358979 / 360.0); K = cv::Matx33d(f, 0, w / 2.0, 0, f, h / 2.0, 0, 0, 1); }
    const float hs = (float)(s_.size / 2);
    const std::vector<cv::Point3f> obj = {{-hs, hs, 0}, {hs, hs, 0}, {hs, -hs, 0}, {-hs, -hs, 0}};   // top left, clockwise
    const double minArea = w * h * 0.0015;
    for (size_t i = 0; i < contours.size(); ++i) {
        if (tree[i][3] >= 0) continue;   // outer outlines only: the black square, not its inside
        const double area = cv::contourArea(contours[i]);
        if (area < minArea) continue;
        std::vector<cv::Point> quad;
        cv::approxPolyDP(contours[i], quad, 0.03 * cv::arcLength(contours[i], true), true);
        if (quad.size() != 4 || !cv::isContourConvex(quad)) continue;
        bool edge = false;   // a square the picture's edge cuts off isn't the marker's own shape (ARToolKit skips them too)
        for (const auto& p : quad) if (p.x <= 2 || p.y <= 2 || p.x >= w - 3 || p.y >= h - 3) edge = true;
        if (edge) continue;
        std::vector<cv::Point2f> q(quad.begin(), quad.end());
        // clockwise on the screen (y down)
        const cv::Point2f a = q[1] - q[0], b = q[2] - q[0];
        if (a.x * b.y - a.y * b.x < 0) std::swap(q[1], q[3]);
        cv::cornerSubPix(gray, q, cv::Size(4, 4), cv::Size(-1, -1), cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 0.01));
        // the square, flattened: 4N across, the middle half is the inside
        const int S = 4 * N;
        const std::vector<cv::Point2f> dst = {{0, 0}, {(float)S, 0}, {(float)S, (float)S}, {0, (float)S}};
        cv::Mat flat; cv::warpPerspective(small, flat, cv::getPerspectiveTransform(q, dst), cv::Size(S, S), cv::INTER_LINEAR);
        cv::Mat inside; cv::resize(flat(cv::Rect(S / 4, S / 4, S / 2, S / 2)), inside, cv::Size(N, N), 0, 0, cv::INTER_AREA);
        // which turn of it is up: barcodes first (as ARToolKit's colour-and-matrix mode: a readable barcode is one),
        // by their corners; then patterns by their best match
        int bestId = -1, bestTurn = 0; double best = s_.minConfidence; bool bar = false;
        {
            cv::Mat g3; cv::cvtColor(flat(cv::Rect(S / 4, S / 4, S / 2, S / 2)), g3, cv::COLOR_BGR2GRAY);
            double lo = 255, hi = 0; cv::Scalar mean[3][3], sd[3][3];
            const int cs = (S / 2) / 3;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) {   // each cell's middle: one solid colour on a barcode
                    cv::meanStdDev(g3(cv::Rect(c * cs + cs / 4, r * cs + cs / 4, cs / 2, cs / 2)), mean[r][c], sd[r][c]);
                    lo = std::min(lo, mean[r][c][0]); hi = std::max(hi, mean[r][c][0]);
                }
            bool solid = hi - lo > 60;
            const double mid = (lo + hi) / 2;
            cv::Mat ones(3, 3, CV_8U);
            for (int r = 0; r < 3 && solid; ++r)
                for (int c = 0; c < 3; ++c) {
                    if (sd[r][c][0] > 0.15 * (hi - lo) || std::abs(mean[r][c][0] - mid) < 0.3 * (hi - lo)) { solid = false; break; }
                    ones.at<uchar>(r, c) = mean[r][c][0] < mid ? 1 : 0;
                }
            int turn = 0; const int id = solid ? readBarcode(ones, turn) : -1;
            if (id >= 0) { bestId = id; bestTurn = turn; bar = true; best = 1; }
        }
        cv::Mat turned = inside.clone();
        for (int r = 0; r < 4 && !bar; ++r) {
            const cv::Mat t = normalised(turned.clone());
            for (size_t p = 0; p < patts_.size(); ++p) {
                const double c = t.dot(patts_[p]);
                if (c > best) { best = c; bestId = (int)p; bestTurn = r; }
            }
            cv::rotate(turned, turned, cv::ROTATE_90_COUNTERCLOCKWISE);
        }
        if (bestId < 0) continue;
        // the corners from the marker's own top left: the picture was turned bestTurn quarters anticlockwise to match,
        // so its top left is bestTurn corners on, clockwise
        std::vector<cv::Point2f> c(4);
        for (int j = 0; j < 4; ++j) c[j] = q[(j + (bar ? (4 - bestTurn) % 4 : bestTurn)) % 4];
        cv::Mat rvec, tvec;
        if (!cv::solvePnP(obj, c, cv::Mat(K), s_.dist.empty() ? cv::Mat() : cv::Mat(s_.dist), rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE)) continue;
        cv::Mat Rm; cv::Rodrigues(rvec, Rm);
        Found f; f.id = bestId; f.barcode = bar; f.confidence = best;
        f.R = cv::Matx33d((double*)Rm.clone().ptr()); f.t = cv::Vec3d(tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2));
        for (const auto& p : c) f.corners.push_back(p * (float)k);
        out.push_back(f);
    }
    return out;
}

}
