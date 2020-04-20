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
};

GType gst_realsense_meta_api_get_type (void);
const GstMetaInfo *gst_realsense_meta_get_info (void);
#define gst_buffer_get_realsense_meta(b) ((GstStringMeta*)gst_buffer_get_meta((b),GST_REALSENSE_META_API_TYPE))

GstRealsenseMeta *gst_buffer_add_realsense_meta(GstBuffer* buffer, 
        const std::string model,
        const std::string serial_number,
        uint exposure, 
        const std::string json_descr
        );

// for python access
// const char* gst_buffer_get_realsense_meta_cstring(GstBuffer* buffer);

G_END_DECLS


#endif //APP_CPP_GST_REALSENSE_META_H
