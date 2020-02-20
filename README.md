# realsense-gstreamer

GStreamer source plugin for the Intel RealSense line of cameras. 

The plugin is set up as a GstPushSrc, based on [gst-vision-plugins](https://github.com/joshdoe/gst-plugins-vision) and GstVideoTestSrc.

[RealSense Examples](https://github.com/IntelRealSense/librealsense/tree/master/examples)
[RealSense Reference](https://dev.intelrealsense.com/docs/api-architecture)

## Supported Models
- D435i 
    - RGB stream is the default. A property will be added to set stream mode.
    - Support for Depth and IMU will be added

## To Do
### Source
- src/gstrealsenseplugin.cpp:83:// TODO update formats
- src/gstrealsenseplugin.cpp:196:    // TODO properties
- src/gstrealsenseplugin.cpp:209:    // TODO properties
- src/gstrealsenseplugin.cpp:224:  /* TODO: use allocator or use from pool */
- src/gstrealsenseplugin.cpp:229:  // TODO: update log
- src/gstrealsenseplugin.cpp:239:  /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:289:  /* TODO: set timestamps */
- src/gstrealsenseplugin.cpp:339:      // TODO need to set up format here

### Tests
- Test application in Python or C++
- The source may be run using gst-launch
```
gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
```

## Known Issues
