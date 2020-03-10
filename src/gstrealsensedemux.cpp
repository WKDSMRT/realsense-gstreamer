/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>

// #include <gst/audio/audio.h>
#include "gstrealsensedemux.h"
// #include "gstsmptetimecode.h"

/**
 * SECTION:element-rsdemux
 * @title: rsdemux
 *
 * rsdemux splits raw DV into its audio and video components. The audio will be
 * decoded raw samples and the video will be encoded DV video.
 *
 * This element can operate in both push and pull mode depending on the
 * capabilities of the upstream peer.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=test.dv ! rsdemux name=demux ! queue ! audioconvert ! alsasink demux. ! queue ! dvdec ! xvimagesink
 * ]| This pipeline decodes and renders the raw DV stream to an audio and a videosink.
 *
 */

/* DV output has two modes, normal and wide. The resolution is the same in both
 * cases: 720 pixels wide by 576 pixels tall in PAL format, and 720x480 for
 * NTSC.
 *
 * Each of the modes has its own pixel aspect ratio, which is fixed in practice
 * by ITU-R BT.601 (also known as "CCIR-601" or "Rec.601"). Or so claims a
 * reference that I culled from the reliable "internet",
 * http://www.mir.com/DMG/aspect.html. Normal PAL is 59/54 and normal NTSC is
 * 10/11. Because the pixel resolution is the same for both cases, we can get
 * the pixel aspect ratio for wide recordings by multiplying by the ratio of
 * display aspect ratios, 16/9 (for wide) divided by 4/3 (for normal):
 *
 * Wide NTSC: 10/11 * (16/9)/(4/3) = 40/33
 * Wide PAL: 59/54 * (16/9)/(4/3) = 118/81
 *
 * However, the pixel resolution coming out of a DV source does not combine with
 * the standard pixel aspect ratios to give a proper display aspect ratio. An
 * image 480 pixels tall, with a 4:3 display aspect ratio, will be 768 pixels
 * wide. But, if we take the normal PAL aspect ratio of 59/54, and multiply it
 * with the width of the DV image (720 pixels), we get 786.666..., which is
 * nonintegral and too wide. The camera is not outputting a 4:3 image.
 * 
 * If the video sink for this stream has fixed dimensions (such as for
 * fullscreen playback, or for a java applet in a web page), you then have two
 * choices. Either you show the whole image, but pad the image with black
 * borders on the top and bottom (like watching a widescreen video on a 4:3
 * device), or you crop the video to the proper ratio. Apparently the latter is
 * the standard practice.
 *
 * For its part, GStreamer is concerned with accuracy and preservation of
 * information. This element outputs the 720x576 or 720x480 video that it
 * receives, noting the proper aspect ratio. This should not be a problem for
 * windowed applications, which can change size to fit the video. Applications
 * with fixed size requirements should decide whether to crop or pad which
 * an element such as videobox can do.
 */

#define NTSC_HEIGHT 480
#define NTSC_BUFFER 120000
#define NTSC_FRAMERATE_NUMERATOR 30000
#define NTSC_FRAMERATE_DENOMINATOR 1001

#define PAL_HEIGHT 576
#define PAL_BUFFER 144000
#define PAL_FRAMERATE_NUMERATOR 25
#define PAL_FRAMERATE_DENOMINATOR 1

#define PAL_NORMAL_PAR_X        16
#define PAL_NORMAL_PAR_Y        15
#define PAL_WIDE_PAR_X          64
#define PAL_WIDE_PAR_Y          45

#define NTSC_NORMAL_PAR_X       8
#define NTSC_NORMAL_PAR_Y       9
#define NTSC_WIDE_PAR_X         32
#define NTSC_WIDE_PAR_Y         27

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
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, RGBA, BGR, BGRA, GRAY16_LE, GRAY16_BE, YVYU }"))
    );

static GstStaticPadTemplate depth_src_tmpl = GST_STATIC_PAD_TEMPLATE ("depth",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ GRAY16_LE, GRAY16_BE }"))
    );

// static GstStaticPadTemplate audio_src_temp = GST_STATIC_PAD_TEMPLATE ("audio",
//     GST_PAD_SRC,
//     GST_PAD_SOMETIMES,
//     GST_STATIC_CAPS ("audio/x-raw, "
//         "format = (string) " GST_AUDIO_NE (S16) ", "
//         "layout = (string) interleaved, "
//         "rate = (int) { 32000, 44100, 48000 }, " "channels = (int) {2, 4}")
//     );


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
static void gst_rsdemux_loop (GstPad * pad);
static GstFlowReturn gst_rsdemux_flush (GstRSDemux * rsdemux);
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
  gint i;

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
  GstRSDemux *rsdemux;
  gint i;

  rsdemux = GST_RSDEMUX (object);

//   g_object_unref (rsdemux->adapter);

  /* clean up temp audio buffers */
  for (i = 0; i < 4; i++) {
    g_free (rsdemux->audio_buffers[i]);
  }

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

  rsdemux->discont = TRUE;
  g_atomic_int_set (&rsdemux->found_header, 0);
  rsdemux->frame_len = -1;
  rsdemux->need_segment = FALSE;
  rsdemux->new_media = FALSE;
  rsdemux->framerate_numerator = 0;
  rsdemux->framerate_denominator = 0;
  gst_segment_init (&rsdemux->byte_segment, GST_FORMAT_BYTES);
  gst_segment_init (&rsdemux->time_segment, GST_FORMAT_TIME);
  rsdemux->segment_seqnum = 0;
  rsdemux->upstream_time_segment = FALSE;
  rsdemux->have_group_id = FALSE;
  rsdemux->group_id = G_MAXUINT;
  rsdemux->tag_event = NULL;
}

static gboolean
have_group_id (GstRSDemux * demux)
{
  GstEvent *event;

  event = gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
  if (event) {
    if (gst_event_parse_group_id (event, &demux->group_id))
      demux->have_group_id = TRUE;
    else
      demux->have_group_id = FALSE;
    gst_event_unref (event);
  } else if (!demux->have_group_id) {
    demux->have_group_id = TRUE;
    demux->group_id = gst_util_group_id_next ();
  }

  return demux->have_group_id;
}

static GstEvent *
gst_rsdemux_create_global_tag_event (GstRSDemux * rsdemux)
{
  gchar rec_datetime[40];
  GstDateTime *rec_dt;
  GstTagList *tags;

  tags = gst_tag_list_new (GST_TAG_CONTAINER_FORMAT, "RS", NULL);
  gst_tag_list_set_scope (tags, GST_TAG_SCOPE_GLOBAL);

//   if (dv_get_recording_datetime (rsdemux->decoder, rec_datetime)) {
//     rec_dt = gst_date_time_new_from_iso8601_string (rec_datetime);
//     if (rec_dt) {
//       gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_DATE_TIME,
//           rec_dt, NULL);
//       gst_date_time_unref (rec_dt);
//     }
//   }

  return gst_event_new_tag (tags);
}

#if CUSTOM_ADD_PAD
static GstPad *
gst_rsdemux_add_pad (GstRSDemux * rsdemux, GstStaticPadTemplate * template,
    GstCaps * caps)
{
  GstPad *pad;
  GstEvent *event;
  gchar *stream_id;

  pad = gst_pad_new_from_static_template (template, template->name_template);

  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_rsdemux_src_query));

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_rsdemux_handle_src_event));
  gst_pad_use_fixed_caps (pad);
  gst_pad_set_active (pad, TRUE);

  stream_id =
      gst_pad_create_stream_id (pad,
      GST_ELEMENT_CAST (rsdemux),
      template == &video_src_temp ? "video" : "audio");
  event = gst_event_new_stream_start (stream_id);
  if (have_group_id (rsdemux))
    gst_event_set_group_id (event, rsdemux->group_id);
  gst_pad_push_event (pad, event);
  g_free (stream_id);

//   gst_pad_set_caps (pad, caps);

  gst_pad_push_event (pad, gst_event_new_segment (&rsdemux->time_segment));

  gst_element_add_pad (GST_ELEMENT (rsdemux), pad);

  if (!rsdemux->tag_event) {
    rsdemux->tag_event = gst_rsdemux_create_global_tag_event (rsdemux);
  }

  if (rsdemux->tag_event) {
    gst_pad_push_event (pad, gst_event_ref (rsdemux->tag_event));
  }

  return pad;
}
#endif
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

  if (rsdemux->frame_len <= 0)
    goto error;

//   if (rsdemux->decoder == NULL)
//     goto error;

  GST_INFO_OBJECT (pad,
      "src_value:%" G_GINT64_FORMAT ", src_format:%d, dest_format:%d",
      src_value, src_format, dest_format);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (dest_format) {
        case GST_FORMAT_DEFAULT:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad)
            *dest_value = src_value / rsdemux->frame_len;
#if AUDIO_SRC
          else if (pad == rsdemux->audiosrcpad)
            *dest_value = src_value / (2 * rsdemux->channels);
#endif
          break;
        case GST_FORMAT_TIME:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad)
            *dest_value = gst_util_uint64_scale (src_value,
                GST_SECOND * rsdemux->framerate_denominator,
                rsdemux->frame_len * rsdemux->framerate_numerator);
#if AUDIO_SRC                
          else if (pad == rsdemux->audiosrcpad)
            *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
                2 * rsdemux->frequency * rsdemux->channels);
#endif        
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_format) {
        case GST_FORMAT_BYTES:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad)
            *dest_value = gst_util_uint64_scale (src_value,
                rsdemux->frame_len * rsdemux->framerate_numerator,
                rsdemux->framerate_denominator * GST_SECOND);
#if AUDIO_SRC                
          else if (pad == rsdemux->audiosrcpad)
            *dest_value = gst_util_uint64_scale_int (src_value,
                2 * rsdemux->frequency * rsdemux->channels, GST_SECOND);
#endif                
          break;
        case GST_FORMAT_DEFAULT:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad) {
            if (src_value)
              *dest_value = gst_util_uint64_scale (src_value,
                  rsdemux->framerate_numerator,
                  rsdemux->framerate_denominator * GST_SECOND);
            else
              *dest_value = 0;
          }
#if AUDIO_SRC
          else if (pad == rsdemux->audiosrcpad) {
            *dest_value = gst_util_uint64_scale (src_value,
                rsdemux->frequency, GST_SECOND);
          }
#endif          
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (dest_format) {
        case GST_FORMAT_TIME:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad) {
            *dest_value = gst_util_uint64_scale (src_value,
                GST_SECOND * rsdemux->framerate_denominator,
                rsdemux->framerate_numerator);
          } 
#if AUDIO_SRC
          else if (pad == rsdemux->audiosrcpad) {
            if (src_value)
              *dest_value = gst_util_uint64_scale (src_value,
                  GST_SECOND, rsdemux->frequency);
            else
              *dest_value = 0;
          }
#endif
          break;
        case GST_FORMAT_BYTES:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad) {
            *dest_value = src_value * rsdemux->frame_len;
          } 
#if AUDIO_SRC
          else if (pad == rsdemux->audiosrcpad) {
            *dest_value = src_value * 2 * rsdemux->channels;
          }
#endif          
          break;
        default:
          res = FALSE;
      }
      break;
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

  if (rsdemux->frame_len <= 0)
    goto error;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (dest_format) {
        case GST_FORMAT_TIME:
        {
          guint64 frame;

          /* get frame number, rounds down so don't combine this
           * line and the next line. */
          frame = src_value / rsdemux->frame_len;

          *dest_value = gst_util_uint64_scale (frame,
              GST_SECOND * rsdemux->framerate_denominator,
              rsdemux->framerate_numerator);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_format) {
        case GST_FORMAT_BYTES:
        {
          guint64 frame;

          /* calculate the frame */
          frame =
              gst_util_uint64_scale (src_value, rsdemux->framerate_numerator,
              rsdemux->framerate_denominator * GST_SECOND);

          /* calculate the offset from the rounded frame */
          *dest_value = frame * rsdemux->frame_len;
          break;
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
  GstRSDemux *rsdemux = GST_RSDEMUX (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur;

      /* get target format */
      gst_query_parse_position (query, &format, NULL);

      /* bring the position to the requested format. */
      if (!(res = gst_rsdemux_src_convert (rsdemux, pad,
                  GST_FORMAT_TIME, rsdemux->time_segment.position,
                  format, &cur)))
        goto error;
      gst_query_set_position (query, format, cur);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 end;
      GstQuery *pquery;

      /* First ask the peer in the original format */
      if (!gst_pad_peer_query (rsdemux->sinkpad, query)) {
        /* get target format */
        gst_query_parse_duration (query, &format, NULL);

        /* Now ask the peer in BYTES format and try to convert */
        pquery = gst_query_new_duration (GST_FORMAT_BYTES);
        if (!gst_pad_peer_query (rsdemux->sinkpad, pquery)) {
          gst_query_unref (pquery);
          goto error;
        }

        /* get peer total length */
        gst_query_parse_duration (pquery, NULL, &end);
        gst_query_unref (pquery);

        /* convert end to requested format */
        if (end != -1) {
          if (!(res = gst_rsdemux_sink_convert (rsdemux,
                      GST_FORMAT_BYTES, end, format, &end))) {
            goto error;
          }
          gst_query_set_duration (query, format, end);
        }
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_rsdemux_src_convert (rsdemux, pad, src_fmt, src_val,
                  dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFormat fmt;
      GstQuery *peerquery;
      gboolean seekable;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);

      /* We can only handle TIME seeks really */
      if (fmt != GST_FORMAT_TIME) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
        break;
      }

      /* First ask upstream */
      if (gst_pad_peer_query (rsdemux->sinkpad, query)) {
        gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
        if (seekable) {
          res = TRUE;
          break;
        }
      }

      res = TRUE;

      peerquery = gst_query_new_seeking (GST_FORMAT_BYTES);
      seekable = gst_pad_peer_query (rsdemux->sinkpad, peerquery);

      if (seekable)
        gst_query_parse_seeking (peerquery, NULL, &seekable, NULL, NULL);
      gst_query_unref (peerquery);

      if (seekable) {
        peerquery = gst_query_new_duration (GST_FORMAT_TIME);
        seekable = gst_rsdemux_src_query (pad, parent, peerquery);

        if (seekable) {
          gint64 duration;

          gst_query_parse_duration (peerquery, NULL, &duration);
          gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0, duration);
        } else {
          gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);
        }

        gst_query_unref (peerquery);
      } else {
        gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG ("error source query");
    return FALSE;
  }
}

static gboolean
gst_rsdemux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstRSDemux *rsdemux;

  rsdemux = GST_RSDEMUX (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_rsdemux_sink_convert (rsdemux, src_fmt, src_val, dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG ("error handling sink query");
    return FALSE;
  }
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
#if AUDIO_SRC
  if (rsdemux->audiosrcpad)
    res |= gst_pad_push_event (rsdemux->audiosrcpad, event);
  else 
#endif
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
      gst_segment_init (&rsdemux->byte_segment, GST_FORMAT_BYTES);
      gst_segment_init (&rsdemux->time_segment, GST_FORMAT_TIME);
      rsdemux->discont = TRUE;
      res = gst_rsdemux_push_event (rsdemux, event);
      break;
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);
      switch (segment->format) {
        case GST_FORMAT_BYTES:
          gst_segment_copy_into (segment, &rsdemux->byte_segment);
          rsdemux->need_segment = TRUE;
          rsdemux->segment_seqnum = gst_event_get_seqnum (event);
          gst_event_unref (event);
          break;
        case GST_FORMAT_TIME:
          gst_segment_copy_into (segment, &rsdemux->time_segment);

          rsdemux->upstream_time_segment = TRUE;
          rsdemux->segment_seqnum = gst_event_get_seqnum (event);

          /* and we can just forward this time event */
          res = gst_rsdemux_push_event (rsdemux, event);
          break;
        default:
          gst_event_unref (event);
          /* cannot accept this format */
          res = FALSE;
          break;
      }
      break;
    }
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

/* convert a pair of values on the srcpad to a pair of
 * values on the sinkpad 
 */
static gboolean
gst_rsdemux_convert_src_to_sink (GstRSDemux * rsdemux, GstPad * pad,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  GstFormat conv;
  gboolean res;

  conv = GST_FORMAT_TIME;
  /* convert to TIME intermediate format */
  if (!(res = gst_rsdemux_convert_src_pair (rsdemux, pad,
              src_format, src_start, src_stop, conv, dst_start, dst_stop))) {
    /* could not convert format to time offset */
    goto done;
  }
  /* convert to dst format on sinkpad */
  if (!(res = gst_rsdemux_convert_sink_pair (rsdemux,
              conv, *dst_start, *dst_stop, dst_format, dst_start, dst_stop))) {
    /* could not convert format to time offset */
    goto done;
  }
done:
  return res;
}

#if 0
static gboolean
gst_rsdemux_convert_segment (Gstrsdemux * rsdemux, GstSegment * src,
    GstSegment * dest)
{
  dest->rate = src->rate;
  dest->abs_rate = src->abs_rate;
  dest->flags = src->flags;

  return TRUE;
}
#endif

/* handle seek in push base mode.
 *
 * Convert the time seek to a bytes seek and send it
 * upstream
 * Does not take ownership of the event.
 */
static gboolean
gst_rsdemux_handle_push_seek (GstRSDemux * rsdemux, GstPad * pad,
    GstEvent * event)
{
  gboolean res = FALSE;
  gdouble rate;
  GstSeekFlags flags;
  GstFormat format;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 start_position, end_position;
  GstEvent *newevent;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  /* First try if upstream can handle time based seeks */
  if (format == GST_FORMAT_TIME)
    res = gst_pad_push_event (rsdemux->sinkpad, gst_event_ref (event));

  if (!res) {
    /* we convert the start/stop on the srcpad to the byte format
     * on the sinkpad and forward the event */
    res = gst_rsdemux_convert_src_to_sink (rsdemux, pad,
        format, cur, stop, GST_FORMAT_BYTES, &start_position, &end_position);
    if (!res)
      goto done;

    /* now this is the updated seek event on bytes */
    newevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
        cur_type, start_position, stop_type, end_position);
    gst_event_set_seqnum (newevent, gst_event_get_seqnum (event));

    res = gst_pad_push_event (rsdemux->sinkpad, newevent);
  }
done:
  return res;
}

#if PROBABLY_UNUSED
static void
gst_rsdemux_update_frame_offsets (GstRSDemux * rsdemux, GstClockTime timestamp)
{
  /* calculate current frame number */
  gst_rsdemux_src_convert (rsdemux, rsdemux->colorsrcpad,
      rsdemux->time_segment.format, timestamp,
      GST_FORMAT_DEFAULT, &rsdemux->video_offset);

#if AUDIO_SRC
  /* calculate current audio number */
  gst_rsdemux_src_convert (rsdemux, rsdemux->audiosrcpad,
      rsdemux->time_segment.format, timestamp,
      GST_FORMAT_DEFAULT, &rsdemux->audio_offset);
#endif
  /* every DV frame corresponts with one video frame */
  rsdemux->frame_offset = rsdemux->video_offset;
}
#endif
/* position ourselves to the configured segment, used in pull mode.
 * The input segment is in TIME format. We convert the time values
 * to bytes values into our byte_segment which we use to pull data from
 * the sinkpad peer.
 */
static gboolean
gst_rsdemux_do_seek (GstRSDemux * demux, GstSegment * segment)
{
  gboolean res;
  GstFormat format;

  /* position to value configured is last_stop, this will round down
   * to the byte position where the frame containing the given 
   * timestamp can be found. */
  format = GST_FORMAT_BYTES;
  res = gst_rsdemux_sink_convert (demux,
      segment->format, segment->position,
      format, (gint64 *) & demux->byte_segment.position);
  if (!res)
    goto done;

  /* update byte segment start */
  gst_rsdemux_sink_convert (demux,
      segment->format, segment->start, format,
      (gint64 *) & demux->byte_segment.start);

  /* update byte segment stop */
  gst_rsdemux_sink_convert (demux,
      segment->format, segment->stop, format,
      (gint64 *) & demux->byte_segment.stop);

  /* update byte segment time */
  gst_rsdemux_sink_convert (demux,
      segment->format, segment->time, format,
      (gint64 *) & demux->byte_segment.time);
#if PROBABLY_UNUSED
  gst_rsdemux_update_frame_offsets (demux, segment->start);
#endif
  demux->discont = TRUE;

done:
  return res;
}

/* handle seek in pull base mode.
 *
 * Does not take ownership of the event.
 */
static gboolean
gst_rsdemux_handle_pull_seek (GstRSDemux * demux, GstPad * pad,
    GstEvent * event)
{
  gboolean res;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  GstSegment seeksegment;
  GstEvent *new_event;

  GST_DEBUG_OBJECT (demux, "doing seek");

  /* first bring the event format to TIME, our native format
   * to perform the seek on */
  if (event) {
    GstFormat conv;

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* can't seek backwards yet */
    if (rate <= 0.0)
      goto wrong_rate;

    /* convert input format to TIME */
    conv = GST_FORMAT_TIME;
    if (!(gst_rsdemux_convert_src_pair (demux, pad,
                format, cur, stop, conv, &cur, &stop)))
      goto no_format;

    format = GST_FORMAT_TIME;
  } else {
    flags = GST_SEEK_FLAG_NONE;
  }

  demux->segment_seqnum = gst_event_get_seqnum (event);

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush) {
    new_event = gst_event_new_flush_start ();
    gst_event_set_seqnum (new_event, demux->segment_seqnum);
    gst_rsdemux_push_event (demux, new_event);
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  /* make copy into temp structure, we can only update the main one
   * when the subclass actually could to the seek. */
  memcpy (&seeksegment, &demux->time_segment, sizeof (GstSegment));

  /* now configure the seek segment */
  if (event) {
    gst_segment_do_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (demux, "segment configured from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT ", position %" G_GINT64_FORMAT,
      seeksegment.start, seeksegment.stop, seeksegment.position);

  /* do the seek, segment.position contains new position. */
  res = gst_rsdemux_do_seek (demux, &seeksegment);

  /* and prepare to continue streaming */
  if (flush) {
    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    new_event = gst_event_new_flush_stop (TRUE);
    gst_event_set_seqnum (new_event, demux->segment_seqnum);
    gst_rsdemux_push_event (demux, new_event);
  }

  /* if successful seek, we update our real segment and push
   * out the new segment. */
  if (res) {
    memcpy (&demux->time_segment, &seeksegment, sizeof (GstSegment));

    if (demux->time_segment.flags & GST_SEEK_FLAG_SEGMENT) {
      GstMessage *message;

      message = gst_message_new_segment_start (GST_OBJECT_CAST (demux),
          demux->time_segment.format, demux->time_segment.position);
      gst_message_set_seqnum (message, demux->segment_seqnum);
      gst_element_post_message (GST_ELEMENT_CAST (demux), message);
    }
    if ((stop = demux->time_segment.stop) == -1)
      stop = demux->time_segment.duration;

    GST_INFO_OBJECT (demux,
        "Saving newsegment event to be sent in streaming thread");

    if (demux->pending_segment)
      gst_event_unref (demux->pending_segment);

    demux->pending_segment = gst_event_new_segment (&demux->time_segment);
    gst_event_set_seqnum (demux->pending_segment, demux->segment_seqnum);

    demux->need_segment = FALSE;
  }

  /* and restart the task in case it got paused explicitly or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_rsdemux_loop,
      demux->sinkpad, NULL);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

  /* ERRORS */
wrong_rate:
  {
    GST_DEBUG_OBJECT (demux, "negative playback rate %lf not supported.", rate);
    return FALSE;
  }
no_format:
  {
    GST_DEBUG_OBJECT (demux, "cannot convert to TIME format, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_rsdemux_send_event (GstElement * element, GstEvent * event)
{
  GstRSDemux *rsdemux = GST_RSDEMUX (element);
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      /* checking header and configuring the seek must be atomic */
      GST_OBJECT_LOCK (rsdemux);
      if (g_atomic_int_get (&rsdemux->found_header) == 0) {
        GstEvent **event_p;

        event_p = &rsdemux->seek_event;

        /* We don't have pads yet. Keep the event. */
        GST_INFO_OBJECT (rsdemux, "Keeping the seek event for later");

        gst_event_replace (event_p, event);
        GST_OBJECT_UNLOCK (rsdemux);

        res = TRUE;
      } else {
        GST_OBJECT_UNLOCK (rsdemux);

        if (rsdemux->seek_handler)
          res = rsdemux->seek_handler (rsdemux, rsdemux->colorsrcpad, event);
        gst_event_unref (event);
      }
      break;
    }
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
  GstRSDemux *rsdemux;

  rsdemux = GST_RSDEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* seek handler is installed based on scheduling mode */
      if (rsdemux->seek_handler)
        res = rsdemux->seek_handler (rsdemux, pad, event);
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_push_event (rsdemux->sinkpad, event);
      break;
  }

  return res;
}

/* does not take ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_audio (GstRSDemux * rsdemux, GstBuffer * buffer,
    guint64 duration)
{
  gint num_samples;
  GstFlowReturn ret;
  GstMapInfo map;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
//   dv_decode_full_audio (rsdemux->decoder, map.data, rsdemux->audio_buffers);
  gst_buffer_unmap (buffer, &map);
/*
  if (G_LIKELY ((num_samples = dv_get_num_samples (rsdemux->decoder)) > 0)) {
    gint16 *a_ptr;
    gint i, j;
    GstBuffer *outbuf;
    gint frequency, channels;

    // get initial format or check if format changed 
    frequency = dv_get_frequency (rsdemux->decoder);
    channels = dv_get_num_channels (rsdemux->decoder);

    if (G_UNLIKELY ((rsdemux->audiosrcpad == NULL)
            || (frequency != rsdemux->frequency)
            || (channels != rsdemux->channels))) {
      GstCaps *caps;
      GstAudioInfo info;

      rsdemux->frequency = frequency;
      rsdemux->channels = channels;

      gst_audio_info_init (&info);
      gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16LE,
          frequency, channels, NULL);
      caps = gst_audio_info_to_caps (&info);
      if (G_UNLIKELY (rsdemux->audiosrcpad == NULL)) {
        rsdemux->audiosrcpad =
            gst_rsdemux_add_pad (rsdemux, &audio_src_temp, caps);

        if (rsdemux->colorsrcpad && rsdemux->audiosrcpad)
          gst_element_no_more_pads (GST_ELEMENT (rsdemux));

      } else {
        gst_pad_set_caps (rsdemux->audiosrcpad, caps);
      }
      gst_caps_unref (caps);
    }

    outbuf = gst_buffer_new_and_alloc (num_samples *
        sizeof (gint16) * rsdemux->channels);

    gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
    a_ptr = (gint16 *) map.data;

    for (i = 0; i < num_samples; i++) {
      for (j = 0; j < rsdemux->channels; j++) {
        *(a_ptr++) = rsdemux->audio_buffers[j][i];
      }
    }
    gst_buffer_unmap (outbuf, &map);

    GST_DEBUG ("pushing audio %" GST_TIME_FORMAT,
        GST_TIME_ARGS (rsdemux->time_segment.position));

    GST_BUFFER_TIMESTAMP (outbuf) = rsdemux->time_segment.position;
    GST_BUFFER_DURATION (outbuf) = duration;
    GST_BUFFER_OFFSET (outbuf) = rsdemux->audio_offset;
    rsdemux->audio_offset += num_samples;
    GST_BUFFER_OFFSET_END (outbuf) = rsdemux->audio_offset;

    if (rsdemux->new_media || rsdemux->discont)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    ret = gst_pad_push (rsdemux->audiosrcpad, outbuf);
  } else {
    // no samples *
    ret = GST_FLOW_OK;
  }
*/
  return ret;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_video (GstRSDemux * rsdemux, GstBuffer * buffer,
    guint64 duration)
{
  GstBuffer *outbuf;
  gint height;
  gboolean wide;
  GstFlowReturn ret = GST_FLOW_OK;

  /* get params */
  /* framerate is already up-to-date */
  /*
  height = rsdemux->decoder->height;
  wide = dv_format_wide (rsdemux->decoder);

  // see if anything changed 
  if (G_UNLIKELY ((rsdemux->colorsrcpad == NULL) || (rsdemux->height != height)
          || rsdemux->wide != wide)) {
    gint par_x, par_y;
    GstCaps *caps;

    rsdemux->height = height;
    rsdemux->wide = wide;

    if (rsdemux->decoder->system == e_dv_system_625_50) {
      if (wide) {
        par_x = PAL_WIDE_PAR_X;
        par_y = PAL_WIDE_PAR_Y;
      } else {
        par_x = PAL_NORMAL_PAR_X;
        par_y = PAL_NORMAL_PAR_Y;
      }
    } else {
      if (wide) {
        par_x = NTSC_WIDE_PAR_X;
        par_y = NTSC_WIDE_PAR_Y;
      } else {
        par_x = NTSC_NORMAL_PAR_X;
        par_y = NTSC_NORMAL_PAR_Y;
      }
    }
    
    caps = gst_caps_new_simple ("video/x-dv",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "width", G_TYPE_INT, 720,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, rsdemux->framerate_numerator,
        rsdemux->framerate_denominator,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_x, par_y, NULL);

    if (G_UNLIKELY (rsdemux->colorsrcpad == NULL)) {
      rsdemux->colorsrcpad =
          gst_rsdemux_add_pad (rsdemux, &video_src_temp, caps);

      if (rsdemux->colorsrcpad && rsdemux->audiosrcpad)
        gst_element_no_more_pads (GST_ELEMENT (rsdemux));

    } else {
      gst_pad_set_caps (rsdemux->colorsrcpad, caps);
    }
    gst_caps_unref (caps);
  }
  */
  /* takes ownership of buffer here, we just need to modify
   * the metadata. */
  outbuf = gst_buffer_make_writable (buffer);

  GST_BUFFER_TIMESTAMP (outbuf) = rsdemux->time_segment.position;
#if PROBABLY_UNUSED
  GST_BUFFER_OFFSET (outbuf) = rsdemux->video_offset;
  GST_BUFFER_OFFSET_END (outbuf) = rsdemux->video_offset + 1;
#endif
  GST_BUFFER_DURATION (outbuf) = duration;

  if (rsdemux->new_media || rsdemux->discont)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_DEBUG ("pushing video %" GST_TIME_FORMAT,
      GST_TIME_ARGS (rsdemux->time_segment.position));

  ret = gst_pad_push (rsdemux->colorsrcpad, outbuf);
#if PROBABLY_UNUSED
  rsdemux->video_offset++;
#endif
  return ret;
}

static int
get_ssyb_offset (int dif, int ssyb)
{
  int offset;

  offset = dif * 12000;         /* to dif */
  offset += 80 * (1 + (ssyb / 6));      /* to subcode pack */
  offset += 3;                  /* past header */
  offset += 8 * (ssyb % 6);     /* to ssyb */

  return offset;
}

/*static gboolean
gst_rsdemux_get_timecode (GstRSDemux * rsdemux, GstBuffer * buffer,
    GstSMPTETimeCode * timecode)
{
  guint8 *data;
  GstMapInfo map;
  int offset;
  int dif;
  int n_difs = rsdemux->decoder->num_dif_seqs;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  data = map.data;
  for (dif = 0; dif < n_difs; dif++) {
    offset = get_ssyb_offset (dif, 3);
    if (data[offset + 3] == 0x13) {
      timecode->frames = ((data[offset + 4] >> 4) & 0x3) * 10 +
          (data[offset + 4] & 0xf);
      timecode->seconds = ((data[offset + 5] >> 4) & 0x3) * 10 +
          (data[offset + 5] & 0xf);
      timecode->minutes = ((data[offset + 6] >> 4) & 0x3) * 10 +
          (data[offset + 6] & 0xf);
      timecode->hours = ((data[offset + 7] >> 4) & 0x3) * 10 +
          (data[offset + 7] & 0xf);
      GST_DEBUG ("got timecode %" GST_SMPTE_TIME_CODE_FORMAT,
          GST_SMPTE_TIME_CODE_ARGS (timecode));
      gst_buffer_unmap (buffer, &map);
      return TRUE;
    }
  }

  gst_buffer_unmap (buffer, &map);
  return FALSE;
}*/

static gboolean
gst_rsdemux_is_new_media (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  guint8 *data;
  GstMapInfo map;
  int aaux_offset;
  int dif;
  int n_difs;

//   n_difs = rsdemux->decoder->num_dif_seqs;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  data = map.data;
  for (dif = 0; dif < n_difs; dif++) {
    if (dif & 1) {
      aaux_offset = (dif * 12000) + (6 + 16 * 1) * 80 + 3;
    } else {
      aaux_offset = (dif * 12000) + (6 + 16 * 4) * 80 + 3;
    }
    if (data[aaux_offset + 0] == 0x51) {
      if ((data[aaux_offset + 2] & 0x80) == 0) {
        gst_buffer_unmap (buffer, &map);
        return TRUE;
      }
    }
  }

  gst_buffer_unmap (buffer, &map);
  return FALSE;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_frame (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  GstClockTime next_ts;
  GstFlowReturn aret, vret, ret;
  GstMapInfo map;
  guint64 duration;
//   GstSMPTETimeCode timecode;
  int frame_number;

  if (rsdemux->need_segment) {
    GstFormat format;
    GstEvent *event;

    g_assert (!rsdemux->upstream_time_segment);

    /* convert to time and store as start/end_timestamp */
    format = GST_FORMAT_TIME;
    if (!(gst_rsdemux_convert_sink_pair (rsdemux,
                GST_FORMAT_BYTES, rsdemux->byte_segment.start,
                rsdemux->byte_segment.stop, format,
                (gint64 *) & rsdemux->time_segment.start,
                (gint64 *) & rsdemux->time_segment.stop)))
      goto segment_error;

    rsdemux->time_segment.time = rsdemux->time_segment.start;
    rsdemux->time_segment.rate = rsdemux->byte_segment.rate;

    gst_rsdemux_sink_convert (rsdemux,
        GST_FORMAT_BYTES, rsdemux->byte_segment.position,
        GST_FORMAT_TIME, (gint64 *) & rsdemux->time_segment.position);

#if PROBABLY_UNUSED
    gst_rsdemux_update_frame_offsets (rsdemux, rsdemux->time_segment.position);
#endif
    GST_DEBUG_OBJECT (rsdemux, "sending segment start: %" GST_TIME_FORMAT
        ", stop: %" GST_TIME_FORMAT ", time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (rsdemux->time_segment.start),
        GST_TIME_ARGS (rsdemux->time_segment.stop),
        GST_TIME_ARGS (rsdemux->time_segment.start));

    event = gst_event_new_segment (&rsdemux->time_segment);
    if (rsdemux->segment_seqnum)
      gst_event_set_seqnum (event, rsdemux->segment_seqnum);
    gst_rsdemux_push_event (rsdemux, event);

    rsdemux->need_segment = FALSE;
  }

#if PROBABLY_UNUSED
  gst_rsdemux_get_timecode (rsdemux, buffer, &timecode);
  gst_smpte_time_code_get_frame_number (
      (rsdemux->decoder->system == e_dv_system_625_50) ?
      GST_SMPTE_TIME_CODE_SYSTEM_25 : GST_SMPTE_TIME_CODE_SYSTEM_30,
      &frame_number, &timecode);

  if (rsdemux->time_segment.rate < 0) {
    next_ts = gst_util_uint64_scale_int (
        (rsdemux->frame_offset >
            0 ? rsdemux->frame_offset - 1 : 0) * GST_SECOND,
        rsdemux->framerate_denominator, rsdemux->framerate_numerator);
    duration = rsdemux->time_segment.position - next_ts;
  } else {
    next_ts = gst_util_uint64_scale_int (
        (rsdemux->frame_offset + 1) * GST_SECOND,
        rsdemux->framerate_denominator, rsdemux->framerate_numerator);
    duration = next_ts - rsdemux->time_segment.position;
  }
#endif
  gst_buffer_map (buffer, &map, GST_MAP_READ);
//   dv_parse_packs (rsdemux->decoder, map.data);
  gst_buffer_unmap (buffer, &map);
  rsdemux->new_media = FALSE;
  if (gst_rsdemux_is_new_media (rsdemux, buffer) &&
      rsdemux->frames_since_new_media > 2) {
    rsdemux->new_media = TRUE;
    rsdemux->frames_since_new_media = 0;
  }
  rsdemux->frames_since_new_media++;

  /* does not take ownership of buffer */
  aret = ret = gst_rsdemux_demux_audio (rsdemux, buffer, duration);
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)) {
    gst_buffer_unref (buffer);
    goto done;
  }

  /* takes ownership of buffer */
  vret = ret = gst_rsdemux_demux_video (rsdemux, buffer, duration);
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
    goto done;

  /* if both are not linked, we stop */
  if (G_UNLIKELY (aret == GST_FLOW_NOT_LINKED && vret == GST_FLOW_NOT_LINKED)) {
    ret = GST_FLOW_NOT_LINKED;
    goto done;
  }

  rsdemux->discont = FALSE;
  rsdemux->time_segment.position = next_ts;

#if PROBABLY_UNUSED
  if (rsdemux->time_segment.rate < 0) {
    if (rsdemux->frame_offset > 0)
      rsdemux->frame_offset--;
    else
      GST_WARNING_OBJECT (rsdemux,
          "Got before frame offset 0 in reverse playback");
  } else {
    rsdemux->frame_offset++;
  }
#endif
  /* check for the end of the segment */
  if ((rsdemux->time_segment.rate > 0 && rsdemux->time_segment.stop != -1
          && next_ts > rsdemux->time_segment.stop)
      || (rsdemux->time_segment.rate < 0
          && rsdemux->time_segment.start > next_ts))
    ret = GST_FLOW_EOS;
  else
    ret = GST_FLOW_OK;

done:
  return ret;

  /* ERRORS */
segment_error:
  {
    GST_DEBUG ("error generating new_segment event");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

/* flush any remaining data in the adapter, used in chain based scheduling mode */
static GstFlowReturn
gst_rsdemux_flush (GstRSDemux * rsdemux)
{
  GstFlowReturn ret = GST_FLOW_OK;
/*
  while (gst_adapter_available (rsdemux->adapter) >= rsdemux->frame_len) {
    const guint8 *data;
    gint length;

    /* get the accumulated bytes 
    data = gst_adapter_map (rsdemux->adapter, rsdemux->frame_len);

    /* parse header to know the length and other params 
    if (G_UNLIKELY (dv_parse_header (rsdemux->decoder, data) < 0)) {
      gst_adapter_unmap (rsdemux->adapter);
      goto parse_header_error;
    }
    gst_adapter_unmap (rsdemux->adapter);

    /* after parsing the header we know the length of the data 
    length = rsdemux->frame_len = rsdemux->decoder->frame_size;
    if (rsdemux->decoder->system == e_dv_system_625_50) {
      rsdemux->framerate_numerator = PAL_FRAMERATE_NUMERATOR;
      rsdemux->framerate_denominator = PAL_FRAMERATE_DENOMINATOR;
    } else {
      rsdemux->framerate_numerator = NTSC_FRAMERATE_NUMERATOR;
      rsdemux->framerate_denominator = NTSC_FRAMERATE_DENOMINATOR;
    }
    g_atomic_int_set (&rsdemux->found_header, 1);

    /* let demux_video set the height, it needs to detect when things change so
     * it can reset caps 

    /* if we still have enough for a frame, start decoding 
    if (G_LIKELY (gst_adapter_available (rsdemux->adapter) >= length)) {
      GstBuffer *buffer;

      buffer = gst_adapter_take_buffer (rsdemux->adapter, length);

      /* and decode the buffer, takes ownership 
      ret = gst_rsdemux_demux_frame (rsdemux, buffer);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto done;
    }
  }
  */
done:
  return ret;

  /* ERRORS */
parse_header_error:
  {
    GST_ELEMENT_ERROR (rsdemux, STREAM, DECODE,
        (NULL), ("Error parsing DV header"));
    return GST_FLOW_ERROR;
  }
}

/* streaming operation: 
 *
 * accumulate data until we have a frame, then decode. 
 */
static GstFlowReturn
gst_rsdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRSDemux *rsdemux;
  GstFlowReturn ret;
  GstClockTime timestamp;

  rsdemux = GST_RSDEMUX (parent);

  /* a discontinuity in the stream, we need to get rid of
   * accumulated data in the adapter and assume a new frame
   * starts after the discontinuity */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    // gst_adapter_clear (rsdemux->adapter);
    rsdemux->discont = TRUE;

    /* Should recheck where we are */
    if (!rsdemux->upstream_time_segment)
      rsdemux->need_segment = TRUE;
  }

  /* a timestamp always should be respected */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    rsdemux->time_segment.position = timestamp;
#if PROBABLY_UNUSED
    if (rsdemux->discont)
      gst_rsdemux_update_frame_offsets (rsdemux,
          rsdemux->time_segment.position);
#endif
  } else if (rsdemux->upstream_time_segment && rsdemux->discont) {
    /* This will probably fail later to provide correct
     * timestamps and/or durations but also should not happen */
    GST_ERROR_OBJECT (rsdemux,
        "Upstream provides TIME segment but no PTS after discont");
  }

//   gst_adapter_push (rsdemux->adapter, buffer);

  /* Apparently dv_parse_header can read from the body of the frame
   * too, so it needs more than header_size bytes. Wacky!
   */
  if (G_UNLIKELY (rsdemux->frame_len == -1)) {
    /* if we don't know the length of a frame, we assume it is
     * the NTSC_BUFFER length, as this is enough to figure out 
     * if this is PAL or NTSC */
    rsdemux->frame_len = NTSC_BUFFER;
  }

  /* and try to flush pending frames */
  ret = gst_rsdemux_flush (rsdemux);

  return ret;
}

/* pull based operation.
 *
 * Read header first to figure out the frame size. Then read
 * and decode full frames.
 */
static void
gst_rsdemux_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstRSDemux *rsdemux;
  GstBuffer *buffer = NULL;
  GstMapInfo map;

  rsdemux = GST_RSDEMUX (gst_pad_get_parent (pad));

  if (G_UNLIKELY (g_atomic_int_get (&rsdemux->found_header) == 0)) {
    GST_DEBUG_OBJECT (rsdemux, "pulling first buffer");
    /* pull in NTSC sized buffer to figure out the frame
     * length */
    ret = gst_pad_pull_range (rsdemux->sinkpad,
        rsdemux->byte_segment.position, NTSC_BUFFER, &buffer);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto pause;

    /* check buffer size, don't want to read small buffers */
    if (G_UNLIKELY (gst_buffer_get_size (buffer) < NTSC_BUFFER))
      goto small_buffer;

    gst_buffer_map (buffer, &map, GST_MAP_READ);
    /* parse header to know the length and other params */
    // if (G_UNLIKELY (dv_parse_header (rsdemux->decoder, map.data) < 0)) {
    //   gst_buffer_unmap (buffer, &map);
    //   goto parse_header_error;
    // }
    gst_buffer_unmap (buffer, &map);

    /* after parsing the header we know the length of the data */
    // rsdemux->frame_len = rsdemux->decoder->frame_size;
    // if (rsdemux->decoder->system == e_dv_system_625_50) {
    //   rsdemux->framerate_numerator = PAL_FRAMERATE_NUMERATOR;
    //   rsdemux->framerate_denominator = PAL_FRAMERATE_DENOMINATOR;
    // } else 
    {
      rsdemux->framerate_numerator = NTSC_FRAMERATE_NUMERATOR;
      rsdemux->framerate_denominator = NTSC_FRAMERATE_DENOMINATOR;
    }
    rsdemux->need_segment = TRUE;

    /* see if we need to read a larger part */
    if (rsdemux->frame_len != NTSC_BUFFER) {
      gst_buffer_unref (buffer);
      buffer = NULL;
    }

    {
      GstEvent *event;

      /* setting header and prrforming the seek must be atomic */
      GST_OBJECT_LOCK (rsdemux);
      /* got header now */
      g_atomic_int_set (&rsdemux->found_header, 1);

      /* now perform pending seek if any. */
      event = rsdemux->seek_event;
      if (event)
        gst_event_ref (event);
      GST_OBJECT_UNLOCK (rsdemux);

      if (event) {
        if (!gst_rsdemux_handle_pull_seek (rsdemux, rsdemux->colorsrcpad,
                event)) {
          GST_ELEMENT_WARNING (rsdemux, STREAM, DECODE, (NULL),
              ("Error performing initial seek"));
        }
        gst_event_unref (event);

        /* and we need to pull a new buffer in all cases. */
        if (buffer) {
          gst_buffer_unref (buffer);
          buffer = NULL;
        }
      }
    }
  }

  if (G_UNLIKELY (rsdemux->pending_segment)) {

    /* now send the newsegment */
    GST_DEBUG_OBJECT (rsdemux, "Sending newsegment from");

    gst_rsdemux_push_event (rsdemux, rsdemux->pending_segment);
    rsdemux->pending_segment = NULL;
  }

  if (G_LIKELY (buffer == NULL)) {
    GST_DEBUG_OBJECT (rsdemux, "pulling buffer at offset %" G_GINT64_FORMAT,
        rsdemux->byte_segment.position);

    ret = gst_pad_pull_range (rsdemux->sinkpad,
        rsdemux->byte_segment.position, rsdemux->frame_len, &buffer);
    if (ret != GST_FLOW_OK)
      goto pause;

    /* check buffer size, don't want to read small buffers */
    if (gst_buffer_get_size (buffer) < rsdemux->frame_len)
      goto small_buffer;
  }
  /* and decode the buffer */
  ret = gst_rsdemux_demux_frame (rsdemux, buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pause;

  /* and position ourselves for the next buffer */
  rsdemux->byte_segment.position += rsdemux->frame_len;

done:
  gst_object_unref (rsdemux);

  return;

  /* ERRORS */
parse_header_error:
  {
    GstEvent *event;

    GST_ELEMENT_ERROR (rsdemux, STREAM, DECODE,
        (NULL), ("Error parsing DV header"));
    gst_buffer_unref (buffer);
    gst_pad_pause_task (rsdemux->sinkpad);
    event = gst_event_new_eos ();
    if (rsdemux->segment_seqnum)
      gst_event_set_seqnum (event, rsdemux->segment_seqnum);
    gst_rsdemux_push_event (rsdemux, event);
    goto done;
  }
small_buffer:
  {
    GstEvent *event;

    GST_ELEMENT_ERROR (rsdemux, STREAM, DECODE,
        (NULL), ("Error reading buffer"));
    gst_buffer_unref (buffer);
    gst_pad_pause_task (rsdemux->sinkpad);
    event = gst_event_new_eos ();
    if (rsdemux->segment_seqnum)
      gst_event_set_seqnum (event, rsdemux->segment_seqnum);
    gst_rsdemux_push_event (rsdemux, event);
    goto done;
  }
pause:
  {
    GST_INFO_OBJECT (rsdemux, "pausing task, %s", gst_flow_get_name (ret));
    gst_pad_pause_task (rsdemux->sinkpad);
    if (ret == GST_FLOW_EOS) {
      GST_LOG_OBJECT (rsdemux, "got eos");
      /* so align our position with the end of it, if there is one
       * this ensures a subsequent will arrive at correct base/acc time */
      if (rsdemux->time_segment.rate > 0.0 &&
          GST_CLOCK_TIME_IS_VALID (rsdemux->time_segment.stop))
        rsdemux->time_segment.position = rsdemux->time_segment.stop;
      else if (rsdemux->time_segment.rate < 0.0)
        rsdemux->time_segment.position = rsdemux->time_segment.start;
      /* perform EOS logic */
      if (rsdemux->time_segment.flags & GST_SEEK_FLAG_SEGMENT) {
        GstMessage *message;
        GstEvent *event;

        event = gst_event_new_segment_done (rsdemux->time_segment.format,
            rsdemux->time_segment.position);
        if (rsdemux->segment_seqnum)
          gst_event_set_seqnum (event, rsdemux->segment_seqnum);

        message = gst_message_new_segment_done (GST_OBJECT_CAST (rsdemux),
            rsdemux->time_segment.format, rsdemux->time_segment.position);
        if (rsdemux->segment_seqnum)
          gst_message_set_seqnum (message, rsdemux->segment_seqnum);

        gst_element_post_message (GST_ELEMENT (rsdemux), message);
        gst_rsdemux_push_event (rsdemux, event);
      } else {
        GstEvent *event = gst_event_new_eos ();
        if (rsdemux->segment_seqnum)
          gst_event_set_seqnum (event, rsdemux->segment_seqnum);
        gst_rsdemux_push_event (rsdemux, event);
      }
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GstEvent *event = gst_event_new_eos ();
      /* for fatal errors or not-linked we post an error message */
      GST_ELEMENT_FLOW_ERROR (rsdemux, ret);
      if (rsdemux->segment_seqnum)
        gst_event_set_seqnum (event, rsdemux->segment_seqnum);
      gst_rsdemux_push_event (rsdemux, event);
    }
    goto done;
  }
}

static gboolean
gst_rsdemux_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  GstRSDemux *demux = GST_RSDEMUX (parent);

  switch (mode) {
    case GST_PAD_MODE_PULL:
      if (active) {
        demux->seek_handler = gst_rsdemux_handle_pull_seek;
        res = gst_pad_start_task (sinkpad,
            (GstTaskFunction) gst_rsdemux_loop, sinkpad, NULL);
      } else {
        demux->seek_handler = NULL;
        res = gst_pad_stop_task (sinkpad);
      }
      break;
    case GST_PAD_MODE_PUSH:
      if (active) {
        GST_DEBUG_OBJECT (demux, "activating push/chain function");
        demux->seek_handler = gst_rsdemux_handle_push_seek;
      } else {
        GST_DEBUG_OBJECT (demux, "deactivating push/chain function");
        demux->seek_handler = NULL;
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

      gst_rsdemux_remove_pads (rsdemux);

      if (rsdemux->tag_event) {
        gst_event_unref (rsdemux->tag_event);
        rsdemux->tag_event = NULL;
      }

      break;
    */
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      GstEvent **event_p;

      event_p = &rsdemux->seek_event;
      gst_event_replace (event_p, NULL);
      if (rsdemux->pending_segment)
        gst_event_unref (rsdemux->pending_segment);
      rsdemux->pending_segment = NULL;
      break;
    }
    default:
      break;
  }
  return ret;
}
