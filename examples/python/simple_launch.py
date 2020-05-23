#!/usr/bin/env python3

import sys, os
import time

import gi
gi.require_version('Gst', '1.0')
gi.require_version('Gtk', '3.0')
from gi.repository import Gst, GObject, Gtk

import gst_realsense_meta

use_appsink = True

class GTK_Main:
    def __init__(self):
        window = Gtk.Window(Gtk.WindowType.TOPLEVEL)
        window.set_title("Webcam-Viewer")
        window.set_default_size(500, 400)
        window.connect("destroy", self.exit) 
        vbox = Gtk.VBox()
        window.add(vbox)
        self.movie_window = Gtk.DrawingArea()
        vbox.add(self.movie_window)
        hbox = Gtk.HBox()
        vbox.pack_start(hbox, False, False, 0)
        hbox.set_border_width(10)
        hbox.pack_start(Gtk.Label(), False, False, 0)
        self.button = Gtk.Button("Start")
        self.button.connect("clicked", self.start_stop)
        hbox.pack_start(self.button, False, False, 0)
        self.button2 = Gtk.Button("Quit")
        self.button2.connect("clicked", self.exit)
        self.start_time = 0
        self.prev_time = None
        self.frame_count = 0
        hbox.pack_start(self.button2, False, False, 0)
        hbox.add(Gtk.Label())
        window.show_all()

        if use_appsink:
            color_sink = "appsink name=sink emit-signals=true"
        else:
            color_sink = "autovideosink"

        pipeline_str = f'realsensesrc stream-type=2 align=0 imu_on=false ! \
            rsdemux name=demux ! queue ! videoconvert ! {color_sink} \
                demux. ! queue ! videoconvert ! autovideosink'


        # Set up the gstreamer pipeline
        self.player = Gst.parse_launch (pipeline_str)
        bus = self.player.get_bus()
        bus.add_signal_watch()
        bus.enable_sync_message_emission()
        bus.connect("message", self.on_message)
        bus.connect("sync-message::element", self.on_sync_message)
        
        if use_appsink:
            sink_element = self.player.get_by_name("sink")
            sink_element.connect('new-sample', self.on_new_sample)

    def start_stop(self, w):
        if self.button.get_label() == "Start":
            self.button.set_label("Stop")
            self.player.set_state(Gst.State.PLAYING)
            self.start_time = time.time()
        else:
            self.player.set_state(Gst.State.PAUSED)
            self.button.set_label("Start")
            self.start_time = 0

    def exit(self, widget, data=None):
        self.player.set_state(Gst.State.NULL)
        Gtk.main_quit()

    def on_new_sample(self, sink):
        sample = sink.emit('pull-sample')
        buffer = sample.get_buffer()
        t = time.time()
        if buffer:
            # do something with the video buffer, if desired
            units = gst_realsense_meta.get_depth_units(buffer)
            if units:
                print(f'got depth units from buffer: {units}')

        if self.prev_time is None:
            self.prev_time = t
            inst_fr = 0
        else:
            inst_fr = 1.0 / (t - self.prev_time)
            self.prev_time = t
        
        units = gst_realsense_meta.get_depth_units(buffer)
        if units:
            print(f'got depth units from buffer: {units}')

        elapsed = t - self.start_time
        self.frame_count += 1
        mean_fr = self.frame_count / elapsed
        
        print(f'mean frame rate = {mean_fr:04.2f}, instant frame rate = {inst_fr:04.2f}')
        return Gst.FlowReturn.OK

    def on_message(self, bus, message):
        t = message.type
        if t == Gst.MessageType.EOS:
            self.player.set_state(Gst.State.NULL)
            self.button.set_label("Start")
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print (f'Error: {err}, {debug}')
            self.player.set_state(Gst.State.NULL)
            self.button.set_label("Start")

    def on_sync_message(self, bus, message):
        struct = message.get_structure()
        if not struct:
            return
        message_name = struct.get_name()
        if message_name == "prepare-xwindow-id":
            # Assign the viewport
            imagesink = message.src
            imagesink.set_property("force-aspect-ratio", True)
            imagesink.set_xwindow_id(self.movie_window.window.xid)

Gst.init(None)
GTK_Main()
GObject.threads_init()
Gtk.main()
