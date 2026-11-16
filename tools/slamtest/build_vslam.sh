#!/bin/sh
set -e
cd "$(dirname "$0")"
P="$(pwd)/install"
COMMON="-DCMAKE_INSTALL_PREFIX=$P -DCMAKE_PREFIX_PATH=$P -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
echo "== sqlite"
cmake -S sqlite -B sqlite/build -G "Visual Studio 17 2022" -A x64 $COMMON > sqlite.log 2>&1 && cmake --build sqlite/build --config Release --target install >> sqlite.log 2>&1 && echo "   ok"
echo "== stella_vslam"
cmake -S stella_vslam -B stella_vslam/build -G "Visual Studio 17 2022" -A x64 $COMMON \
  -DOpenCV_DIR=D:/cc/_src/opencv-dl/opencv/build -DOpenCV_STATIC=OFF -DEigen3_DIR=$P/share/eigen3/cmake \
  -DSQLite3_INCLUDE_DIR=$P/include -DSQLite3_LIBRARY=$P/lib/sqlite3.lib \
  -DUSE_PANGOLIN_VIEWER=OFF -DUSE_SOCKET_PUBLISHER=OFF -DUSE_STACK_TRACE_LOGGER=OFF -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DUSE_ARUCO_NANO=OFF > vslam.log 2>&1 || { echo "   configure FAILED"; tail -30 vslam.log; exit 1; }
cmake --build stella_vslam/build --config Release --target install -j 8 >> vslam.log 2>&1 && echo "   ok" || { echo "   build FAILED"; grep -E "error" vslam.log | head -40; exit 1; }
