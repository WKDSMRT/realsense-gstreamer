/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) YEAR AUTHOR_NAME AUTHOR_EMAIL
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-gstrealsensesrc
 *
 * FIXME:Describe plugin here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! plugin ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include "gstrealsenseplugin.h"

GST_DEBUG_CATEGORY_STATIC (gst_realsensesrc_debug);
#define GST_CAT_DEFAULT gst_realsensesrc_debug

/* prototypes */
static void gst_realsensesrc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_realsensesrc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_realsensesrc_dispose (GObject * object);
static void gst_realsensesrc_finalize (GObject * object);

static gboolean gst_realsensesrc_start (GstBaseSrc * src);
static gboolean gst_realsensesrc_stop (GstBaseSrc * src);
static GstCaps *gst_realsensesrc_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_realsensesrc_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_realsensesrc_unlock (GstBaseSrc * src);
static gboolean gst_realsensesrc_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_realsensesrc_create (GstPushSrc * src, GstBuffer ** buf);

enum
{
  PROP_0,
  PROP_NUM_CAPTURE_BUFFERS,
  PROP_CAM_INDEX,
  PROP_TIMEOUT
};

#define DEFAULT_PROP_NUM_CAPTURE_BUFFERS 3
#define DEFAULT_PROP_CAM_INDEX 0
#define DEFAULT_PROP_TIMEOUT 1000

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 * 
 *  TODO: This is where we'll need to define depth and IMU caps.
 */
static GstStaticPadTemplate src_factory = 
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE 
        ("RGB"))
    );

/* initialize the plugin's class */

G_DEFINE_TYPE (GstRealsenseSrc, gst_realsensesrc, GST_TYPE_PUSH_SRC);

static void
gst_realsensesrc_class_init (GstRealsenseSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_realsensesrc_set_property;
  gobject_class->get_property = gst_realsensesrc_get_property;
  gobject_class->dispose = gst_realsensesrc_dispose;
  gobject_class->finalize = gst_realsensesrc_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "RealSense Video+Depth+IMU Source", "Source/Video",
      "RealSense video source", "Tim Connelly <tconnelly@wkdsmrt.com>");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_realsensesrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_realsensesrc_stop);
  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_realsensesrc_get_caps);
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_realsensesrc_set_caps);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_realsensesrc_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_realsensesrc_unlock_stop);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_realsensesrc_create);

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_NUM_CAPTURE_BUFFERS,
      g_param_spec_uint ("num-capture-buffers", "Number of capture buffers",
          "Number of capture buffers", 1, G_MAXUINT,
          DEFAULT_PROP_NUM_CAPTURE_BUFFERS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CAM_INDEX,
      g_param_spec_uint ("cam-index", "Camera Index", "Camera Index", 0, 7,
          DEFAULT_PROP_CAM_INDEX,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (
    G_OBJECT_CLASS (klass),
    PROP_TIMEOUT, 
    g_param_spec_int (
      "timeout",
      "Timeout (ms)",
      "Timeout in ms (0 to use default)", 
      0, 
      G_MAXINT,
      DEFAULT_PROP_TIMEOUT, 
      (GParamFlags)(G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE)
      //static_cast<GParamFlags>(G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE)
    )
  );

}

static void
gst_realsensesrc_reset (GstRealsenseSrc * src)
{
  src->cam_index = -1;
  // memset (&src->buffer_array, 0, sizeof (src->buffer_array));
  // src->error_string[0] = 0;
  src->last_frame_count = 0;
  src->total_dropped_frames = 0;

  if (src->caps) {
    gst_caps_unref (src->caps);
    src->caps = NULL;
  }
}

static void
gst_realsensesrc_init (GstRealsenseSrc * src)
{
  /* set source as live (no preroll) */
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

  /* override default of BYTES to operate in time mode */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

  /* initialize member variables */
  // src->camera_file = g_strdup (DEFAULT_PROP_CAMERA_FILE);
  src->num_capture_buffers = DEFAULT_PROP_NUM_CAPTURE_BUFFERS;
  src->cam_index = DEFAULT_PROP_CAM_INDEX;
  src->timeout = DEFAULT_PROP_TIMEOUT;

  src->stop_requested = FALSE;
  src->caps = NULL;

  gst_realsensesrc_reset (src);
}

void
gst_realsensesrc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (object);

  switch (property_id) {
    case PROP_NUM_CAPTURE_BUFFERS:
      src->num_capture_buffers = g_value_get_uint (value);
      break;
    case PROP_CAM_INDEX:
      src->cam_index = g_value_get_uint (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_realsensesrc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_REALSENSE_SRC (object));

  GstRealsenseSrc *src = GST_REALSENSE_SRC (object);

  switch (property_id) {
    case PROP_NUM_CAPTURE_BUFFERS:
      g_value_set_uint (value, src->num_capture_buffers);
      break;
    case PROP_CAM_INDEX:
      g_value_set_uint (value, src->cam_index);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, src->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_realsensesrc_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_REALSENSE_SRC (object));
  
  GstRealsenseSrc *src = GST_REALSENSE_SRC (object);

  /* clean up as possible.  may be called multiple times */
  G_OBJECT_CLASS (gst_realsensesrc_parent_class)->dispose (object);
}

void
gst_realsensesrc_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_REALSENSE_SRC (object));

  GstRealsenseSrc *src = GST_REALSENSE_SRC (object);

  /* clean up object here */
  if (src->caps) {
    gst_caps_unref (src->caps);
    src->caps = NULL;
  }

  G_OBJECT_CLASS (gst_realsensesrc_parent_class)->finalize (object);
}

static gboolean
gst_realsensesrc_start (GstBaseSrc * bsrc)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);

  guint32 width, height, bpp, stride;
  GstVideoInfo vinfo;

  GST_DEBUG_OBJECT (src, "start");

  /* Open interface to camera + sensors */
  // src->rs_pipeline = std::make_unique<rs2::pipeline>();

  GST_DEBUG_OBJECT (src, "Camera %d has been opened.\n", src->cam_index);

  /* create caps */
  if (src->caps) {
    gst_caps_unref (src->caps);
    src->caps = NULL;
  }

  gst_video_info_init (&vinfo);

  /* TODO: update caps */
  if (bpp <= 8) {
    gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_GRAY8, width, height);
    src->caps = gst_video_info_to_caps (&vinfo);
  } else if (bpp > 8 && bpp <= 16) {
    GValue val = G_VALUE_INIT;
    GstStructure *s;

    if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
      gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_GRAY16_LE, width,
          height);
    } else if (G_BYTE_ORDER == G_BIG_ENDIAN) {
      gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_GRAY16_BE, width,
          height);
    }
    src->caps = gst_video_info_to_caps (&vinfo);

    /* set bpp, extra info for GRAY16 so elements can scale properly */
    s = gst_caps_get_structure (src->caps, 0);
    g_value_init (&val, G_TYPE_INT);
    g_value_set_int (&val, bpp);
    gst_structure_set_value (s, "bpp", &val);
    g_value_unset (&val);
  } else {
    GST_ELEMENT_ERROR (src, STREAM, WRONG_TYPE,
        ("Unknown or unsupported bit depth (%d).", bpp), (NULL));
    return FALSE;
  }

  src->height = vinfo.height;
  src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 0);
  src->rs_stride = stride;

  // src->rs_pipeline->start();
  /* TODO: check timestamps on buffers vs start time */
  src->acq_start_time =
      gst_clock_get_time (gst_element_get_clock (GST_ELEMENT (src)));

  return TRUE;
}

static gboolean
gst_realsensesrc_stop (GstBaseSrc * bsrc)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);

  // src->rs_pipeline->stop();

  gst_realsensesrc_reset (src);

  return TRUE;
}

static GstCaps *
gst_realsensesrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);
  GstCaps *caps;

  if (src->cam_index == -1) {
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));
  } else {
    caps = gst_caps_copy (src->caps);
  }

  GST_DEBUG_OBJECT (src, "The caps before filtering are %" GST_PTR_FORMAT,
      caps);

  if (filter && caps) {
    GstCaps *tmp = gst_caps_intersect (caps, filter);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (src, "The caps after filtering are %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_realsensesrc_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);
  GstVideoInfo vinfo;
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (src, "The caps being set are %" GST_PTR_FORMAT, caps);

  gst_video_info_from_caps (&vinfo, caps);

  if (GST_VIDEO_INFO_FORMAT (&vinfo) != GST_VIDEO_FORMAT_UNKNOWN) {
    src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 0);
  } else {
    GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_realsensesrc_unlock (GstBaseSrc * bsrc)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);

  GST_LOG_OBJECT (src, "unlock");

  src->stop_requested = TRUE;

  return TRUE;
}

static gboolean
gst_realsensesrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (bsrc);

  GST_LOG_OBJECT (src, "unlock_stop");

  src->stop_requested = FALSE;

  return TRUE;
}

static GstBuffer *
gst_realsensesrc_create_buffer_from_frameset (const GstRealsenseSrc * src)
// , const rs2::frameset& data)
{
  GstMapInfo minfo;
  GstBuffer *buf;

  /* TODO: copy RS frame + data into GstBuffer. 
  Refer to gst_bitflowsrc_create_buffer_from_circ_handle */

  return buf;
}

static GstFlowReturn
gst_realsensesrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstRealsenseSrc *src = GST_REALSENSE_SRC (psrc);
  guint32 dropped_frames;
  GstClock *clock;
  GstClockTime clock_time;

  GST_LOG_OBJECT (src, "create");

  /* wait for next frame to be available */
  // auto data = src->rs_pipeline->wait_for_frames();

  // if (!data) {
  //   GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
  //       ("Failed to acquire frame: %s", gst_realsensesrc_get_error_string (src,
  //               ret)), (NULL));
  //   return GST_FLOW_ERROR;
  // }

  clock = gst_element_get_clock (GST_ELEMENT (src));
  clock_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  /* check for dropped frames and disrupted signal */
  
  /* create GstBuffer then release buffer back to acquisition */
  *buf = gst_realsensesrc_create_buffer_from_frameset (src);//, data);

  
  GST_BUFFER_TIMESTAMP (*buf) =
      GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (src)),
      clock_time);
  // GST_BUFFER_OFFSET (*buf) = circ_handle.FrameCount - 1;

  if (src->stop_requested) {
    if (*buf != NULL) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    return GST_FLOW_FLUSHING;
  }

  return GST_FLOW_OK;
}

gchar *
gst_realsensesrc_get_error_string (GstRealsenseSrc * src)
{
  /* TODO: Get error messages */
  return src->error_string;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{

  GST_DEBUG_CATEGORY_INIT (gst_realsensesrc_debug, GST_PACKAGE_NAME,
      0, PLUGIN_DESCRIPTION);

  return gst_element_register (plugin, GST_PACKAGE_NAME, GST_RANK_NONE,
      GST_TYPE_REALSENSE_SRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gstrealsensesrc"
#endif

/* gstreamer looks for this structure to register plugins
 *
 * exchange the string 'Template plugin' with your plugin description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gstrealsensesrc,
    "RealSense camera source",
    plugin_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
