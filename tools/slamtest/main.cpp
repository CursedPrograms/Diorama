// slamtest: the player's SLAM (src/slam.cpp: stella_vslam + plane fitting) on a video.
//   slamtest <video> <orb_vocab.fbow> [--fov 60] [--truth truth.txt] [--height 0.6]
// Each second: tracked frames, map points, planes. With --truth (per frame: frame R(9, plane->camera) t(3)), how far the
// found floor is from the true one: its tilt (degrees) and the camera's height above it (cm). A plane that "moves in
// parallax" shows here as a tilt or height that wanders while the camera moves.
#include "slam.h"

#include <opencv2/videoio.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("slamtest <video> <orb_vocab.fbow> [--fov 60] [--truth truth.txt] [--height 0.6]\n"); return 1; }
    double fov = 60, height = 0.6; int loops = 1; std::string truthPath;
    for (int i = 3; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fov") fov = std::atof(argv[++i]);
        else if (a == "--truth") truthPath = argv[++i];
        else if (a == "--height") height = std::atof(argv[++i]);
        else if (a == "--loops") loops = std::atoi(argv[++i]);
    }
    std::map<int, std::pair<cv::Vec3d, double>> truth;   // frame -> the floor's normal (camera axes), the camera's height
    if (!truthPath.empty()) {
        std::ifstream f(truthPath); std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line); int fr; double v[12];
            if (!(ss >> fr)) continue;
            for (double& x : v) ss >> x;
            const cv::Vec3d n(v[1], v[4], v[7]), t(v[9], v[10], v[11]);   // column 1 of R: the plane's y (up)
            truth[fr] = {n, std::fabs(n.dot(t))};
        }
    }
    cv::VideoCapture cap(argv[1]);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", argv[1]); return 1; }
    const int W = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), H = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS); if (!(fps > 1)) fps = 30;
    const double f = (W / 2.0) / std::tan(fov * 3.14159265358979 / 360.0);
    Slam::Session slam; std::string err;
    if (!slam.start(W, H, f, f, W / 2.0, H / 2.0, argv[2], height, err)) { std::printf("SLAM: %s\n", err.c_str()); return 1; }
    cv::Mat frame; int n = 0, tracked = 0, trackedSec = 0, compared = 0;
    double tiltSum = 0, tiltMax = 0, hSum = 0, secTilt = 0, secH = 0; int secN = 0;
    const int perLoop = (int)cap.get(cv::CAP_PROP_FRAME_COUNT);
    for (;;) {
        if (!cap.read(frame)) { if (--loops <= 0) break; cap.set(cv::CAP_PROP_POS_FRAMES, 0); if (!cap.read(frame)) break; }
        const double t = n / fps;
        cv::Matx44d T;
        if (slam.feed(frame, t, T)) {
            ++tracked; ++trackedSec;
            const auto it = truth.find(perLoop > 0 ? n % perLoop : n);
            if (it != truth.end() && n % 15 == 0 && slam.points().size() > 30) {   // all the points (they're all on the table): least squares
                const auto P = slam.points(); cv::Mat A((int)P.size(), 3, CV_64F); cv::Vec3d m(0, 0, 0);
                for (const auto& q : P) m += cv::Vec3d(q); m *= 1.0 / P.size();
                for (int i = 0; i < (int)P.size(); ++i) for (int k = 0; k < 3; ++k) A.at<double>(i, k) = cv::Vec3d(P[i])[k] - m[k];
                cv::SVD svd(A); cv::Vec3d nw(svd.vt.at<double>(2, 0), svd.vt.at<double>(2, 1), svd.vt.at<double>(2, 2));
                const cv::Matx33d R(T(0, 0), T(0, 1), T(0, 2), T(1, 0), T(1, 1), T(1, 2), T(2, 0), T(2, 1), T(2, 2));
                const cv::Vec3d nc = R * nw;
                double planeN[3] = {0, 0, 0}; for (const auto& p : slam.planes()) if (!p.vertical) { const cv::Vec3d y(p.toWorld(0, 1), p.toWorld(1, 1), p.toWorld(2, 1)); const cv::Vec3d yc = R * y; planeN[0] = yc[0]; planeN[1] = yc[1]; planeN[2] = yc[2]; break; }
                std::printf("   frame %d: all-points fit off %.1f deg (spread %.4f/%.4f/%.4f), RANSAC plane off %.1f deg\n", n,
                    std::acos(std::min(1.0, std::fabs(nc.dot(it->second.first)))) * 57.2958, svd.w.at<double>(0), svd.w.at<double>(1), svd.w.at<double>(2),
                    std::acos(std::min(1.0, std::fabs(cv::Vec3d(planeN[0], planeN[1], planeN[2]).dot(it->second.first)))) * 57.2958);
            }
            for (const auto& p : slam.planes()) if (!p.vertical && it != truth.end()) {
                const cv::Matx44d M = T * p.toWorld;   // the plane -> the camera
                const cv::Vec3d nn(M(0, 1), M(1, 1), M(2, 1)), c(M(0, 3), M(1, 3), M(2, 3));
                const double tilt = std::acos(std::min(1.0, std::fabs(nn.dot(it->second.first)))) * 180 / 3.14159265358979;
                const double dh = std::fabs(std::fabs(nn.dot(c)) - it->second.second) * 100;
                tiltSum += tilt; tiltMax = std::max(tiltMax, tilt); hSum += dh; ++compared;
                secTilt += tilt; secH += dh; ++secN;
                break;
            }
        }
        ++n;
        if (n % (int)fps == 0) {
            int walls = 0; for (const auto& p : slam.planes()) if (p.vertical) ++walls;
            std::printf("%5.1f s: tracked %2d/%d  map %4d points  %zu planes (%d walls)", t, trackedSec, (int)fps, slam.mapPoints(), slam.planes().size(), walls);
            if (secN) std::printf("  floor tilt %5.1f deg, height off %5.1f cm", secTilt / secN, secH / secN);
            std::printf("\n"); std::fflush(stdout);
            trackedSec = 0; secTilt = secH = 0; secN = 0;
        }
    }
    std::printf("tracked %d of %d frames (%.0f%%)", tracked, n, 100.0 * tracked / std::max(1, n));
    if (compared) std::printf("; floor vs the truth: tilt %.1f deg (worst %.1f), height off %.1f cm", tiltSum / compared, tiltMax, hSum / compared);
    std::printf("\n");
    return 0;
}
