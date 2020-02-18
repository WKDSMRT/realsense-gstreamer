#!/bin/bash

#meson . mbuild
ninja -C mbuild
gst-inspect-1.0 mbuild/src/libgstrealsensesrc.so