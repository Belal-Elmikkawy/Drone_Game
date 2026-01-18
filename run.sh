#!/bin/bash

# One Command to Rule Them All: Clean, Build using CMake, and Run

echo "---------------------------------------"
echo " [1/3] Configuring with CMake..."
echo "---------------------------------------"

# Ensure build directory exists
mkdir -p build
cd build

# Generate Makefiles
cmake ..

echo "---------------------------------------"
echo " [2/3] Building Project (Clean Rebuild)..."
echo "---------------------------------------"

# Force Clean and Build as requested
make clean
make

# Check build status
if [ $? -ne 0 ]; then
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "Error: Compilation failed."
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    exit 1
fi

cd ..

echo "---------------------------------------"
echo " [3/3] Starting Server..."
echo "---------------------------------------"

# Run the Master Process
# Note: CMakeLists.txt is configured to output binaries to src/*/ folders
./src/server/server
