#!/bin/bash

# gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
# gst-launch-1.0 -vvv realsensesrc cam-serial-number=918512070217 ! videoconvert ! autovideosink 
# gst-launch-1.0 -v -m realsensesrc stream-type=2 ! videoconvert ! autovideosink
gst-launch-1.0 -v -m realsensesrc stream-type=2 align=2 ! videoconvert ! autovideosink