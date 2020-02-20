#!/bin/bash

# gst-launch-1.0 -vvv -m realsensesrc ! videoconvert ! autovideosink
gst-launch-1.0 -v realsensesrc cam_serial_number=918512070217 ! videoconvert ! autovideosink 
# gst-launch-1.0 -v -m videotestsrc ! autovideosink