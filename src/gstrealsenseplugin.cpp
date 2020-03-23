/*
 * TODO add license
 */

/**
 * SECTION:element-realsensesrc
 *
 * FIXME: Source element for Intel RealSense camera. This source takes the 
 * frame_set from the RealSense SDK and multiplexes it into a single buffer
 * that is pushed out on the source pad. Downstream elements may receive this buffer
 * and demux it themselves (use RSMux::demux) or use the rsdemux element to split
 * the color and depth into separate buffers.
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
#include "gstrealsensedemux.h"

#include "rsmux.hpp"

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
 */
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
  auto cframe = frame_set.get_color_frame();
  auto depth = frame_set.get_depth_frame();

  auto header = RSHeader{
    cframe.get_height(),
    cframe.get_width(),
    src->gst_stride,
    src->color_format,
    depth.get_height(),
    depth.get_width(),
    depth.get_stride_in_bytes(),
    src->depth_format
  };
  
  gst_element_post_message(GST_ELEMENT_CAST(src), 
    gst_message_new_info(GST_OBJECT_CAST(src), NULL, "muxing data into GstBuffer"));

  return RSMux::mux(frame_set, header, src);
}

static GstFlowReturn
gst_realsense_src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (psrc);
  
  GST_LOG_OBJECT (src, "create");

  gst_element_post_message(GST_ELEMENT_CAST(src), 
    gst_message_new_info(GST_OBJECT_CAST(src), NULL, "creating frame buffer"));
  /* wait for next frame to be available */
  try 
  {
    auto frame_set = src->rs_pipeline->wait_for_frames();
    if(src->aligner != nullptr)
      frame_set = src->aligner->process(frame_set);
    
    gst_element_post_message(GST_ELEMENT_CAST(src), 
      gst_message_new_info(GST_OBJECT_CAST(src), NULL, "received frame from realsense"));

    /* create GstBuffer then release */
    *buf = gst_realsense_src_create_buffer_from_frameset(src, frame_set);

    gst_element_post_message(GST_ELEMENT_CAST(src),
      gst_message_new_info(GST_OBJECT_CAST(src), NULL, "setting timestamp."));
    
    const auto clock = gst_element_get_clock (GST_ELEMENT (src));
    const auto clock_time = gst_clock_get_time (clock);
    GST_BUFFER_TIMESTAMP (*buf) =
        GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (src)),
        clock_time);
    GST_BUFFER_OFFSET (*buf) = frame_set.get_frame_number();
    gst_object_unref (clock);

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

  gst_element_post_message(GST_ELEMENT_CAST(src),
    gst_message_new_info(GST_OBJECT_CAST(src), NULL, "create method done"));

  return GST_FLOW_OK;
}

static GstVideoFormat RS_to_Gst_Format(rs2_format fmt)
{
  switch(fmt){
        case RS2_FORMAT_RGB8:
          return GST_VIDEO_FORMAT_RGB;
        case RS2_FORMAT_BGR8:
          return GST_VIDEO_FORMAT_BGR;
        case RS2_FORMAT_RGBA8:
          return GST_VIDEO_FORMAT_RGBA;
        case RS2_FORMAT_BGRA8:
          return GST_VIDEO_FORMAT_BGRA;
        case RS2_FORMAT_Z16:
        case RS2_FORMAT_RAW16:
        case RS2_FORMAT_Y16:
          if (G_BYTE_ORDER == G_LITTLE_ENDIAN) 
          {
            return GST_VIDEO_FORMAT_GRAY16_LE;
          } 
          else if (G_BYTE_ORDER == G_BIG_ENDIAN) 
          {
            return GST_VIDEO_FORMAT_GRAY16_BE;
          }
        case RS2_FORMAT_YUYV:
          // FIXME Not exact format match
          return GST_VIDEO_FORMAT_YVYU;
        default:
          return GST_VIDEO_FORMAT_UNKNOWN;
      }
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
        frame_set = src->aligner->process(frame_set);
      
      int height = 0;
      int width = 0;
      GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;
      if(src->stream_type == StreamType::StreamColor)
      {
        auto cframe = frame_set.get_color_frame();
        height = cframe.get_height();
        width = cframe.get_width();
        src->color_format = RS_to_Gst_Format(cframe.get_profile().format());
        fmt = src->color_format;
        // rs2_frame_metadata_value
        // auto raw_rs_size = vf.get_frame_metadata(RS2_FRAME_METADATA_RAW_FRAME_SIZE);
      }
      else if(src->stream_type == StreamType::StreamDepth)
      {
        auto depth = frame_set.get_depth_frame();
        height = depth.get_height();
        width = depth.get_width();
        src->depth_format = RS_to_Gst_Format(depth.get_profile().format());
        fmt = src->depth_format;
      }
      else if(src->stream_type == StreamType::StreamMux)
      {
        auto depth = frame_set.get_depth_frame();
        auto cframe = frame_set.get_color_frame();

        height = cframe.get_height();
        width = cframe.get_width();
        src->depth_format = RS_to_Gst_Format(depth.get_profile().format());
        src->color_format = RS_to_Gst_Format(cframe.get_profile().format());

        auto depth_height = depth.get_height();
        height += (depth_height * depth.get_stride_in_bytes()) / cframe.get_stride_in_bytes();
        fmt = src->color_format;
      }

      gst_video_info_init(&src->info);
      
      if(fmt ==GST_VIDEO_FORMAT_UNKNOWN)
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Unhandled RealSense format %d", fmt), (NULL));

      gst_video_info_set_format(&src->info, fmt, width, height);

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

  if (!gst_element_register (realsensesrc, "rsdemux", GST_RANK_MARGINAL,
      GST_TYPE_RSDEMUX))
    return FALSE;

  if(!gst_element_register (realsensesrc, "realsensesrc", GST_RANK_PRIMARY,
      GST_TYPE_REALSENSESRC))
    return FALSE;

  return TRUE;
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
