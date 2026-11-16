// Diorama - markers made without a webcam: ARToolKit pattern markers (.patt, like Hiro and Kanji) from any picture or
// letter, and 3x3 barcode markers 0..63; each with a printable picture.
#pragma once
#include <string>
#include <vector>

namespace Markers {

struct Image { int w = 0, h = 0; std::vector<unsigned char> px; };   // RGBA, top row first

// a pattern marker's .patt: the inside picture (what sits in the black square's middle half) at 16 x 16, in its four
// turns, blue/green/red rows, as ARToolKit's own trainer writes it
std::string pattFrom(const Image& inside);
// the printable marker: a white margin, the black square, the picture in its middle half (ratio 0.5), size px across
Image printablePattern(const Image& inside, int size);
// a 3x3 barcode (AR_MATRIX_CODE_3x3, 0..63): black square, its 3x3 cells; size px across, with a white margin
Image printableBarcode(int id, int size);
// an ArUco marker (OpenCV's dictionaries: 0 4x4_50, 1 5x5_100, 2 6x6_250, 3 original): its black border and cells, a white margin
Image printableAruco(int dict, int id, int size);
int arucoCount(int dict);                 // how many markers the dictionary has
// the camera calibration board (OpenCV's ChArUco, as src/calibrate.cpp makes it): 5 x 7 squares, markers 0.7 of a
// square, ArUco 4x4 ids 0..16 in the white squares, a white margin of half a square; px a square
Image printableCharuco(int px);
const char* arucoName(int dict);
// a letter or short word, black on white, as a pattern marker's inside picture
Image letterPicture(const std::string& text, int size, const std::string& fontFile);
// a sheet to print: the markers in a grid, their names under them, A4 at 150 dpi
Image sheet(const std::vector<std::pair<Image, std::string>>& markers, const std::string& fontFile);

bool load(const std::string& path, Image& out);
bool savePng(const std::string& path, const Image& im);
Image resized(const Image& im, int w, int h);

}
