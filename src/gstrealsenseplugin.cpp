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
 * gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
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

enum
{
  PROP_0,
  PROP_CAM_SN,
  PROP_ALIGN,
  PROP_DEPTH_ON
};


/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
// #include "gst/video/video-format.h"

// TODO update formats
#define RSS_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) "," \
  "multiview-mode = { mono, left, right }"                              \
  ";" \
  "video/x-bayer, format=(string) { bggr, rggb, grbg, gbrg }, "        \
  "width = " GST_VIDEO_SIZE_RANGE ", "                                 \
  "height = " GST_VIDEO_SIZE_RANGE ", "                                \
  "framerate = " GST_VIDEO_FPS_RANGE ", "                              \
  "multiview-mode = { mono, left, right }"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, RGBA, BGR, BGRA, GRAY16_LE, GRAY16_BE, YVYU }"))
    );

#define gst_realsense_src_parent_class parent_class
G_DEFINE_TYPE (GstRealsenseSrc, gst_realsense_src, GST_TYPE_PUSH_SRC);

static void gst_realsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_realsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_realsense_src_create (GstPushSrc * src, GstBuffer ** buf);

static gboolean gst_realsense_src_start (GstBaseSrc * basesrc);
static gboolean gst_realsense_src_stop (GstBaseSrc * basesrc);
static GstCaps *gst_realsense_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_realsense_src_set_caps (GstBaseSrc * src, GstCaps * caps);

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

  gst_element_class_set_details_simple(gstelement_class,
    "RealsenseSrc",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "tim <<user@hostname.org>>");

  // gst_element_class_add_pad_template (gstelement_class,
  //     gst_static_pad_template_get (&src_factory));
  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstbasesrc_class->get_caps = gst_realsense_src_get_caps;
  gstbasesrc_class->set_caps = gst_realsense_src_set_caps;
  // gstbasesrc_class->fixate = gst_video_test_src_src_fixate;
  // gstbasesrc_class->is_seekable = gst_video_test_src_is_seekable;
  // gstbasesrc_class->do_seek = gst_video_test_src_do_seek;
  // gstbasesrc_class->query = gst_video_test_src_query;
  // gstbasesrc_class->get_times = gst_video_test_src_get_times;
  gstbasesrc_class->start = gst_realsense_src_start;
  gstbasesrc_class->stop = gst_realsense_src_stop;
  // gstbasesrc_class->decide_allocation = gst_video_test_src_decide_allocation;

  gstpushsrc_class->create = gst_realsense_src_create;

  // Properties
  // see Pattern property in VideoTestSrc for usage of enum propert
  g_object_class_install_property (gobject_class, PROP_ALIGN,
    g_param_spec_int ("align", "Alignment",
        "Alignment between Color and Depth sensors.",
        Align::None, Align::Depth, 0,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  
  g_object_class_install_property (gobject_class, PROP_DEPTH_ON,
    g_param_spec_int ("stream-type", "Enable Depth",
        "Enable streaming of depth data",
        StreamType::StreamColor, StreamType::StreamMux, StreamType::StreamDepth,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (
    gobject_class, 
    PROP_CAM_SN,
    g_param_spec_uint64 ("cam-serial-number", "cam-sn",
          "Camera serial number (as unsigned int)", 
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)
        )
    );
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_realsense_src_init (GstRealsenseSrc * src)
{
  /* set source as live (no preroll) */
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

  /* override default of BYTES to operate in time mode */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
}

static void
gst_realsense_src_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (object);

  switch (prop_id) 
  {
    // TODO properties
    case PROP_CAM_SN:
      src->serial_number = g_value_get_uint64(value);
      GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, ("Received serial number %lu.", src->serial_number), (NULL));
      break;
    case PROP_ALIGN:
      src->align = static_cast<Align>(g_value_get_int(value));
      break;
    case PROP_DEPTH_ON:
      src->stream_type = static_cast<StreamType>(g_value_get_int(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_realsense_src_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (object);
  
  switch (prop_id) {
    case PROP_CAM_SN:
      g_value_set_uint64(value, src->serial_number);
      break;
    case PROP_ALIGN:
      g_value_set_int(value, src->align);
      break;
    case PROP_DEPTH_ON:
      g_value_set_int(value, src->stream_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstBuffer *
gst_realsense_src_create_buffer_from_frameset (GstRealsenseSrc * src, rs2::frameset& frame_set)
{
  GstMapInfo minfo;
  GstBuffer *buf;

  auto cframe = frame_set.get_color_frame();
  auto color_sz = cframe.get_height() * src->gst_stride;
  auto depth = frame_set.get_depth_frame();
  auto depth_sz = depth.get_data_size();
  /* TODO: use allocator or use from pool if that's more efficient or safer*/
  buf = gst_buffer_new_and_alloc (color_sz + depth_sz);

  gst_buffer_map (buf, &minfo, GST_MAP_WRITE);
  GST_LOG_OBJECT (src,
      "GstBuffer size=%lu, gst_stride=%d, frame_num=%llu",
      minfo.size, src->gst_stride, cframe.get_frame_number());
  GST_LOG_OBJECT (src, "Buffer timestamp %f", cframe.get_timestamp());

  // TODO refactor this section into cleaner code
  int rs_stride = 0;
  if(src->stream_type == StreamType::StreamColor || src->stream_type == StreamType::StreamMux) 
  {
    rs_stride = cframe.get_stride_in_bytes();
      /* TODO: use orc_memcpy */
    if (src->gst_stride == rs_stride) 
    {
      memcpy (minfo.data, ((guint8 *) cframe.get_data()), color_sz);
    } 
    else 
    {
      int i;
      GST_LOG_OBJECT (src, "Image strides not identical, copy will be slower.");
      for (i = 0; i < src->height; i++) 
      {
        memcpy (minfo.data + i * src->gst_stride,
            ((guint8 *) cframe.get_data()) +
            i * rs_stride, rs_stride);
      }
    }

    // Just cram the depth data into the buffer. We can write a filter to 
    // separate RGB and Depth, or consuming elements can do this themselves
    if(src->stream_type == StreamType::StreamMux)
    {
      memcpy(minfo.data + color_sz, depth.get_data(), depth_sz);
    }
  }
  else //implied src->stream_type == StreamType::Depth
  {
    rs_stride = depth.get_stride_in_bytes();
      /* TODO: use orc_memcpy */
    if (src->gst_stride == rs_stride) 
    {
      memcpy (minfo.data, ((guint8 *) depth.get_data()), depth_sz);
    } 
    else 
    {
      int i;
      GST_LOG_OBJECT (src, "Image strides not identical, copy will be slower.");
      for (i = 0; i < src->height; i++) 
      {
        memcpy (minfo.data + i * src->gst_stride,
            ((guint8 *) depth.get_data()) +
            i * rs_stride, rs_stride);
      }
    }
  }
  

  gst_buffer_unmap (buf, &minfo);

  return buf;
}

static GstFlowReturn
gst_realsense_src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (psrc);
  
  GST_LOG_OBJECT (src, "create");

  /* wait for next frame to be available */
  try 
  {
    auto frame_set = src->rs_pipeline->wait_for_frames();
    if(src->aligner != nullptr)
      src->aligner->process(frame_set);
    
    const auto clock = gst_element_get_clock (GST_ELEMENT (src));
    const auto clock_time = gst_clock_get_time (clock);
    gst_object_unref (clock);

    /* create GstBuffer then release */
    *buf = gst_realsense_src_create_buffer_from_frameset(src, frame_set);

    GST_BUFFER_TIMESTAMP (*buf) =
        GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (src)),
        clock_time);
    GST_BUFFER_OFFSET (*buf) = frame_set.get_frame_number();
  }
  catch (rs2::error & e)
  {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, 
        ("RealSense error calling %s (%s)", e.get_failed_function().c_str(), e.get_failed_args().c_str()),
        (NULL));
    return GST_FLOW_ERROR;
  }


  // if (src->stop_requested) {
  //   if (*buf != NULL) {
  //     gst_buffer_unref (*buf);
  //     *buf = NULL;
  //   }
  //   return GST_FLOW_FLUSHING;
  // }

  return GST_FLOW_OK;
}

static gboolean
gst_realsense_src_start (GstBaseSrc * basesrc)
{
  auto *src = GST_REALSENSESRC (basesrc);
  // GST_OBJECT_LOCK (src);

  try 
  {
      GST_LOG_OBJECT(src, "Creating RealSense pipeline");
      src->rs_pipeline = std::make_unique<rs2::pipeline>();
      if(src->rs_pipeline == nullptr)
      {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Failed to create RealSense pipeline."), (NULL));
        // gst_element_post_message (src,
        //                   GstMessage * message)
        // GST_ERROR_OBJECT(src, "Failed to create RealSense pipeline");
        return FALSE;
      }
      rs2::config cfg;
      
      rs2::context ctx;
      const auto dev_list = ctx.query_devices();      
      const auto serial_number = std::to_string(src->serial_number);

      if(dev_list.size() == 0)
      {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED, 
        ("No RealSense devices found. Cannot start pipeline."),
        (NULL));
        return FALSE;
      }

      auto val = dev_list.begin();
      for(; val != dev_list.end(); ++val )
      {
        if(0 == serial_number.compare(val.operator*().get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)))
        {
          break;
        }
      }
      
      // it might be good to split up this logic for clarity
      if((val == dev_list.end()) || (src->serial_number == DEFAULT_PROP_CAM_SN))
      {
        GST_ELEMENT_WARNING (src, RESOURCE, FAILED, 
          ("Specified serial number %lu not found. Using first found device.", src->serial_number),
          (NULL));
        cfg.enable_device(dev_list[0].get_info(RS2_CAMERA_INFO_SERIAL_NUMBER));
      } 
      else
      {          
        cfg.enable_device(serial_number);
      }
      
      cfg.enable_all_streams();
      // auto profile = src->rs_pipeline->get_active_profile();
      // auto streams = profile.get_streams();     
      // auto s0 = streams[0].get();
      
      // src->running_time = 0;
      // src->n_frames = 0;
      // src->accum_frames = 0;
      // src->accum_rtime = 0;
      
      switch(src->align)
      {
        case Align::None:
          break;
        case Align::Color:
          src->aligner = std::make_unique<rs2::align>(RS2_STREAM_COLOR);
          break;
        case Align::Depth:
          src->aligner = std::make_unique<rs2::align>(RS2_STREAM_DEPTH);
          break;
        default:
          GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, ("Unknown alignment parameter %d", src->align), (NULL));
      }
      src->rs_pipeline->start(cfg);
      GST_LOG_OBJECT(src, "RealSense pipeline started");

      auto frame_set = src->rs_pipeline->wait_for_frames();
      if(src->aligner != nullptr)
        src->aligner->process(frame_set);
      
      int height = 0;
      int width = 0;
      rs2_format rs_format = RS2_FORMAT_COUNT;
      if(src->stream_type == StreamType::StreamColor)
      {
        auto cframe = frame_set.get_color_frame();
        height = cframe.get_height();
        width = cframe.get_width();
        rs_format = cframe.get_profile().format();
        // rs2_frame_metadata_value
        // auto raw_rs_size = vf.get_frame_metadata(RS2_FRAME_METADATA_RAW_FRAME_SIZE);
      }
      else if(src->stream_type == StreamType::StreamDepth)
      {
        auto depth = frame_set.get_depth_frame();
        height = depth.get_height();
        width = depth.get_width();
        rs_format = depth.get_profile().format();
      }
      else if(src->stream_type == StreamType::StreamMux)
      {
        auto depth = frame_set.get_depth_frame();
        auto cframe = frame_set.get_color_frame();

        height = cframe.get_height();
        width = cframe.get_width();
        rs_format = cframe.get_profile().format();

        auto depth_height = depth.get_height();
        height += (depth_height * depth.get_stride_in_bytes()) / cframe.get_stride_in_bytes();
      }

      gst_video_info_init(&src->info);

      switch(rs_format){
        case RS2_FORMAT_RGB8:
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_RGB, width, height);
          break;
        case RS2_FORMAT_BGR8:
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_BGR, width, height);
          break;
        case RS2_FORMAT_RGBA8:
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_RGBA, width, height);
          break;
        case RS2_FORMAT_BGRA8:
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_BGRA, width, height);
          break;
        case RS2_FORMAT_Z16:
        case RS2_FORMAT_RAW16:
        case RS2_FORMAT_Y16:
          if (G_BYTE_ORDER == G_LITTLE_ENDIAN) 
          {
            gst_video_info_set_format (&src->info, GST_VIDEO_FORMAT_GRAY16_LE, width, height);
          } 
          else if (G_BYTE_ORDER == G_BIG_ENDIAN) 
          {
            gst_video_info_set_format (&src->info, GST_VIDEO_FORMAT_GRAY16_BE, width, height);
          }
          break;
        case RS2_FORMAT_YUYV:
          // FIXME Not exact format match
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_YVYU, width, height);
          break;
        default:
          gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_UNKNOWN, width, height);
          GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Unhandled RealSense format %d", rs_format), (NULL));
          // GST_LOG_OBJECT(src, "Unhandled RealSense format %d", rs_format);
      }
      src->caps = gst_video_info_to_caps (&src->info);
  }
  catch (rs2::error & e)
  {
     GST_ERROR_OBJECT (
             src,
             "Realsense error calling %s (%s)",
             e.get_failed_function().c_str(),
             e.get_failed_args().c_str());
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED, 
        ("RealSense error calling %s (%s)", e.get_failed_function().c_str(), e.get_failed_args().c_str()),
        (NULL));
      return FALSE;
  }

  src->height = src->info.height;
  src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE (&src->info, 0);

  // GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_realsense_src_stop (GstBaseSrc * basesrc)
{
  auto *src = GST_REALSENSESRC (basesrc);
  // guint i;

  if(src->rs_pipeline != nullptr)
    src->rs_pipeline->stop();

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

static GstCaps *
gst_realsense_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (bsrc);
  GstCaps *caps;

  if (src->rs_pipeline == nullptr) {
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
gst_realsense_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (bsrc);
  GstVideoInfo vinfo;
  // GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (src, "The caps being set are %" GST_PTR_FORMAT, caps);

  gst_video_info_from_caps (&vinfo, caps);

  if (GST_VIDEO_INFO_FORMAT (&vinfo) != GST_VIDEO_FORMAT_UNKNOWN) {
    src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 0);
  } else {
    goto unsupported_caps;
  }

  return TRUE;

unsupported_caps:
  GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
  return FALSE;
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
#define PACKAGE "realsensesrc"
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
