cmake_minimum_required(VERSION 3.10)
project(VirtualDisplayStream)

set(CMAKE_CXX_STANDARD 17)

# Find required packages
find_package(OpenCV REQUIRED)
find_package(X11 REQUIRED)

# Add executable 
add_executable(virtual_display_stream main.cpp)

# Link libraries
target_link_libraries(virtual_display_stream
    ${OpenCV_LIBS}
    ${X11_LIBRARIES}
)

# Include headers
target_include_directories(virtual_display_stream PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)
