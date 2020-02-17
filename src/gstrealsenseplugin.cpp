/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 tim <<user@hostname.org>>
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
 * SECTION:element-realsensesrc
 *
 * FIXME:Describe realsensesrc here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! realsensesrc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstrealsenseplugin.h"

GST_DEBUG_CATEGORY_STATIC (gst_realsense_src_debug);
#define GST_CAT_DEFAULT gst_realsense_src_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
// #include "gst/video/video-format.h"

// TODO update formats
// #define RSS_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) "," \
//   "multiview-mode = { mono, left, right }"                              \
//   ";" \
//   "video/x-bayer, format=(string) { bggr, rggb, grbg, gbrg }, "        \
//   "width = " GST_VIDEO_SIZE_RANGE ", "                                 \
//   "height = " GST_VIDEO_SIZE_RANGE ", "                                \
//   "framerate = " GST_VIDEO_FPS_RANGE ", "                              \
//   "multiview-mode = { mono, left, right }"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_realsense_src_parent_class parent_class
G_DEFINE_TYPE (GstRealsenseSrc, gst_realsense_src, GST_TYPE_ELEMENT);

static void gst_realsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_realsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_realsense_src_fill (GstPushSrc * psrc,
    GstBuffer * buffer);
static gboolean gst_realsense_src_start (GstBaseSrc * basesrc);
static gboolean gst_realsense_src_stop (GstBaseSrc * basesrc);

/* GObject vmethod implementations */

/* initialize the realsensesrc's class */
static void
gst_realsense_src_class_init (GstRealsenseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_realsense_src_set_property;
  gobject_class->get_property = gst_realsense_src_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "RealsenseSrc",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "tim <<user@hostname.org>>");

  // gst_element_class_add_pad_template (gstelement_class,
  //     gst_static_pad_template_get (&src_factory));
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_factory);
  // gstbasesrc_class->set_caps = gst_video_test_src_setcaps;
  // gstbasesrc_class->fixate = gst_video_test_src_src_fixate;
  // gstbasesrc_class->is_seekable = gst_video_test_src_is_seekable;
  // gstbasesrc_class->do_seek = gst_video_test_src_do_seek;
  // gstbasesrc_class->query = gst_video_test_src_query;
  // gstbasesrc_class->get_times = gst_video_test_src_get_times;
  gstbasesrc_class->start = gst_realsense_src_start;
  gstbasesrc_class->stop = gst_realsense_src_stop;
  // gstbasesrc_class->decide_allocation = gst_video_test_src_decide_allocation;

  gstpushsrc_class->fill = gst_realsense_src_fill;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_realsense_src_init (GstRealsenseSrc * filter)
{
  // gst_video_test_src_set_pattern (src, DEFAULT_PATTERN);

  // src->timestamp_offset = DEFAULT_TIMESTAMP_OFFSET;
  // src->foreground_color = DEFAULT_FOREGROUND_COLOR;
  // src->background_color = DEFAULT_BACKGROUND_COLOR;
  // src->horizontal_speed = DEFAULT_HORIZONTAL_SPEED;
  // src->random_state = 0;

  // /* we operate in time */
  // gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  // gst_base_src_set_live (GST_BASE_SRC (src), DEFAULT_IS_LIVE);

  // src->animation_mode = DEFAULT_ANIMATION_MODE;
  // src->motion_type = DEFAULT_MOTION_TYPE;
  // src->flip = DEFAULT_FLIP;

}

static void
gst_realsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *filter = GST_REALSENSESRC (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_realsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *filter = GST_REALSENSESRC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_realsense_src_fill (GstPushSrc * psrc, GstBuffer * buffer)
{
  // GstVideoTestSrc *src;
  // GstClockTime next_time;
  // GstVideoFrame frame;
  // gconstpointer pal;
  // gsize palsize;

  GstRealsenseSrc *src = GST_REALSENSESRC (psrc);



  return GST_FLOW_OK;
}

static gboolean
gst_realsense_src_start (GstBaseSrc * basesrc)
{
  auto *src = GST_REALSENSESRC (basesrc);

  GST_OBJECT_LOCK (src);
  // src->running_time = 0;
  // src->n_frames = 0;
  // src->accum_frames = 0;
  // src->accum_rtime = 0;

  gst_video_info_init (&src->info);
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_realsense_src_stop (GstBaseSrc * basesrc)
{
  auto *src = GST_REALSENSESRC (basesrc);
  guint i;

  // g_free (src->tmpline);
  // src->tmpline = NULL;
  // g_free (src->tmpline2);
  // src->tmpline2 = NULL;
  // g_free (src->tmpline_u8);
  // src->tmpline_u8 = NULL;
  // g_free (src->tmpline_u16);
  // src->tmpline_u16 = NULL;
  // if (src->subsample)
  //   gst_video_chroma_resample_free (src->subsample);
  // src->subsample = NULL;

  // for (i = 0; i < src->n_lines; i++)
  //   g_free (src->lines[i]);
  // g_free (src->lines);
  // src->n_lines = 0;
  // src->lines = NULL;

  return TRUE;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
realsensesrc_init (GstPlugin * realsensesrc)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template realsensesrc' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_realsense_src_debug, "realsensesrc",
      0, "Template realsensesrc");

  return gst_element_register (realsensesrc, "realsensesrc", GST_RANK_NONE,
      GST_TYPE_REALSENSESRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstrealsensesrc"
#endif

/* gstreamer looks for this structure to register realsensesrcs
 *
 * exchange the string 'Template realsensesrc' with your realsensesrc description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    realsensesrc,
    "Realsense Source plugin",
    realsensesrc_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
