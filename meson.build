project('realsensesrc', 'cpp', version : '0.0.1', license : 'LGPL')

#plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_install_dir = '/usr/local/lib/gstreamer-1.0/'
#cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])

subdir('src')
