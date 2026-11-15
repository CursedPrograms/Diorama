// surfacetrack: Diorama's webcam surface tracker on a video (or a webcam), one line per frame: where the surface is.
//   surfacetrack <video | camera number> [--fov 60] [--tilt 50] [--height 0.6] [--lock-frame 0] [--show]
// Each line: {"frame": n, "ok": true, "inliers": n, "t": [x, y, z], "R": [9 numbers]}  (surface -> camera, OpenCV's axes)
#include "surfacetrack.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("surfacetrack <video | camera number> [--fov 60] [--tilt 50] [--height 0.6] [--lock-frame 0] [--show]\n"); return 1; }
    SurfaceTrack::Settings s; int lockFrame = 0; bool show = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (a == "--fov") s.fovDeg = next(); else if (a == "--tilt") s.tiltDeg = next(); else if (a == "--height") s.height = next();
        else if (a == "--lock-frame") lockFrame = (int)next(); else if (a == "--show") show = true;
    }
    const std::string src = argv[1];
    cv::VideoCapture cap;
    if (src.size() <= 2 && isdigit((unsigned char)src[0])) cap.open(std::atoi(src.c_str())); else cap.open(src);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", src.c_str()); return 1; }
    SurfaceTrack::Tracker tr(s);
    cv::Mat frame; int n = 0;
    while (cap.read(frame)) {
        SurfaceTrack::Pose p; bool ok = false;
        if (n == lockFrame) { ok = tr.lock(frame); p = SurfaceTrack::Tracker::lockPose(s); }
        else if (tr.locked()) ok = tr.track(frame, p);
        if (tr.locked()) {
            std::printf("{\"frame\": %d, \"ok\": %s, \"inliers\": %d, \"t\": [%.4f, %.4f, %.4f], \"R\": [", n, ok ? "true" : "false", tr.inliers(), p.t[0], p.t[1], p.t[2]);
            for (int k = 0; k < 9; ++k) std::printf("%s%.5f", k ? ", " : "", p.R.val[k]);
            std::printf("]}\n");
        }
        if (show && tr.locked()) {   // the surface's grid, drawn where it was found
            const cv::Matx33d K = tr.K(); const double sc = (double)frame.cols / s.width;
            for (int g = -5; g <= 5; ++g) for (int ax = 0; ax < 2; ++ax) {
                cv::Vec3d A = ax ? cv::Vec3d(g * 0.1, 0, -0.5) : cv::Vec3d(-0.5, 0, g * 0.1), B = ax ? cv::Vec3d(g * 0.1, 0, 0.5) : cv::Vec3d(0.5, 0, g * 0.1);
                cv::Vec3d a = K * (p.R * A + p.t), b = K * (p.R * B + p.t);
                if (a[2] > 0 && b[2] > 0) cv::line(frame, cv::Point2d(a[0] / a[2] * sc, a[1] / a[2] * sc), cv::Point2d(b[0] / b[2] * sc, b[1] / b[2] * sc), ok ? cv::Scalar(90, 240, 60) : cv::Scalar(60, 60, 240), 1, cv::LINE_AA);
            }
            cv::imshow("surfacetrack", frame);
            if (cv::waitKey(1) == 27) break;
        }
        ++n;
    }
    return 0;
}
