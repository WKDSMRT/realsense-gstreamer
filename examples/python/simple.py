#!/usr/bin/python3

'''
Based on tutorial from
https://brettviren.github.io/pygst-tutorial-org/pygst-tutorial.html
'''

import os

import gi
gi.require_version('Gst', '1.0')
gi.require_version('Gtk', '3.0')
from gi.repository import GObject, Gst, Gtk

STREAM_TYPE = 2
ALIGN = 0
IMU_ON = True

class GTK_Main(object):

    def __init__(self):
        window = Gtk.Window(Gtk.WindowType.TOPLEVEL)
        window.set_title("Gst Realsense")
        window.set_default_size(500, 400)
        window.connect("destroy", Gtk.main_quit, "WM destroy")
        vbox = Gtk.VBox()
        window.add(vbox)
        hbox = Gtk.HBox()
        vbox.pack_start(hbox, False, False, 0)
        self.entry = Gtk.Entry()
        hbox.add(self.entry)
        self.button = Gtk.Button("Start")
        hbox.pack_start(self.button, False, False, 0)
        self.button.connect("clicked", self.start_stop)
        self.movie_window = Gtk.DrawingArea()
        vbox.add(self.movie_window)
        window.show_all()

        self.pipeline = Gst.Pipeline.new('realsense-stream')

        rssrc = Gst.ElementFactory.make('realsensesrc')
        rssrc.set_property('stream-type', STREAM_TYPE)
        rssrc.set_property('align', ALIGN)
        rssrc.set_property('imu_on', IMU_ON)
        
        rsdemux = Gst.ElementFactory.make('rsdemux', 'demux')
        rsdemux.connect('pad-added', self.demuxer_callback)
        vidconvert_color = Gst.ElementFactory.make('videoconvert', 'convert-color')
        vidsink_color = Gst.ElementFactory.make('autovideosink', 'sink-color')
        vidconvert_depth = Gst.ElementFactory.make('videoconvert', 'convert-depth')
        vidsink_depth = Gst.ElementFactory.make('autovideosink', 'sink-depth')
        self.queue_color = Gst.ElementFactory.make('queue', 'queue_color')
        self.queue_depth = Gst.ElementFactory.make('queue', 'queue_depth')
        if IMU_ON:
            self.queue_imu = Gst.ElementFactory.make('queue', 'queue-imu')
            sink_imu = Gst.ElementFactory.make('fakesink', 'sink-imu')

        self.pipeline.add(rssrc)
        self.pipeline.add(rsdemux)
        self.pipeline.add(vidconvert_color)
        self.pipeline.add(vidconvert_depth)
        self.pipeline.add(vidsink_color)
        self.pipeline.add(vidsink_depth)
        self.pipeline.add(self.queue_color)
        self.pipeline.add(self.queue_depth)
        if IMU_ON:
            self.pipeline.add(self.queue_imu)
            self.pipeline.add(sink_imu)

        ret = rssrc.link(rsdemux)
        if not ret:
            print('failed to link source to demux')
        
        ret = self.queue_color.link(vidconvert_color)
        if not ret:
            print('failed to link queue_color to vidconvert')

        ret = vidconvert_color.link(vidsink_color)
        if not ret:
            print('failed to link vidconvert to vidsink')
        
        if IMU_ON:
            self.queue_imu.link(sink_imu)

        ret = self.queue_depth.link(vidconvert_depth)
        if not ret:
            print('failed to link queue_depth to vidconvert')

        ret = vidconvert_depth.link(vidsink_depth)
        if not ret:
            print('failed to link depth vidconvert to vidsink')

        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.enable_sync_message_emission()
        bus.connect("message", self.on_message)
        bus.connect("sync-message::element", self.on_sync_message)

    def demuxer_callback(self, demuxer, pad):
        print(f'pad template: {pad.get_property("template").name_template}')
        if pad.get_property("template").name_template == "color":
            qc_pad = self.queue_color.get_static_pad("sink")
            linked = pad.link(qc_pad)
            if linked != Gst.PadLinkReturn.OK:
                print('failed to link demux to color queue')
        elif pad.get_property("template").name_template == "depth":
            qd_pad = self.queue_depth.get_static_pad("sink")
            linked = pad.link(qd_pad)
            if linked != Gst.PadLinkReturn.OK:
                print('failed to link demux to depth queue')
        elif IMU_ON and pad.get_property("template").name_template == "imu":
            qi_pad = self.queue_imu.get_static_pad("sink")
            linked = pad.link(qi_pad)
            if linked != Gst.PadLinkReturn.OK:
                print('failed to link demux to IMU queue')

    def start_stop(self, w):
        if self.button.get_label() == "Start":
            self.button.set_label("Stop")
            self.pipeline.set_state(Gst.State.PLAYING)
        elif self.button.get_label() == "Stop":
            self.pipeline.set_state(Gst.State.PAUSED)
            self.button.set_label("Start")

    def on_message(self, bus, message):
        t = message.type
        if t == Gst.MessageType.EOS:
            self.pipeline.set_state(Gst.State.NULL)
            self.button.set_label("Start")
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print (f'Error: {err}, {debug}')
            self.pipeline.set_state(Gst.State.NULL)
            self.button.set_label("Start")

    def on_sync_message(self, bus, message):
        if message.get_structure().get_name() == 'prepare-window-handle':
            imagesink = message.src
            imagesink.set_property("force-aspect-ratio", True)
            xid = self.movie_window.get_property('window').get_xid()
            imagesink.set_window_handle(xid)

Gst.init(None)
GTK_Main()
GObject.threads_init()
Gtk.main()
