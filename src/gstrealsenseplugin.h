/*
 */

#ifndef __GST_PLUGIN_REALSENSE_H_
#define __GST_PLUGIN_REALSENSE_H_

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <librealsense2/rs.hpp>

G_BEGIN_DECLS

#define GST_TYPE_REALSENSE_SRC (gst_realsensesrc_get_type())
#define GST_REALSENSE_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REALSENSE_SRC, GstRealsenseSrc))
#define GST_REALSENSE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REALSENSE_SRC,GstRealsenseTemplateClass))
#define GST_IS_REALSENSE_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REALSENSE_SRC))
#define GST_IS_REALSENSE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REALSENSE_SRC))

// TODO set these in CMakeLists.txt
#define PACKAGE_VERSION 0
#define GST_LICENSE "none" 
#define GST_PACKAGE_NAME "gstrealsensesrc"
#define GST_PACKAGE_ORIGIN "WKD.SMRT"
#define PLUGIN_DESCRIPTION "RealSense source plugin"

typedef struct _GstRealsenseSrc GstRealsenseSrc;
typedef struct _GstRealsenseSrcClass GstRealsenseSrcClass;

using rs_pipe_ptr = std::unique_ptr<rs2::pipeline>;

struct _GstRealsenseSrc
{
  GstPushSrc base_realsensesrc;

  /* camera handle */
  rs_pipe_ptr rs_pipeline = nullptr;

  gchar error_string[256];
  /* properties - may or may not need all of these*/
  guint num_capture_buffers;
  guint cam_index;
  gint timeout;

  GstClockTime acq_start_time;
  guint32 last_frame_count;
  guint32 total_dropped_frames;

  GstCaps *caps;
  gint height;
  gint gst_stride;
  gint rs_stride;

  gboolean stop_requested;
};

struct _GstRealsenseSrcClass 
{
  GstPushSrcClass base_realsense_class;
};

GType gst_realsensesrc_get_type (void);

G_END_DECLS

#endif /* __GST_PLUGIN_REALSENSE_H_ */
