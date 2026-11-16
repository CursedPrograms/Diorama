// markertrack: Diorama's native ARToolKit-style tracker on a video (or a webcam): .patt patterns and 3x3 barcodes.
//   markertrack <video | camera number> [--patt file.patt]... [--want patt:0 | barcode:5] [--size 1] [--fov 60] [--camera camera.json]
// Each line: {"frame": n, "ok": true, "t": [x, y, z], "R": [9 numbers], "conf": c}  (marker -> camera, OpenCV's axes)
#include "calibrate.h"
#include "markertrack.h"

#include <opencv2/videoio.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("markertrack <video | camera number> [--patt f.patt]... [--want patt:0 | barcode:5] [--size 1] [--fov 60] [--camera camera.json]\n"); return 1; }
    MarkerTrack::Settings s; std::vector<std::string> patts; bool wantBar = false; int want = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) break;
        if (a == "--patt") patts.push_back(argv[++i]);
        else if (a == "--want") { const std::string v = argv[++i]; wantBar = v.rfind("barcode:", 0) == 0; want = std::atoi(v.substr(v.find(':') + 1).c_str()); }
        else if (a == "--size") s.size = std::atof(argv[++i]);
        else if (a == "--fov") s.fovDeg = std::atof(argv[++i]);
        else if (a == "--camera") {
            std::ifstream f(argv[++i]); std::stringstream ss; ss << f.rdbuf(); Calib::Result r;
            if (Calib::fromJson(ss.str(), r)) { s.K = cv::Matx33d(r.fx, 0, r.cx, 0, r.fy, r.cy, 0, 0, 1); s.dist = r.dist; s.calibWidth = r.width; }
        }
    }
    MarkerTrack::Tracker tr(s);
    for (const auto& p : patts) if (tr.addPatternFile(p) < 0) { std::printf("Couldn't read %s\n", p.c_str()); return 1; }
    const std::string src = argv[1];
    cv::VideoCapture cap;
    if (src.size() <= 2 && isdigit((unsigned char)src[0])) cap.open(std::atoi(src.c_str())); else cap.open(src);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", src.c_str()); return 1; }
    cv::Mat frame; int n = 0;
    while (cap.read(frame)) {
        bool ok = false;
        for (const auto& f : tr.detect(frame)) if (f.id == want && f.barcode == wantBar) {
            std::printf("{\"frame\": %d, \"ok\": true, \"t\": [%.4f, %.4f, %.4f], \"R\": [", n, f.t[0], f.t[1], f.t[2]);
            for (int k = 0; k < 9; ++k) std::printf("%s%.5f", k ? ", " : "", f.R.val[k]);
            std::printf("], \"conf\": %.3f}\n", f.confidence); ok = true; break;
        }
        if (!ok) std::printf("{\"frame\": %d, \"ok\": false, \"t\": [0, 0, 0], \"R\": [1, 0, 0, 0, 1, 0, 0, 0, 1]}\n", n);
        ++n;
    }
    return 0;
}
