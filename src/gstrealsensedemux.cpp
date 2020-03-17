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
#include "gstrealsenseplugin.h"

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

  rsdemux->new_media = FALSE;
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
#if AUDIO_SRC                
          else if (pad == rsdemux->audiosrcpad)
            *dest_value = gst_util_uint64_scale_int (src_value,
                2 * rsdemux->frequency * rsdemux->channels, GST_SECOND);
#endif                
          break;
        case GST_FORMAT_DEFAULT:
          if (pad == rsdemux->colorsrcpad || pad == rsdemux->depthsrcpad) {
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
  // GstRSDemux *rsdemux = GST_RSDEMUX (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      // GstFormat format;
      // gint64 end;
      // GstQuery *pquery;

      // /* First ask the peer in the original format */
      // if (!gst_pad_peer_query (rsdemux->sinkpad, query)) {
      //   /* get target format */
      //   gst_query_parse_duration (query, &format, NULL);

      //   /* Now ask the peer in BYTES format and try to convert */
      //   pquery = gst_query_new_duration (GST_FORMAT_BYTES);
      //   if (!gst_pad_peer_query (rsdemux->sinkpad, pquery)) {
      //     gst_query_unref (pquery);
      //     goto error;
      //   }

      //   /* get peer total length */
      //   gst_query_parse_duration (pquery, NULL, &end);
      //   gst_query_unref (pquery);

      //   /* convert end to requested format */
      //   if (end != -1) {
      //     if (!(res = gst_rsdemux_sink_convert (rsdemux,
      //                 GST_FORMAT_BYTES, end, format, &end))) {
      //       goto error;
      //     }
      //     gst_query_set_duration (query, format, end);
      //   }
      // }
      // break;
    }
    case GST_QUERY_CONVERT:
    {
      // GstFormat src_fmt, dest_fmt;
      // gint64 src_val, dest_val;

      // gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      // if (!(res =
      //         gst_rsdemux_src_convert (rsdemux, pad, src_fmt, src_val,
      //             dest_fmt, &dest_val)))
      //   goto error;
      // gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      // break;
    }
    case GST_QUERY_SEEKING:
    {
      // GstFormat fmt;
      // GstQuery *peerquery;
      // gboolean seekable;

      // gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);

      // /* We can only handle TIME seeks really */
      // if (fmt != GST_FORMAT_TIME) {
      //   gst_query_set_seeking (query, fmt, FALSE, -1, -1);
      //   break;
      // }

      // /* First ask upstream */
      // if (gst_pad_peer_query (rsdemux->sinkpad, query)) {
      //   gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
      //   if (seekable) {
      //     res = TRUE;
      //     break;
      //   }
      // }

      // res = TRUE;

      // peerquery = gst_query_new_seeking (GST_FORMAT_BYTES);
      // seekable = gst_pad_peer_query (rsdemux->sinkpad, peerquery);

      // if (seekable)
      //   gst_query_parse_seeking (peerquery, NULL, &seekable, NULL, NULL);
      // gst_query_unref (peerquery);

      // if (seekable) {
      //   peerquery = gst_query_new_duration (GST_FORMAT_TIME);
      //   seekable = gst_rsdemux_src_query (pad, parent, peerquery);

      //   if (seekable) {
      //     gint64 duration;

      //     gst_query_parse_duration (peerquery, NULL, &duration);
      //     gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0, duration);
      //   } else {
      //     gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);
      //   }

      //   gst_query_unref (peerquery);
      // } else {
      //   gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);
      // }
      // break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;

  /* ERRORS */
// error:
//   {
//     GST_DEBUG ("error source query");
//     return FALSE;
//   }
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
      // GstFormat src_fmt, dest_fmt;
      // gint64 src_val, dest_val;

      // gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      // if (!(res =
      //         gst_rsdemux_sink_convert (rsdemux, src_fmt, src_val, dest_fmt,
      //             &dest_val)))
      //   goto error;
      // gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      // break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;

  /* ERRORS */
// error:
//   {
//     GST_DEBUG ("error handling sink query");
//     return FALSE;
//   }
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

/* convert a pair of values on the srcpad to a pair of
 * values on the sinkpad 
 */
// static gboolean
// gst_rsdemux_convert_src_to_sink (GstRSDemux * rsdemux, GstPad * pad,
//     GstFormat src_format, gint64 src_start, gint64 src_stop,
//     GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
// {
//   GstFormat conv;
//   gboolean res;

//   conv = GST_FORMAT_TIME;
//   /* convert to TIME intermediate format */
//   if (!(res = gst_rsdemux_convert_src_pair (rsdemux, pad,
//               src_format, src_start, src_stop, conv, dst_start, dst_stop))) {
//     /* could not convert format to time offset */
//     goto done;
//   }
//   /* convert to dst format on sinkpad */
//   if (!(res = gst_rsdemux_convert_sink_pair (rsdemux,
//               conv, *dst_start, *dst_stop, dst_format, dst_start, dst_stop))) {
//     /* could not convert format to time offset */
//     goto done;
//   }
// done:
//   return res;
// }

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
// static gboolean
// gst_rsdemux_handle_push_seek (GstRSDemux * rsdemux, GstPad * pad,
//     GstEvent * event)
// {
//   gboolean res = FALSE;
//   gdouble rate;
//   GstSeekFlags flags;
//   GstFormat format;
//   GstSeekType cur_type, stop_type;
//   gint64 cur, stop;
//   gint64 start_position, end_position;
//   GstEvent *newevent;

//   gst_event_parse_seek (event, &rate, &format, &flags,
//       &cur_type, &cur, &stop_type, &stop);

//   /* First try if upstream can handle time based seeks */
//   if (format == GST_FORMAT_TIME)
//     res = gst_pad_push_event (rsdemux->sinkpad, gst_event_ref (event));

//   if (!res) {
//     /* we convert the start/stop on the srcpad to the byte format
//      * on the sinkpad and forward the event */
//     res = gst_rsdemux_convert_src_to_sink (rsdemux, pad,
//         format, cur, stop, GST_FORMAT_BYTES, &start_position, &end_position);
//     if (!res)
//       goto done;

//     /* now this is the updated seek event on bytes */
//     newevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
//         cur_type, start_position, stop_type, end_position);
//     gst_event_set_seqnum (newevent, gst_event_get_seqnum (event));

//     res = gst_pad_push_event (rsdemux->sinkpad, newevent);
//   }
// done:
//   return res;
// }

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

  if (rsdemux->new_media)// || rsdemux->discont)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

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

// static gboolean
// gst_rsdemux_is_new_media (GstRSDemux * rsdemux, GstBuffer * buffer)
// {
//   guint8 *data;
//   GstMapInfo map;
//   int aaux_offset;
//   int dif;
//   int n_difs;

// //   n_difs = rsdemux->decoder->num_dif_seqs;

//   gst_buffer_map (buffer, &map, GST_MAP_READ);
//   data = map.data;
//   for (dif = 0; dif < n_difs; dif++) {
//     if (dif & 1) {
//       aaux_offset = (dif * 12000) + (6 + 16 * 1) * 80 + 3;
//     } else {
//       aaux_offset = (dif * 12000) + (6 + 16 * 4) * 80 + 3;
//     }
//     if (data[aaux_offset + 0] == 0x51) {
//       if ((data[aaux_offset + 2] & 0x80) == 0) {
//         gst_buffer_unmap (buffer, &map);
//         return TRUE;
//       }
//     }
//   }

//   gst_buffer_unmap (buffer, &map);
//   return FALSE;
// }
#include <stdexcept>

/* takes ownership of buffer */
static GstFlowReturn
gst_rsdemux_demux_frame (GstRSDemux * rsdemux, GstBuffer * buffer)
{
  // GstClockTime next_ts;
  GstFlowReturn vret, ret;
  // GstMapInfo map;
  // guint64 duration;
  // int frame_number;

  // gst_buffer_map (buffer, &map, GST_MAP_READ);
//   dv_parse_packs (rsdemux->decoder, map.data);
  // gst_buffer_unmap (buffer, &map);
  // rsdemux->new_media = FALSE;
  // if (gst_rsdemux_is_new_media (rsdemux, buffer) &&
  //     rsdemux->frames_since_new_media > 2) {
  //   rsdemux->new_media = TRUE;
  //   rsdemux->frames_since_new_media = 0;
  // }
  // rsdemux->frames_since_new_media++;
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
