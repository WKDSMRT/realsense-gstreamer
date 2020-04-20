plugin_cpp_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', project_version)
cdata.set_quoted('PACKAGE', 'gst-realsensesrc-plugin')

# used in plugin creation
cdata.set_quoted('GST_LICENSE', 'LGPL') # FIXME with real license
cdata.set_quoted('GST_API_VERSION', gst_api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer RealSense Source Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/WKDSMRT/realsense-gstreamer')
configure_file(output : 'config.h', configuration : cdata)

gstvideo_dep = dependency('gstreamer-video-1.0',
    fallback: ['gst-plugins-base', 'video_dep'])

gstaudio_dep = dependency('gstreamer-audio-1.0',
    fallback: ['gst-plugins-base', 'audio_dep'])

realsense_dep = dependency('realsense2')

# Plugin 1
plugin_sources = [
  'gstrealsenseplugin.cpp',
  'gstrealsensedemux.cpp',
  'rsmux.hpp',
  'gstrealsensemeta.cpp'
  ]

gstpluginexample = library('gstrealsensesrc',
  plugin_sources,
  cpp_args: [plugin_cpp_args, '-std=c++1z'],
  dependencies : [gst_dep, gstvideo_dep, gstaudio_dep, realsense_dep],
  install : true,
  install_dir : plugins_install_dir,
)