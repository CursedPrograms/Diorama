// arucotrack: Diorama's ArUco tracker on a video (or a webcam), one line per frame for a marker: where it is.
//   arucotrack <video | camera number> [--dict 0] [--id 0] [--size 1] [--fov 60]
// Each line: {"frame": n, "ok": true, "t": [x, y, z], "R": [9 numbers]}  (marker -> camera, OpenCV's axes)
#include "arucotrack.h"

#include "calibrate.h"

#include <opencv2/videoio.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("arucotrack <video | camera number> [--dict 0] [--id 0] [--size 1] [--fov 60] [--camera camera.json]\n"); return 1; }
    ArucoTrack::Settings s; int want = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (a == "--camera" && i + 1 < argc) {   // a camera.json
            std::ifstream f(argv[++i]); std::stringstream ss; ss << f.rdbuf(); Calib::Result r;
            if (Calib::fromJson(ss.str(), r)) { s.K = cv::Matx33d(r.fx, 0, r.cx, 0, r.fy, r.cy, 0, 0, 1); s.dist = r.dist; s.calibWidth = r.width; }
            continue;
        }
        if (a == "--dict") s.dict = (int)next(); else if (a == "--id") want = (int)next(); else if (a == "--size") s.size = next(); else if (a == "--fov") s.fovDeg = next();
    }
    const std::string src = argv[1];
    cv::VideoCapture cap;
    if (src.size() <= 2 && isdigit((unsigned char)src[0])) cap.open(std::atoi(src.c_str())); else cap.open(src);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", src.c_str()); return 1; }
    ArucoTrack::Tracker tr(s);
    cv::Mat frame; int n = 0;
    while (cap.read(frame)) {
        bool ok = false;
        for (const auto& f : tr.detect(frame)) if (f.id == want) {
            std::printf("{\"frame\": %d, \"ok\": true, \"t\": [%.4f, %.4f, %.4f], \"R\": [", n, f.t[0], f.t[1], f.t[2]);
            for (int k = 0; k < 9; ++k) std::printf("%s%.5f", k ? ", " : "", f.R.val[k]);
            std::printf("]}\n"); ok = true; break;
        }
        if (!ok) std::printf("{\"frame\": %d, \"ok\": false, \"t\": [0, 0, 0], \"R\": [1, 0, 0, 0, 1, 0, 0, 0, 1]}\n", n);
        ++n;
    }
    return 0;
}
