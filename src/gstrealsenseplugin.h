/*
 */

#ifndef __GST_PLUGIN_REALSENSE_H_
#define __GST_PLUGIN_REALSENSE_H_

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_PLUGIN_TEMPLATE \
  (gst_plugin_template_get_type())
#define GST_PLUGIN_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLUGIN_TEMPLATE,GstPluginTemplate))
#define GST_PLUGIN_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLUGIN_TEMPLATE,GstPluginTemplateClass))
#define GST_IS_PLUGIN_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLUGIN_TEMPLATE))
#define GST_IS_PLUGIN_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLUGIN_TEMPLATE))

// TODO set these in CMakeLists.txt
#define PACKAGE_VERSION 0
#define GST_LICENSE "none" 
#define GST_PACKAGE_NAME "realsense-plugin"
#define GST_PACKAGE_ORIGIN "WKD.SMRT"

typedef struct _GstPluginTemplate      GstPluginTemplate;
typedef struct _GstPluginTemplateClass GstPluginTemplateClass;

struct _GstPluginTemplate
{
  GstElement element;

  GstPad *srcpad;

  gboolean silent;
};

struct _GstPluginTemplateClass 
{
  GstElementClass parent_class;
};

GType gst_plugin_template_get_type (void);

G_END_DECLS

#endif /* __GST_PLUGIN_REALSENSE_H_ */
