#!/bin/bash

#meson . mbuild
ninja -C mbuild install
gst-inspect-1.0 mbuild/src/libgstrealsensesrc.so