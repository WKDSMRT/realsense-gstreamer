cmake_minimum_required(VERSION 3.10)


SET(CMAKE_C_COMPILER ${CMAKE_CXX_COMPILER})

set (libname gstrealsense)

# TODO Do gst plugins need to be SHARED or MODULE?
add_library (${libname} SHARED "gstrealsenseplugin.c" "gstrealsenseplugin.h")

# RealSense
pkg_check_modules(RSLIB REQUIRED realsense2)

include_directories(
    ${RSLIB_INCLUDE_DIRS}
)

#link_directories(     ${RSLIB_LIBRARY_DIRS})

target_link_libraries (${libname} gstreamer-1.0 gstvideo-1.0 ${RSLIB_LIBRARIES})
#  ${GLIB2_LIBRARIES}
#  ${GOBJECT_LIBRARIES}
#  ${GSTREAMER_LIBRARY}
#  ${GSTREAMER_BASE_LIBRARY}
#  ${GSTREAMER_VIDEO_LIBRARY}
#  )