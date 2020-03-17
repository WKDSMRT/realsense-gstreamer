/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_RSDEMUX_H__
#define __GST_RSDEMUX_H__

#include <gst/gst.h>
// #include <libdv/dv.h>
// #include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RSDEMUX \
  (gst_rsdemux_get_type())
#define GST_RSDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RSDEMUX,GstRSDemux))
#define GST_RSDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RSDEMUX,GstRSDemuxClass))
#define GST_IS_RSDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RSDEMUX))
#define GST_IS_RSDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RSDEMUX))


typedef struct _GstRSDemux GstRSDemux;
typedef struct _GstRSDemuxClass GstRSDemuxClass;

typedef gboolean (*GstDVDemuxSeekHandler) (GstRSDemux *demux, GstPad * pad, GstEvent * event);

// TODO review all members
struct _GstRSDemux {
  GstElement     element;

  GstPad        *sinkpad;
  GstPad        *colorsrcpad = nullptr;
  GstPad        *depthsrcpad = nullptr;

// TODO put encode/decode all in a single file/class
// dv_decoder_t  *decoder;

  /* video params */
  gint           in_height;
  gint           in_width;
  gint           in_stride_bytes;

  gint           color_height;
  gint           color_width;
  gint           color_stride_bytes;

  gint           depth_height;
  gint           depth_width;
  gint           depth_stride_bytes;
  gint           frame_count = 0;

// TODO What do these values do? Are they needed?
  gint           frame_len;

  gboolean       new_media;
  int            frames_since_new_media;
};

struct _GstRSDemuxClass 
{
  GstElementClass parent_class;
};

GType gst_rsdemux_get_type (void);

G_END_DECLS

#endif /* __GST_RSDEMUX_H__ */
