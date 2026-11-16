#!/bin/sh
# Builds stella_vslam's dependencies for Diorama: static, /MT (Diorama's runtime), Release, into ./install
set -e
cd "$(dirname "$0")"
P="$(pwd)/install"
G='-G "Visual Studio 17 2022" -A x64'
COMMON="-DCMAKE_INSTALL_PREFIX=$P -DCMAKE_PREFIX_PATH=$P -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_BUILD_TYPE=Release"
b() { name=$1; shift; echo "== $name"; eval cmake -S $name -B $name/build $G $COMMON "$@" > $name.log 2>&1 && cmake --build $name/build --config Release --target install -j 8 >> $name.log 2>&1 && echo "   ok" || { echo "   FAILED: see $name.log"; tail -20 $name.log; exit 1; }; }
b eigen -DBUILD_TESTING=OFF -DEIGEN_BUILD_DOC=OFF
b yaml-cpp -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_MSVC_SHARED_RT=OFF
b spdlog -DSPDLOG_BUILD_EXAMPLE=OFF
b g2o -DG2O_BUILD_APPS=OFF -DG2O_BUILD_EXAMPLES=OFF -DG2O_USE_OPENGL=OFF -DG2O_USE_CSPARSE=ON -DBUILD_CSPARSE=ON -DG2O_USE_CHOLMOD=OFF -DBUILD_LGPL_SHARED_LIBS=OFF -DG2O_USE_LOGGING=OFF -DEigen3_DIR=$P/share/eigen3/cmake
b FBoW -DOpenCV_DIR=D:/cc/_src/opencv-dl/opencv/build -DBUILD_UTILS=OFF -DBUILD_TESTS=OFF
echo "== all built"
