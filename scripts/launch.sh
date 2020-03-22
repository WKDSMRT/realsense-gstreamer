#!/bin/bash

GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0/
export GST_PLUGIN_PATH

GST_DEBUG=2

# gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
# gst-launch-1.0 -vvv realsensesrc cam-serial-number=918512070217 ! videoconvert ! autovideosink 
# gst-launch-1.0 -vvv -m realsensesrc stream-type=2 ! videoconvert ! autovideosink
# gst-launch-1.0 -v -m realsensesrc stream-type=2 align=2 ! videoconvert ! autovideosink

gst-launch-1.0 -vvv -m realsensesrc stream-type=2 align=1 ! rsdemux name=demux \
   ! queue ! videoconvert ! autovideosink \
   demux. ! queue ! videoconvert ! autovideosink