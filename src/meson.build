plugin_cpp_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-template-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer template Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

gstvideo_dep = dependency('gstreamer-video-1.0',
    fallback: ['gst-plugins-base', 'video_dep'])

realsense_dep = dependency('realsense2')

# Plugin 1
plugin_sources = [
  'gstrealsenseplugin.cpp'
  ]

gstpluginexample = library('gstrealsensesrc',
  plugin_sources,
  cpp_args: plugin_cpp_args,
  dependencies : [gst_dep, gstvideo_dep, realsense_dep],
  install : true,
  install_dir : plugins_install_dir,
)