project('realsensesrc', 'cpp', 
    version : '0.1.0',
    meson_version : '>= 0.53',
     license : 'LGPL')

libdir = get_option('libdir')
plugins_install_dir = join_paths(libdir, 'gstreamer-1.0')

project_version = meson.project_version()

gst_api_version = '1.0'

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])

subdir('src')
