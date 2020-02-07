cmake_minimum_required(VERSION 3.10)


SET(CMAKE_C_COMPILER ${CMAKE_CXX_COMPILER})

# TODO Do gst plugins need to be SHARED or MODULE?
add_library (realsenseplugin SHARED "gstrealsenseplugin.c" "gstrealsenseplugin.h")

# RealSense
pkg_check_modules(RSLBIB REQUIRED realsense2)

include_directories(
    ${RSLIB_INCLUDE_DIRS}
)

link_directories(
    ${RSLIB_LIBRARY_DIRS}
)
