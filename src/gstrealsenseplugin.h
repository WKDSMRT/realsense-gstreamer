/*
 */

#ifndef __GST_REALSENSESRC_H__
#define __GST_REALSENSESRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <librealsense2/rs.hpp>

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

enum Align
{
  None,
  Color,
  Depth
};

enum StreamType
{
  StreamColor,
  StreamDepth,
  StreamMux // Color and depth crammed into the same buffer
};

struct RSHeader {
  gint color_height;
  gint color_width;
  gint color_stride;
  GstVideoFormat color_format;
  gint depth_height;
  gint depth_width;
  gint depth_stride;
  GstVideoFormat depth_format;
};

struct _GstRealsenseSrc
{
  GstPushSrc element;

  GstVideoInfo info; /* protected by the object or stream lock */

  gboolean silent;

  GstCaps *caps;
  gint height;
  gint gst_stride;
  
  // Realsense vars
  rs_pipe_ptr rs_pipeline = nullptr;
  rs_aligner_ptr aligner = nullptr;

  // Properties
  Align align = Align::None;
  guint64 serial_number = 0;
  StreamType stream_type = StreamType::StreamDepth;
};

struct _GstRealsenseSrcClass 
{
  GstPushSrcClass parent_class;
};

GType gst_realsense_src_get_type (void);

G_END_DECLS

#endif /* __GST_REALSENSESRC_H__ */
