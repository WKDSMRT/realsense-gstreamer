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

#include <gst/gst.h>
#include <gst/video/video.h>

#include <gstrealsensemeta.h>

#include <iostream>

GType gst_realsense_meta_api_get_type (void)
{
    static volatile GType type;

    if (g_once_init_enter (&type)) {
        static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, "sensor", NULL };
        GType _type = gst_meta_api_type_register ("GstRealsenseMetaAPI", tags);
        GST_INFO ("registering");
        g_once_init_leave (&type, _type);
    }
    return type;
}


static gboolean gst_realsense_meta_transform (GstBuffer * dest, GstMeta * meta,
                                           GstBuffer * buffer, GQuark type, gpointer data)
{
    GstRealsenseMeta* source_meta = reinterpret_cast<GstRealsenseMeta*>(meta);
    
    if(GST_META_TRANSFORM_IS_COPY(type))
    {
        GstRealsenseMeta* dest_meta = gst_buffer_add_realsense_meta(
            dest, 
            *source_meta->cam_model,
            *source_meta->cam_serial_number,
            source_meta->exposure,
            *source_meta->json_descr,
            source_meta->depth_units);
        if(dest_meta == nullptr)
            return false;
    }
    else
    {
        return false;
    }
    
    return false;
}

static gboolean gst_realsense_meta_init (GstMeta * meta, gpointer params,
                                      GstBuffer * buffer)
{
    GstRealsenseMeta* emeta = reinterpret_cast<GstRealsenseMeta*>(meta);
    emeta->cam_model = nullptr;
    emeta->cam_serial_number = nullptr;
    emeta->json_descr = nullptr;
    emeta->exposure = 0;
    emeta->depth_units = 0.f;
    return TRUE;
}

static void gst_realsense_meta_free (GstMeta * meta, GstBuffer * buffer)
{
    auto rsmeta = reinterpret_cast<GstRealsenseMeta*>(meta);
    delete rsmeta->cam_model;
    delete rsmeta->cam_serial_number;
    delete rsmeta->json_descr;
    rsmeta->exposure = 0;
    rsmeta->depth_units = 0.f;
}

const GstMetaInfo * gst_realsense_meta_get_info (void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
        const GstMetaInfo *mi =
                gst_meta_register (GST_REALSENSE_META_API_TYPE,
                                   "GstRealsenseMeta",
                                   sizeof (GstRealsenseMeta),
                                   gst_realsense_meta_init,
                                   gst_realsense_meta_free,
                                   gst_realsense_meta_transform);
        g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) mi);
    }
    return meta_info;
}

GstRealsenseMeta* gst_buffer_add_realsense_meta (GstBuffer * buffer, 
        const std::string model,
        const std::string serial_number,
        const uint exposure,
        const std::string json_descr,
        float depth_units)
{
    g_return_val_if_fail (GST_IS_BUFFER (buffer), nullptr);

    auto meta = 
        reinterpret_cast<GstRealsenseMeta*>(gst_buffer_add_meta(buffer, GST_REALSENSE_META_INFO, nullptr));

    meta->cam_model = new std::string(model);
    meta->cam_serial_number = new std::string(serial_number);
    meta->json_descr = new std::string(json_descr);
    meta->exposure = exposure;
    meta->depth_units = depth_units;

    return meta;
}

// const char* gst_buffer_get_string_meta_cstring(GstBuffer* buffer)
// {
//     GstRealsenseMeta* meta = gst_buffer_get_string_meta(buffer);
//     if (meta != nullptr) {
//         return meta->str->c_str();
//     }
//     else {
//         return nullptr;
//     }
// }
