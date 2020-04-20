
#include <gst/gst.h>
#include <gstrealsensemeta.h>

#include <iostream>

GType gst_realsense_meta_api_get_type (void)
{
    static volatile GType type;

    if (g_once_init_enter (&type)) {
        static const gchar *tags[] = { NULL };
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
    GstRealsenseMeta* dest_meta = gst_buffer_add_realsense_meta(dest, 
            source_meta->cam_model.c_str(),
            source_meta->cam_serial_number.c_str(),
            source_meta->exposure);
    return dest_meta != nullptr;
}

static gboolean gst_realsense_meta_init (GstMeta * meta, gpointer params,
                                      GstBuffer * buffer)
{
    GstRealsenseMeta* emeta = reinterpret_cast<GstRealsenseMeta*>(meta);
    emeta->cam_model.clear();
    emeta->cam_serial_number.clear();
    emeta->exposure = 0;

    return TRUE;
}

static void gst_realsense_meta_free (GstMeta * meta, GstBuffer * buffer)
{
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

GstRealsenseMeta *
gst_buffer_add_realsense_meta (GstBuffer * buffer, 
        const std::string model,
        const std::string serial_number,
        const uint exposure)
{
    g_return_val_if_fail (GST_IS_BUFFER (buffer), nullptr);

    auto meta = 
        reinterpret_cast<GstRealsenseMeta*>(gst_buffer_add_meta(buffer, GST_REALSENSE_META_INFO, nullptr));//reinterpret_cast<gpointer>(const_cast<char*>(data)));

    meta->cam_model = model;
    meta->cam_serial_number = serial_number;
    meta->exposure = exposure;

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
