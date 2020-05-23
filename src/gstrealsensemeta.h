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

#ifndef APP_CPP_GST_REALSENSE_META_H
#define APP_CPP_GST_REALSENSE_META_H


#include <gst/video/video.h>

#include <string>

G_BEGIN_DECLS

#define GST_REALSENSE_META_API_TYPE (gst_realsense_meta_api_get_type())
#define GST_REALSENSE_META_INFO  (gst_realsense_meta_get_info())
typedef struct _GstRealsenseMeta GstRealsenseMeta;

struct _GstRealsenseMeta {
  GstMeta            meta;

  std::string* cam_model;
  std::string* cam_serial_number;
  std::string* json_descr; // generic json descriptor
  uint exposure = 0;
  float depth_units 0.f;
};

GType gst_realsense_meta_api_get_type (void);
const GstMetaInfo *gst_realsense_meta_get_info (void);
#define gst_buffer_get_realsense_meta(b) ((GstRealsenseMeta*)gst_buffer_get_meta((b),GST_REALSENSE_META_API_TYPE))

GstRealsenseMeta *gst_buffer_add_realsense_meta(GstBuffer* buffer, 
        const std::string model,
        const std::string serial_number,
        uint exposure, 
        const std::string json_descr,
        float depth_units
        );

// for python access
// const char* gst_buffer_get_realsense_meta_cstring(GstBuffer* buffer);

G_END_DECLS


#endif //APP_CPP_GST_REALSENSE_META_H
