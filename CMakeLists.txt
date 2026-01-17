cmake_minimum_required(VERSION 3.10)
project(DroneGame C)

# Set C Standard to C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Include the 'include' directory so files can find "common.h"
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find necessary libraries (Ncurses and Math)
find_package(Curses REQUIRED)
find_library(MATH_LIB m)

# --- Define Executables ---

# 1. Server (Master Process)
add_executable(server src/server/pro_B.c)
target_link_libraries(server ${CURSES_LIBRARIES} ${MATH_LIB})
# Output to src/server/ to match your manual build structure
set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/server")

# 2. Drone
add_executable(drone src/drone/pro_D.c)
target_link_libraries(drone ${MATH_LIB})
set_target_properties(drone PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/drone")

# 3. Input
add_executable(input src/input/pro_I.c)
set_target_properties(input PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/input")

# 4. Obstacle Generator
add_executable(obstacle src/obstacle/pro_O.c)
set_target_properties(obstacle PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/obstacle")

# 5. Target Generator
add_executable(target src/target/pro_T.c)
set_target_properties(target PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/target")

# 6. Watchdog
add_executable(watchdog src/watchdog/pro_W.c)
target_link_libraries(watchdog ${CURSES_LIBRARIES})
set_target_properties(watchdog PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/watchdog")

# 7. Network Bridge
add_executable(network src/network/network.c)
target_link_libraries(network ${MATH_LIB})
set_target_properties(network PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/network")
