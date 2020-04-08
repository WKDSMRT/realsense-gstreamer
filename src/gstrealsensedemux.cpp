/* GStreamer RealSense is a set of plugins to acquire frames from 
 * Intel RealSense cameras into GStreamer pipeline.
 * Copyright (C) <2020> Tim Connelly/WKD.SMRT <timpconnelly@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* GStreamer realsense demux
 *
 * SECTION:element-rsdemux
 * @title: rsdemux
 *
 * Demuxer element to complement realsensesrc.
 * rsdemux splits muxed Realsense stream into its color and depth components. 
 *
 * ## Example launch line
 * |[
 *  gst-launch-1.0 -vvv -m realsensesrc stream-type=2 align=0 imu_on=True ! rsdemux name=demux \
 *  ! queue ! videoconvert ! autovideosink \
 *  demux. ! queue ! videoconvert ! autovideosink \
 *  demux. ! queue ! fakesink
 * ]| 
 * 
 * This pipeline captures realsense stream, demuxes it to color and depth
 * and renders them to videosinks.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "gstrealsensedemux.h"
#include "gstrealsenseplugin.h"

#include "rsmux.hpp"
#include <stdexcept>

GST_DEBUG_CATEGORY_STATIC (rsdemux_debug);
#define GST_CAT_DEFAULT rsdemux_debug

#define RSS_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) "," \
  "multiview-mode = { mono, left, right }"                              \
  ";" \
  "video/x-bayer, format=(string) { bggr, rggb, grbg, gbrg }, "        \
  "width = " GST_VIDEO_SIZE_RANGE ", "                                 \
  "height = " GST_VIDEO_SIZE_RANGE ", "                                \
  "framerate = " GST_VIDEO_FPS_RANGE ", "                              \
  "multiview-mode = { mono, left, right }"

static GstStaticPadTemplate sink_tmpl = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, RGBA, BGR, BGRA, GRAY16_LE, GRAY16_BE, YVYU }"))
    );

static GstStaticPadTemplate color_src_tmpl = GST_STATIC_PAD_TEMPLATE ("color",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, RGBA, BGR, BGRA, GRAY16_LE, GRAY16_BE, YVYU }"))
    );

static GstStaticPadTemplate depth_src_tmpl = GST_STATIC_PAD_TEMPLATE ("depth",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ GRAY16_LE, GRAY16_BE }"))
    );

static GstStaticPadTemplate imu_src_templ = GST_STATIC_PAD_TEMPLATE ("imu",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (F32) ", "
        "layout = (string) interleaved, "
        "rate = (int) { 32000, 44100, 48000 }, " "channels = (int) {3, 6}")
    );

#define gst_rsdemux_parent_class parent_class
G_DEFINE_TYPE (GstRSDemux, gst_rsdemux, GST_TYPE_ELEMENT);

static void gst_rsdemux_finalize (GObject * object);

/* query functions */
static gboolean gst_rsdemux_src_query (GstPad * pad, GstObject * parent, GstQuery * query);
static gboolean gst_rsdemux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query);

/* event functions */
static gboolean gst_rsdemux_send_event (GstElement * element, GstEvent * event);
static gboolean gst_rsdemux_handle_src_event (GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean gst_rsdemux_handle_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);

/* scheduling functions */
static GstFlowReturn gst_rsdemux_flush (GstRSDemux * rsdemux);
static GstFlowReturn gst_rsdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer);

/* state change functions */
static GstStateChangeReturn gst_rsdemux_change_state (GstElement * element, GstStateChange transition);

static void
gst_rsdemux_class_init (GstRSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rsdemux_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rsdemux_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_rsdemux_send_event);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class, &color_src_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class, &depth_src_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class, &imu_src_templ);

  gst_element_class_set_static_metadata (gstelement_class,
      "RealSense Source Demuxer", "Realsense Demuxer",
      "Separate RealSense muxed stream into components: color, depth, IMU",
      "Tim Connelly/WKD.SMRT <timpconnelly@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (rsdemux_debug, "rsdemux", 0, "RS demuxer element");
}

static void
gst_rsdemux_init (GstRSDemux * rsdemux)
{
  rsdemux->sinkpad = gst_pad_new_from_static_template (&sink_tmpl, "sink");
  /* for push mode, this is the chain function */
  gst_pad_set_chain_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_chain));
  /* handling events (in push mode only) */
  gst_pad_set_event_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_handle_sink_event));
  /* query functions */
  gst_pad_set_query_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_sink_query));

  /* now add the pad */
  gst_element_add_pad (GST_ELEMENT (rsdemux), rsdemux->sinkpad);
  // src pads will be created in the chain function
}

static void
gst_rsdemux_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* reset to default values before starting streaming */
static void
gst_rsdemux_reset (GstRSDemux * rsdemux)
{
  rsdemux->in_height = 0;
  rsdemux->in_width = 0;
  rsdemux->in_stride_bytes = 0;
  rsdemux->header = {};
}

static GstPad *
gst_rsdemux_add_pad (GstRSDemux * rsdemux, GstStaticPadTemplate * templ, GstCaps * caps, std::string&& stream_name)
{
  GstPad *pad;
  GstEvent *event;
  gchar *stream_id;

  pad = gst_pad_new_from_static_template (templ, templ->name_template);

  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_rsdemux_src_query));

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_rsdemux_handle_src_event));
  gst_pad_use_fixed_caps (pad);
  gst_pad_set_active (pad, TRUE);

  stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT_CAST (rsdemux), stream_name.c_str());
  event = gst_event_new_stream_start (stream_id);
  
  gst_pad_push_event (pad, event);
  g_free (stream_id);

  gst_pad_set_caps (pad, caps);

  gst_element_add_pad (GST_ELEMENT (rsdemux), pad);

  return pad;
}

static void
gst_rsdemux_remove_pads (GstRSDemux * rsdemux)
{
  if (rsdemux->colorsrcpad) {
    gst_element_remove_pad (GST_ELEMENT (rsdemux), rsdemux->colorsrcpad);
    rsdemux->colorsrcpad = nullptr;
  }
  if (rsdemux->depthsrcpad) {
    gst_element_remove_pad (GST_ELEMENT (rsdemux), rsdemux->depthsrcpad);
    rsdemux->depthsrcpad = nullptr;
  }
  if (rsdemux->imusrcpad) {
    gst_element_remove_pad (GST_ELEMENT (rsdemux), rsdemux->imusrcpad);
    rsdemux->imusrcpad = nullptr;
  }
}

static gboolean
gst_rsdemux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;

  // TODO Handle any necessary src queries
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_rsdemux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;

  // TODO Handle any sink queries
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

/* takes ownership of the event */
static gboolean
gst_rsdemux_push_event (GstRSDemux * rsdemux, GstEvent * event)
{
  gboolean res = FALSE;

  if (rsdemux->colorsrcpad) {
    gst_event_ref (event);
    res |= gst_pad_push_event (rsdemux->colorsrcpad, event);
  }

  if (rsdemux->depthsrcpad) {
    gst_event_ref (event);
    res |= gst_pad_push_event (rsdemux->depthsrcpad, event);
  }

  if (rsdemux->colorsrcpad==nullptr && rsdemux->depthsrcpad==nullptr) 
  {
    gst_event_unref (event);
    res = TRUE;
  }

  return res;
}

static gboolean
gst_rsdemux_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRSDemux *rsdemux = GST_RSDEMUX (parent);
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* we are not blocking on anything except the push() calls
       * to the peer which will be unblocked by forwarding the
       * event.*/
      res = gst_rsdemux_push_event (rsdemux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_rsdemux_push_event (rsdemux, event);
      break;
    case GST_EVENT_EOS:
      /* flush any pending data, should be nothing left. */
      gst_rsdemux_flush (rsdemux);
      /* forward event */
      res = gst_rsdemux_push_event (rsdemux, event);
      /* and clear the adapter */
      //   gst_adapter_clear (rsdemux->adapter);
      break;
    case GST_EVENT_CAPS:
      gst_event_unref (event);
      break;
    default:
      res = gst_rsdemux_push_event (rsdemux, event);
      break;
  }

  return res;
}

static gboolean
gst_rsdemux_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return res;
}

/* handle an event on the source pad, it's most likely a seek */
static gboolean
gst_rsdemux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res = FALSE;

  const auto rsdemux = GST_RSDEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    // TODO handle src pad events here
    default:
      res = gst_pad_push_event (rsdemux->sinkpad, event);
      break;
  }

  return res;
}

static GstFlowReturn make_new_pads(GstRSDemux* rsdemux, const RSHeader& header)
{
  rsdemux->header = header;

  GST_CAT_DEBUG(rsdemux_debug, "making pad caps");

  auto color_caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, header.color_width,
        "height", G_TYPE_INT, header.color_height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);
  auto depth_caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "GRAY16_LE",
        "width", G_TYPE_INT, header.depth_width,
        "height", G_TYPE_INT, header.depth_height,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);

  bool imu_on = GST_AUDIO_FORMAT_UNKNOWN != static_cast<GstAudioFormat>(header.accel_format);
  if(imu_on)// || G_UNLIKELY(rsdemux->imusrcpad==nullptr))
  {
    GstAudioInfo info;
    constexpr gint imu_rate = GST_AUDIO_DEF_RATE;
    constexpr gint imu_channels = 6; // x,y,z for accel and gyro
    gst_audio_info_init(&info);
    gst_audio_info_set_format(&info, static_cast<GstAudioFormat>(header.accel_format), imu_rate, imu_channels, NULL);

    const auto imu_caps = gst_audio_info_to_caps(&info);
    rsdemux->imusrcpad = gst_rsdemux_add_pad(rsdemux, &imu_src_templ, imu_caps, "imu");
    
    gst_pad_set_caps (rsdemux->imusrcpad, imu_caps);
    gst_caps_unref(imu_caps);
  }
  
  GST_CAT_DEBUG(rsdemux_debug, "made pad caps");

  if (G_UNLIKELY (rsdemux->colorsrcpad == nullptr) || G_UNLIKELY(rsdemux->depthsrcpad==nullptr)) 
  {
    GST_CAT_DEBUG(rsdemux_debug, "adding pads");

    rsdemux->colorsrcpad = gst_rsdemux_add_pad (rsdemux, &color_src_tmpl, color_caps, "color");
    rsdemux->depthsrcpad = gst_rsdemux_add_pad (rsdemux, &depth_src_tmpl, depth_caps, "depth");

    if (rsdemux->colorsrcpad && rsdemux->depthsrcpad && rsdemux->imusrcpad)
      gst_element_no_more_pads (GST_ELEMENT (rsdemux));
  }
  else
  {
    gst_pad_set_caps (rsdemux->colorsrcpad, color_caps);
    gst_pad_set_caps (rsdemux->depthsrcpad, depth_caps);

  }
  gst_caps_unref (color_caps);
  gst_caps_unref (depth_caps);
  
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_rsdemux_demux_video (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GST_DEBUG ("Demuxing video frame");
  
  const auto header = RSMux::GetRSHeader(rsdemux, buffer);

  // see if anything changed 
  if (rsdemux->header != header || 
    G_UNLIKELY (rsdemux->colorsrcpad == nullptr) || 
    G_UNLIKELY (rsdemux->depthsrcpad == nullptr))
  {
    gst_rsdemux_remove_pads(rsdemux);
    make_new_pads(rsdemux, header);
  }
  
  /* takes ownership of buffer here, we just need to modify
   * the metadata. */
  // outbuf = gst_buffer_make_writable (buffer);
  auto [colorbuf, depthbuf, imubuf] = RSMux::demux(buffer, header);

  GST_CAT_DEBUG(rsdemux_debug, "pushing buffers");

  ret = gst_pad_push (rsdemux->colorsrcpad, colorbuf);
  if (ret != GST_FLOW_OK)
    GST_ELEMENT_WARNING(rsdemux, RESOURCE, SETTINGS, ("Pushing to color src gave %d.", ret), (NULL));
  ret = gst_pad_push (rsdemux->depthsrcpad, depthbuf);
  if (ret != GST_FLOW_OK)
    GST_ELEMENT_WARNING(rsdemux, RESOURCE, SETTINGS, ("Pushing to depth src gave %d.", ret), (NULL));
  
  if(imubuf != nullptr && rsdemux->imusrcpad != nullptr)
  {
    ret = gst_pad_push (rsdemux->imusrcpad, imubuf);
    if (ret != GST_FLOW_OK)
      GST_ELEMENT_WARNING(rsdemux, RESOURCE, SETTINGS, ("Pushing to IMU src gave %d.", ret), (NULL));
  }

  return ret;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_frame (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  GstFlowReturn vret, ret;
  
  rsdemux->frame_count++;
  
  GST_CAT_DEBUG(rsdemux_debug, "demuxing frame");

  /* takes ownership of buffer */
  try 
  {
    vret = ret = gst_rsdemux_demux_video (rsdemux, buffer);
    if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
      throw new std::runtime_error("gst_rsdemux_demux_video failed"); //goto done;
    
    
    /* if both are not linked, we stop */
    if (G_UNLIKELY (vret == GST_FLOW_NOT_LINKED)) {
          ret = GST_FLOW_NOT_LINKED;
    }
  }
  catch(const std::exception& e)
  {
    GST_ELEMENT_ERROR (rsdemux, RESOURCE, FAILED, ("gst_rsdemux_demux_frame: %s", e.what()), (NULL));
  }

  return ret;
}

static GstFlowReturn
gst_rsdemux_flush (GstRSDemux * rsdemux)
{
  // TODO What do we need to do in _flush?
  return GST_FLOW_OK;
}

// streaming operation
static GstFlowReturn
gst_rsdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret;
  auto rsdemux = GST_RSDEMUX (parent);
  ret = gst_rsdemux_demux_frame(rsdemux, buffer);

  return ret;
}

static GstStateChangeReturn
gst_rsdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstRSDemux *rsdemux = GST_RSDEMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rsdemux_reset (rsdemux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rsdemux_remove_pads (rsdemux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}
