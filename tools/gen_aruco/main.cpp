// gen_aruco: writes src/aruco_dicts.h, OpenCV's ArUco dictionaries as bit patterns, so Diorama's editor can draw and print
// ArUco markers without OpenCV. Run it when OpenCV is about (it's built with the surface tracker): gen_aruco <out.h>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("gen_aruco <out.h>\n"); return 1; }
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) return 1;
    std::fprintf(f, "// Diorama - OpenCV's ArUco dictionaries as bit patterns (made by tools/gen_aruco from OpenCV 4.10; do not edit).\n"
                    "// Each marker: size x size cells inside its one-cell black border, row by row, 1 = white.\n#pragma once\n\n"
                    "namespace Aruco {\nstruct Dict { const char* name; int size; int count; const unsigned char* bits; };\n");
    const struct { int id; const char* name; int size; int count; } dicts[] = {
        {cv::aruco::DICT_4X4_50, "4x4_50", 4, 50}, {cv::aruco::DICT_5X5_100, "5x5_100", 5, 100},
        {cv::aruco::DICT_6X6_250, "6x6_250", 6, 250}, {cv::aruco::DICT_ARUCO_ORIGINAL, "original", 5, 1024}};
    for (const auto& d : dicts) {
        const cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(d.id);
        std::fprintf(f, "static const unsigned char BITS_%s[] = {\n", d.name);
        for (int i = 0; i < d.count; ++i) {
            cv::Mat img;
            cv::aruco::generateImageMarker(dict, i, d.size + 2, img, 1);   // one pixel a cell, the border included
            std::fprintf(f, "    ");
            for (int y = 1; y <= d.size; ++y) for (int x = 1; x <= d.size; ++x) std::fprintf(f, "%d,", img.at<unsigned char>(y, x) > 127 ? 1 : 0);
            std::fprintf(f, "\n");
        }
        std::fprintf(f, "};\n");
    }
    std::fprintf(f, "static const Dict DICTS[] = {\n");
    for (const auto& d : dicts) std::fprintf(f, "    {\"%s\", %d, %d, BITS_%s},\n", d.name, d.size, d.count, d.name);
    std::fprintf(f, "};\nstatic const int DICT_COUNT = %d;\n}\n", (int)(sizeof dicts / sizeof dicts[0]));
    std::fclose(f);
    std::printf("wrote %s\n", argv[1]);
    return 0;
}
