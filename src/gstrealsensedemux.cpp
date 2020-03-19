/* GStreamer realsense demux
 *
 * SECTION:element-rsdemux
 * @title: rsdemux
 *
 * rsdemux splits muxed Realsense stream into its color and depth components. 
 *
 * ## Example launch line
 * |[
 *   gst-launch-1.0 -vvv -m realsensesrc stream-type=2 ! rsdemux name=demux \
 *     ! queue ! videoconvert ! autovideosink \
 *     demux. ! queue ! videoconvert ! autovideosink
 * ]| 
 * This pipeline captures realsense stream, demuxes it to color and depth
 * and renders them to videosinks.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>

#include "gstrealsensedemux.h"
#include "gstrealsenseplugin.h"

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

#define gst_rsdemux_parent_class parent_class
G_DEFINE_TYPE (GstRSDemux, gst_rsdemux, GST_TYPE_ELEMENT);

static void gst_rsdemux_finalize (GObject * object);

/* query functions */
static gboolean gst_rsdemux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_rsdemux_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

/* convert functions */
static gboolean gst_rsdemux_sink_convert (GstRSDemux * demux,
    GstFormat src_format, gint64 src_value, GstFormat dest_format,
    gint64 * dest_value);
static gboolean gst_rsdemux_src_convert (GstRSDemux * demux, GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat dest_format,
    gint64 * dest_value);

/* event functions */
static gboolean gst_rsdemux_send_event (GstElement * element, GstEvent * event);
static gboolean gst_rsdemux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_rsdemux_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

/* scheduling functions */
static GstFlowReturn gst_rsdemux_flush (GstRSDemux * rsdemux);
static GstFlowReturn gst_rsdemux_flush_buffer (GstRSDemux * rsdemux, GstBuffer* buffer);
static GstFlowReturn gst_rsdemux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

/* state change functions */
static gboolean gst_rsdemux_sink_activate (GstPad * sinkpad,
    GstObject * parent);
static gboolean gst_rsdemux_sink_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static GstStateChangeReturn gst_rsdemux_change_state (GstElement * element,
    GstStateChange transition);

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

  gst_element_class_set_static_metadata (gstelement_class,
      "RealSense Source Demuxer", "FIXME:Demuxer",
      "Separate realsense stream into components: color, depth, IMU",
      "Tim Connelly <user@hostname.org>");

  GST_DEBUG_CATEGORY_INIT (rsdemux_debug, "rsdemux", 0, "RS demuxer element");
}

static void
gst_rsdemux_init (GstRSDemux * rsdemux)
{
  rsdemux->sinkpad = gst_pad_new_from_static_template (&sink_tmpl, "sink");
  /* we can operate in pull and push mode so we install
   * a custom activate function */
  gst_pad_set_activate_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_sink_activate));
  /* the function to activate in push mode */
  gst_pad_set_activatemode_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_sink_activate_mode));
  /* for push mode, this is the chain function */
  gst_pad_set_chain_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_chain));
  /* handling events (in push mode only) */
  gst_pad_set_event_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_handle_sink_event));
  /* query functions */
  gst_pad_set_query_function (rsdemux->sinkpad, GST_DEBUG_FUNCPTR (gst_rsdemux_sink_query));

  /* now add the pad */
  gst_element_add_pad (GST_ELEMENT (rsdemux), rsdemux->sinkpad);
  // src pads will be created in the chain function

//   rsdemux->adapter = gst_adapter_new ();

  /* we need 4 temp buffers for audio decoding which are of a static
   * size and which we can allocate here */
//   for (i = 0; i < 4; i++) {
//     rsdemux->audio_buffers[i] =
//         (gint16 *) g_malloc (DV_AUDIO_MAX_SAMPLES * sizeof (gint16));
//   }
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
  rsdemux->color_height = 0;
  rsdemux->color_width = 0;
  rsdemux->color_stride_bytes = 0;
  rsdemux->depth_height = 0;
  rsdemux->depth_width = 0;
  rsdemux->depth_stride_bytes = 0;
}

static GstPad *
gst_rsdemux_add_pad (GstRSDemux * rsdemux, GstStaticPadTemplate * templ, GstCaps * caps)
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

  stream_id = gst_pad_create_stream_id (pad,
                GST_ELEMENT_CAST (rsdemux),
                templ == &color_src_tmpl ? "color" : "depth");
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
    rsdemux->colorsrcpad = NULL;
  }
  if (rsdemux->depthsrcpad) {
    gst_element_remove_pad (GST_ELEMENT (rsdemux), rsdemux->depthsrcpad);
    rsdemux->depthsrcpad = NULL;
  }
  
#if AUDIO_SRC
  if (rsdemux->audiosrcpad) {
    gst_element_remove_pad (GST_ELEMENT (rsdemux), rsdemux->audiosrcpad);
    rsdemux->audiosrcpad = NULL;
  }
#endif  
}

static gboolean
gst_rsdemux_src_convert (GstRSDemux * rsdemux, GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat dest_format,
    gint64 * dest_value)
{
  gboolean res = TRUE;

  if (dest_format == src_format || src_value == -1) {
    *dest_value = src_value;
    goto done;
  }

//   if (rsdemux->decoder == NULL)
//     goto error;

  GST_INFO_OBJECT (pad,
      "src_value:%" G_GINT64_FORMAT ", src_format:%d, dest_format:%d",
      src_value, src_format, dest_format);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (dest_format) {
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_format) {
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
    default:
      res = FALSE;
  }

done:
  GST_INFO_OBJECT (pad,
      "Result : dest_format:%d, dest_value:%" G_GINT64_FORMAT ", res:%d",
      dest_format, *dest_value, res);
  return res;

  /* ERRORS */
error:
  {
    GST_INFO ("source conversion failed");
    return FALSE;
  }
}

static gboolean
gst_rsdemux_sink_convert (GstRSDemux * rsdemux, GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (rsdemux, "%d -> %d", src_format, dest_format);
  GST_INFO_OBJECT (rsdemux,
      "src_value:%" G_GINT64_FORMAT ", src_format:%d, dest_format:%d",
      src_value, src_format, dest_format);

  if (dest_format == src_format || src_value == -1) {
    *dest_value = src_value;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (dest_format) {
        case GST_FORMAT_TIME:
        {
          // guint64 frame;

          /* get frame number, rounds down so don't combine this
           * line and the next line. */
          // frame = src_value / rsdemux->frame_len;

          // break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_format) {
        case GST_FORMAT_BYTES:
        {
          // break;
        }
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  GST_INFO_OBJECT (rsdemux,
      "Result : dest_format:%d, dest_value:%" G_GINT64_FORMAT ", res:%d",
      dest_format, *dest_value, res);

done:
  return res;

error:
  {
    GST_INFO_OBJECT (rsdemux, "sink conversion failed");
    return FALSE;
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
      /* we are not blocking on anything exect the push() calls
       * to the peer which will be unblocked by forwarding the
       * event.*/
      res = gst_rsdemux_push_event (rsdemux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
    //   gst_adapter_clear (rsdemux->adapter);
      GST_DEBUG ("cleared adapter");
      // gst_segment_init (&rsdemux->byte_segment, GST_FORMAT_BYTES);
      // gst_segment_init (&rsdemux->time_segment, GST_FORMAT_TIME);
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

/* convert a pair of values on the given srcpad */
static gboolean
gst_rsdemux_convert_src_pair (GstRSDemux * rsdemux, GstPad * pad,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  gboolean res;

  GST_INFO ("starting conversion of start");
  /* bring the format to time on srcpad. */
  if (!(res = gst_rsdemux_src_convert (rsdemux, pad,
              src_format, src_start, dst_format, dst_start))) {
    goto done;
  }
  GST_INFO ("Finished conversion of start: %" G_GINT64_FORMAT, *dst_start);

  GST_INFO ("starting conversion of stop");
  /* bring the format to time on srcpad. */
  if (!(res = gst_rsdemux_src_convert (rsdemux, pad,
              src_format, src_stop, dst_format, dst_stop))) {
    /* could not convert seek format to time offset */
    goto done;
  }
  GST_INFO ("Finished conversion of stop: %" G_GINT64_FORMAT, *dst_stop);
done:
  return res;
}

/* convert a pair of values on the sinkpad */
static gboolean
gst_rsdemux_convert_sink_pair (GstRSDemux * rsdemux,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  gboolean res;

  GST_INFO ("starting conversion of start");
  /* bring the format to time on srcpad. */
  if (!(res = gst_rsdemux_sink_convert (rsdemux,
              src_format, src_start, dst_format, dst_start))) {
    goto done;
  }
  GST_INFO ("Finished conversion of start: %" G_GINT64_FORMAT, *dst_start);

  GST_INFO ("starting conversion of stop");
  /* bring the format to time on srcpad. */
  if (!(res = gst_rsdemux_sink_convert (rsdemux,
              src_format, src_stop, dst_format, dst_stop))) {
    /* could not convert seek format to time offset */
    goto done;
  }
  GST_INFO ("Finished conversion of stop: %" G_GINT64_FORMAT, *dst_stop);
done:
  return res;
}

static gboolean
gst_rsdemux_send_event (GstElement * element, GstEvent * event)
{
  GstRSDemux *rsdemux = GST_RSDEMUX (element);
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
  // GstRSDemux *rsdemux;

  // rsdemux = GST_RSDEMUX (parent);

  // switch (GST_EVENT_TYPE (event)) {
  //   default:
  //     res = gst_pad_push_event (rsdemux->sinkpad, event);
  //     break;
  // }

  return res;
  }

RSHeader GetRSHeader(GstRSDemux* src, GstBuffer* buffer)
{

  gst_element_post_message(GST_ELEMENT_CAST(src), 
          gst_message_new_info(GST_OBJECT_CAST(src), NULL, "extracting header"));
#if 1
  RSHeader header;
  RSHeader* in;
  GstMapInfo map;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  in = reinterpret_cast<RSHeader*>(map.data);
  // memcpy(&header, map.data, sizeof(RSHeader));

  header.color_height = in->color_height;
  header.color_width = in->color_width;
  header.color_stride = in->color_stride;
  header.color_format = in->color_format;
  header.depth_height = in->depth_height;
  header.depth_width = in->depth_width;
  header.depth_stride = in->depth_stride;
  header.depth_format = in->depth_format;
  
  gst_buffer_unmap(buffer, &map);
#else
  RSHeader header{
    720,
    1280,
    3840,
    GST_VIDEO_FORMAT_RGB,
    480,
    848,
    1696,
    GST_VIDEO_FORMAT_GRAY16_LE
  };
#endif


  return header;
}

/* takes ownership of buffer FIXME */
static GstFlowReturn
gst_rsdemux_demux_video (GstRSDemux * rsdemux, GstBuffer * buffer)//,guint64 duration)
{
  // return GST_FLOW_OK;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps* caps;
  // GST_DEBUG ("Demuxing video frame");
  
  /* get params */
  /* framerate is already up-to-date */
  
  // auto height = rsdemux->decoder->height;
  // wide = dv_format_wide (rsdemux->decoder);
  const auto header = GetRSHeader(rsdemux, buffer);
  // see if anything changed 
  if (G_UNLIKELY (rsdemux->colorsrcpad == nullptr) || (rsdemux->in_stride_bytes != header.color_stride)) //|| (rsdemux->in_height != header.color_height))
  {
    // gint par_x, par_y;

    // rsdemux->in_height = height;
    rsdemux->in_stride_bytes = header.color_stride;
    
      
    caps = gst_caps_new_simple ("video/x-dv",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "width", G_TYPE_INT, 720,
      "height", G_TYPE_INT, 480,
      "framerate", GST_TYPE_FRACTION, 10, 30,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 4, 3, NULL);

    gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
      gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "making pad caps"));

    // auto color_caps = gst_caps_new_simple("video/x-raw", 
    //   GST_VIDEO_CAPS_MAKE("{ RGB, RGBA, BGR, BGRA, GRAY16_LE, GRAY16_BE, YVYU }"),
    //   NULL
    // );
    //     auto depth_caps = gst_caps_new_simple("video/x-raw", 
    //   GST_VIDEO_CAPS_MAKE("{ GRAY16_LE, GRAY16_BE }"),
    //   NULL
    // );
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

    gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
      gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "made pad caps"));
    if (G_UNLIKELY (rsdemux->colorsrcpad == nullptr) || G_UNLIKELY(rsdemux->depthsrcpad==nullptr)) 
    {
      gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
        gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "adding pads"));

      rsdemux->colorsrcpad = gst_rsdemux_add_pad (rsdemux, &color_src_tmpl, color_caps);
      rsdemux->depthsrcpad = gst_rsdemux_add_pad (rsdemux, &depth_src_tmpl, depth_caps);

      if (rsdemux->colorsrcpad && rsdemux->depthsrcpad)
        gst_element_no_more_pads (GST_ELEMENT (rsdemux));

    }
    else
    {
      gst_pad_set_caps (rsdemux->colorsrcpad, color_caps);
      gst_pad_set_caps (rsdemux->depthsrcpad, depth_caps);
    }
    gst_caps_unref (color_caps);
    gst_caps_unref (depth_caps);
  }
  
  // TODO create colorbuf and depth buf and fill them
  /* takes ownership of buffer here, we just need to modify
   * the metadata. */
  // outbuf = gst_buffer_make_writable (buffer);
  GstMapInfo inmap, cmap, dmap;
  gst_buffer_map(buffer, &inmap, GST_MAP_READ);
  
  gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
        gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "copying color buffer to src pad"));

  auto color_sz = header.color_height * header.color_stride;
  auto colorbuf = gst_buffer_new_and_alloc( color_sz);
  gst_buffer_map(colorbuf, &cmap, GST_MAP_WRITE);
  auto cdata = inmap.data + sizeof(RSHeader);
  memcpy(cmap.data, cdata, color_sz);

  gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
        gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "copying depth buffer to src pad"));

  auto depth_sz = header.depth_height * header.depth_stride;
  auto depthbuf = gst_buffer_new_and_alloc( depth_sz);
  gst_buffer_map(depthbuf, &dmap, GST_MAP_WRITE);
  auto ddata = cdata + color_sz;
  memcpy(dmap.data, ddata, depth_sz);

  // TODO What is duration? some sort of timestamp?
  // GST_BUFFER_DURATION (outbuf) = duration;

  gst_buffer_unmap(buffer, &inmap);
  gst_buffer_unmap(colorbuf, &cmap);
  gst_buffer_unmap(depthbuf, &dmap);
  
  gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
    gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "pushing buffers"));
        
  ret = gst_pad_push (rsdemux->colorsrcpad, colorbuf);
  ret = gst_pad_push (rsdemux->depthsrcpad, depthbuf);
#if PROBABLY_UNUSED
  rsdemux->video_offset++;
#endif
  return ret;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_frame (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  GstFlowReturn vret, ret;
  
  rsdemux->frame_count++;
  
  gst_element_post_message(GST_ELEMENT_CAST(rsdemux), 
            gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "demuxing frame"));
  // GST_DEBUG_OBJECT (rsdemux, "%s", __FUNCTION__);
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
      // goto done;
  }
  catch(const std::exception& e)
  {
    GST_ELEMENT_ERROR (rsdemux, RESOURCE, FAILED, ("gst_rsdemux_demux_frame: %s", e.what()), (NULL));
  }
// done:
  return ret;

  /* ERRORS */
// segment_error:
//   {
//     GST_DEBUG ("error generating new_segment event");
//     gst_buffer_unref (buffer);
//     return GST_FLOW_ERROR;
//   }
}

/* flush any remaining data in the adapter, used in chain based scheduling mode */
static GstFlowReturn
gst_rsdemux_flush_buffer (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  // while (gst_adapter_available (rsdemux->adapter) >= rsdemux->frame_len) {
    // const guint8 *data;
    // gint length;

    // get the accumulated bytes 
    // data = gst_adapter_map (rsdemux->adapter, rsdemux->frame_len);

    // parse header to know the length and other params 
    // if (G_UNLIKELY (dv_parse_header (rsdemux->decoder, data) < 0)) {
      // gst_adapter_unmap (rsdemux->adapter);
      // goto parse_header_error;
    // }
    // gst_adapter_unmap (rsdemux->adapter);

    // after parsing the header we know the length of the data 
    // length = rsdemux->frame_len = rsdemux->decoder->frame_size;
    // if (rsdemux->decoder->system == e_dv_system_625_50) {
      // rsdemux->framerate_numerator = PAL_FRAMERATE_NUMERATOR;
      // rsdemux->framerate_denominator = PAL_FRAMERATE_DENOMINATOR;
    // } else {
      // rsdemux->framerate_numerator = NTSC_FRAMERATE_NUMERATOR;
      // rsdemux->framerate_denominator = NTSC_FRAMERATE_DENOMINATOR;
    // }
    // g_atomic_int_set (&rsdemux->found_header, 1);

    // let demux_video set the height, it needs to detect when things change so
    // it can reset caps 

    // if we still have enough for a frame, start decoding 
    // if (G_LIKELY (gst_adapter_available (rsdemux->adapter) >= length)) {
      // GstBuffer *buffer;

      // buffer = gst_adapter_take_buffer (rsdemux->adapter, length);

      // and decode the buffer, takes ownership 
      ret = gst_rsdemux_demux_frame (rsdemux, buffer);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto done;
    // }
  // }
  
done:
  return ret;

  /* ERRORS */
// parse_header_error:
//   {
//     GST_ELEMENT_ERROR (rsdemux, STREAM, DECODE,
//         (NULL), ("Error parsing header"));
//     return GST_FLOW_ERROR;
//   }
}

/* flush any remaining data in the adapter, used in chain based scheduling mode */
static GstFlowReturn
gst_rsdemux_flush (GstRSDemux * rsdemux)
{
  // TODO What do we need to do in _flush?
  return GST_FLOW_OK;
}

/* streaming operation: 
 *
 * accumulate data until we have a frame, then decode. 
 */
static GstFlowReturn
gst_rsdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret;
  // GstClockTime timestamp;
  // GstRSDemux *rsdemux;
  auto rsdemux = GST_RSDEMUX (parent);

  // Probably don't need to deal with discontinuity

  /* a timestamp always should be respected */
  // timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* and try to flush pending frames */
  ret = gst_rsdemux_flush_buffer (rsdemux, buffer);

  return ret;
}

static gboolean
gst_rsdemux_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  GstRSDemux *demux = GST_RSDEMUX (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        GST_DEBUG_OBJECT (demux, "activating push/chain function");
        // demux->seek_handler = gst_rsdemux_handle_push_seek;
      } else {
        GST_DEBUG_OBJECT (demux, "deactivating push/chain function");
        // demux->seek_handler = NULL;
      }
      res = TRUE;
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

/* decide on push or pull based scheduling */
static gboolean
gst_rsdemux_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
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
    //   rsdemux->decoder = dv_decoder_new (0, FALSE, FALSE);
    //   dv_set_error_log (rsdemux->decoder, NULL);
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
    /*case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (rsdemux->adapter);
      dv_decoder_free (rsdemux->decoder);
      rsdemux->decoder = NULL;
    */
      gst_rsdemux_remove_pads (rsdemux);

    /*  if (rsdemux->tag_event) {
        gst_event_unref (rsdemux->tag_event);
        rsdemux->tag_event = NULL;
      }

      break;
    */
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      // GstEvent **event_p;

      // event_p = &rsdemux->seek_event;
      // gst_event_replace (event_p, NULL);
      // if (rsdemux->pending_segment)
      //   gst_event_unref (rsdemux->pending_segment);
      // rsdemux->pending_segment = NULL;
      break;
    }
    default:
      break;
  }
  return ret;
}
