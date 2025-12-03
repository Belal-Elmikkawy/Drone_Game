#!/bin/bash

# 1. Clean previous builds to avoid conflicts
echo "Cleaning previous build..."
make clean

# 2. Compile the project
echo "Compiling project..."
make all

# 3. Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "---------------------------------------"
    echo "Compilation successful. Starting Server..."
    echo "---------------------------------------"
    
    # Run the server
    ./server
else
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "Error: Compilation failed."
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    exit 1
fi
