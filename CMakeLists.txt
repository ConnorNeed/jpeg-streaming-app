# Minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(GStreamerBandwidthApp VERSION 1.0)

# Specify C++ standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Find GStreamer packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0)

add_subdirectory(app)
