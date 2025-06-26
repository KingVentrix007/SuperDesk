cmake_minimum_required(VERSION 3.14)
project(RemoteQtClient)

set(CMAKE_CXX_STANDARD 17)

# Find Qt
find_package(Qt6 COMPONENTS Widgets REQUIRED)  # Use Qt5 if needed
# If using Qt5, use: find_package(Qt5 COMPONENTS Widgets REQUIRED)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Add executable
add_executable(RemoteQtClient main.cpp)

# Link libraries
target_link_libraries(RemoteQtClient
    Qt6::Widgets             # Use Qt5::Widgets if you're using Qt 5
    ${OpenCV_LIBS}
    ws2_32
)
