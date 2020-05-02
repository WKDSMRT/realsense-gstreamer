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

#ifndef __GST_REALSENSESRC_H__
#define __GST_REALSENSESRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <librealsense2/rs.hpp>

#include "common.hpp"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_REALSENSESRC \
  (gst_realsense_src_get_type())
#define GST_REALSENSESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REALSENSESRC,GstRealsenseSrc))
#define GST_REALSENSESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REALSENSESRC,GstRealsenseSrcClass))
#define GST_IS_REALSENSESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REALSENSESRC))
#define GST_IS_REALSENSESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REALSENSESRC))

typedef struct _GstRealsenseSrc      GstRealsenseSrc;
typedef struct _GstRealsenseSrcClass GstRealsenseSrcClass;

using rs_pipe_ptr = std::unique_ptr<rs2::pipeline>;
using rs_aligner_ptr = std::unique_ptr<rs2::align>;
constexpr const auto DEFAULT_PROP_CAM_SN = 0;

struct _GstRealsenseSrc
{
  GstPushSrc element;

  GstVideoInfo info; /* protected by the object or stream lock */

  gboolean silent;
  gboolean stop_requested = FALSE;

  GstCaps *caps;
  gint height;
  gint gst_stride;
  GstVideoFormat color_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoFormat depth_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstAudioFormat accel_format = GST_AUDIO_FORMAT_UNKNOWN;
  GstAudioFormat gyro_format = GST_AUDIO_FORMAT_UNKNOWN;
  GstClockTime prev_time = 0;
  guint64 frame_count = 0;

  // Realsense vars
  rs_pipe_ptr rs_pipeline = nullptr;
  rs_aligner_ptr aligner = nullptr;
  bool has_imu = false;
  
  // Properties
  Align align = Align::None;
  guint64 serial_number = 0;
  StreamType stream_type = StreamType::StreamDepth;
  bool imu_on = true;
};

struct _GstRealsenseSrcClass 
{
  GstPushSrcClass parent_class;
};

GType gst_realsense_src_get_type (void);

G_END_DECLS

#endif /* __GST_REALSENSESRC_H__ */
