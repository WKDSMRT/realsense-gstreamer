cmake_minimum_required(VERSION 3.0.1)

# TODO Do gst plugins need to be SHARED or MODULE?
add_library (realsenseplugin SHARED "gstrealsenseplugin.c" "gstrealsenseplugin.h")


