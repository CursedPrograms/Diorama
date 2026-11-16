// calibrate: Diorama's camera calibration on a video (or a webcam) of the ChArUco board.
//   calibrate --board board.png [px a square]       writes the board to print
//   calibrate <video | camera number> [out.json]     shows the board from many angles; writes camera.json
#include "calibrate.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("calibrate --board board.png [px]\ncalibrate <video | camera number> [camera.json]\n"); return 1; }
    Calib::Calibrator cal;
    if (std::string(argv[1]) == "--board") {
        if (argc < 3) return 1;
        const int px = argc > 3 ? std::atoi(argv[3]) : 200;
        if (!cv::imwrite(argv[2], cal.boardImage(px))) { std::printf("Couldn't write %s\n", argv[2]); return 1; }
        std::printf("wrote %s\n", argv[2]); return 0;
    }
    const std::string src = argv[1];
    cv::VideoCapture cap;
    if (src.size() <= 2 && isdigit((unsigned char)src[0])) cap.open(std::atoi(src.c_str())); else cap.open(src);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", src.c_str()); return 1; }
    cv::Mat frame; int n = 0;
    const bool live = src.size() <= 2;
    while (cap.read(frame) && (!live || cal.views() < 20)) {
        bool kept = false; const int c = cal.addView(frame, &kept);
        if (kept) std::printf("frame %d: %d corners, view %d kept\n", n, c, cal.views());
        ++n;
    }
    const Calib::Result r = cal.calibrate();
    if (!r.ok) { std::printf("Not enough views of the board (%d; 4 at least, 10-20 is good)\n", r.views); return 1; }
    const std::string json = Calib::toJson(r);
    std::printf("%s\nfield of view %.1f degrees across, error %.3f px\n", json.c_str(), r.fovDeg(), r.rms);
    if (argc > 2) { std::ofstream(argv[2], std::ios::binary) << json; std::printf("wrote %s\n", argv[2]); }
    return 0;
}
