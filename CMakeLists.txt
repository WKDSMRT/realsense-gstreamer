cmake_minimum_required(VERSION 3.10)

project("gstrealsensesrc")

find_package(PkgConfig REQUIRED)

# GLIB 
pkg_check_modules(GLIB REQUIRED glib-2.0)
include_directories(
    ${GLIB_INCLUDE_DIRS}
)

link_directories(
    ${GLIB_LIBRARY_DIRS}
)

# GSTREAMER
#TODO Find which version we actually need
set(GSTREAMER_MINIMUM_VERSION 1.14.5)

pkg_check_modules(GST1_TEST gstreamer-1.0)
if ( GST1_TEST_FOUND AND NOT ${GST1_TEST_VERSION} VERSION_LESS ${GSTREAMER_MINIMUM_VERSION} )
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    add_definitions(-DGST_API_VERSION_1=1)
else()
    # TODO else what?
endif()

set(PACKAGE_VERSION ${GST1_TEST_VERSION})

include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER-APP_INCLUDE_DIRS}
)

link_directories(
    ${GSTREAMER_LIBRARY_DIRS}
)
add_subdirectory("src")

