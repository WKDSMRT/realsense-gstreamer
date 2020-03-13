#!/bin/bash

# gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
# gst-launch-1.0 -vvv realsensesrc cam-serial-number=918512070217 ! videoconvert ! autovideosink 
# gst-launch-1.0 -v -m realsensesrc stream-type=2 ! videoconvert ! autovideosink
#gst-launch-1.0 -v -m realsensesrc stream-type=2 align=2 ! videoconvert ! autovideosink

gst-launch-1.0 -v -m realsensesrc stream-type=2 ! rsdemux name=demux \
    ! queue ! videoscale ! video/x-raw,width=640,height=480 !\
		videoconvert ! autovideosink \
    demux. ! queue ! videoscale ! video/x-raw,width=320,height=240 !\
		videoconvert ! autovideosink
#    demux.! queue ! audioconvert ! alsasink demux. ! queue ! dvdec ! xvimagesink