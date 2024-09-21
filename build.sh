#!/bin/bash

# Create build directory if it doesn't exist
mkdir -p build

# Navigate to the build directory
cd build

# Run CMake to configure the project
cmake ..

# Build the project
make

# Move the compiled binary to the root directory (optional)
mv gstreamer_bandwidth_app ../
