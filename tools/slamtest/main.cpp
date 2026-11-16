// slamtest: stella_vslam (monocular SLAM) on a video: how many frames it tracks, how big its map grows.
//   slamtest <video> <orb_vocab.fbow> [--fov 60] [--every 1]
// Each second: tracked frames, the map's points and keyframes. The camera's pose comes back per frame (null: lost).
#include <stella_vslam/config.h>
#include <stella_vslam/system.h>
#include <stella_vslam/publish/map_publisher.h>
#include <stella_vslam/data/landmark.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <yaml-cpp/yaml.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("slamtest <video> <orb_vocab.fbow> [--fov 60]\n"); return 1; }
    double fov = 60;
    for (int i = 3; i + 1 < argc; ++i) if (std::string(argv[i]) == "--fov") fov = std::atof(argv[++i]);
    cv::VideoCapture cap(argv[1]);
    if (!cap.isOpened()) { std::printf("Couldn't open %s\n", argv[1]); return 1; }
    const int W = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), H = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS); if (!(fps > 1)) fps = 30;
    const double f = (W / 2.0) / std::tan(fov * 3.14159265358979 / 360.0);
    YAML::Node y;
    y["Camera"]["name"] = "webcam"; y["Camera"]["setup"] = "monocular"; y["Camera"]["model"] = "perspective";
    y["Camera"]["fx"] = f; y["Camera"]["fy"] = f; y["Camera"]["cx"] = W / 2.0; y["Camera"]["cy"] = H / 2.0;
    for (const char* k : {"k1", "k2", "p1", "p2", "k3"}) y["Camera"][k] = 0.0;
    y["Camera"]["fps"] = fps; y["Camera"]["cols"] = W; y["Camera"]["rows"] = H; y["Camera"]["color_order"] = "BGR";
    y["Preprocessing"]["min_size"] = 800; y["Preprocessing"]["max_num_keypoints"] = 2000;
    y["Feature"]["name"] = "default ORB feature extraction setting"; y["Feature"]["scale_factor"] = 1.2; y["Feature"]["num_levels"] = 8;
    y["Feature"]["ini_fast_threshold"] = 20; y["Feature"]["min_fast_threshold"] = 7;
    y["Mapping"]["baseline_dist_thr_ratio"] = 0.02; y["Mapping"]["redundant_obs_ratio_thr"] = 0.9;
    auto cfg = std::make_shared<stella_vslam::config>(y);
    stella_vslam::system slam(cfg, argv[2]);
    slam.startup();
    cv::Mat frame; int n = 0, tracked = 0, trackedSec = 0;
    while (cap.read(frame)) {
        const double t = n / fps;
        const auto pose = slam.feed_monocular_frame(frame, t);
        if (pose) { ++tracked; ++trackedSec; }
        ++n;
        if (n % (int)fps == 0) {
            std::vector<std::shared_ptr<stella_vslam::data::landmark>> lms; std::set<std::shared_ptr<stella_vslam::data::landmark>> local;
            const unsigned kf = slam.get_map_publisher()->get_landmarks(lms, local);
            (void)kf;
            std::printf("%5.1f s: tracked %2d of %d frames this second; map %zu points\n", t, trackedSec, (int)fps, lms.size());
            std::fflush(stdout);
            trackedSec = 0;
        }
    }
    std::printf("tracked %d of %d frames (%.0f%%)\n", tracked, n, 100.0 * tracked / std::max(1, n));
    slam.shutdown();
    return 0;
}
