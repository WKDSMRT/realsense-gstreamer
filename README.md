# realsense-gstreamer

GStreamer source plugin for the Intel RealSense line of cameras. 

The plugin is set up as a GstPushSrc, based on [gst-vision-plugins](https://github.com/joshdoe/gst-plugins-vision) and GstVideoTestSrc.

[RealSense Examples](https://github.com/IntelRealSense/librealsense/tree/master/examples)
[RealSense Reference](https://dev.intelrealsense.com/docs/api-architecture)

## Supported Models
None yet. D435i will be the initial focus.

## To Do
### Source
- Test alignment property
    - need depth data first
- Figure out what's wrong with serial number property
- Add Depth channel
- Add IMU data
- Add metadata
- add clocking
- src/gstrealsenseplugin.cpp:85:// TODO update formats
- src/gstrealsenseplugin.cpp:204:    // TODO properties
- src/gstrealsenseplugin.cpp:251:  /* TODO: use allocator or use from pool */
- src/gstrealsenseplugin.cpp:256:  // TODO: update log
- src/gstrealsenseplugin.cpp:266:  /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:317:  /* TODO: set timestamps */
- src/gstrealsenseplugin.cpp:403:      // TODO Handle alignment here
- src/gstrealsenseplugin.cpp:424:      // TODO need to set up format here
- set plugin defines specific to WKD.SMRT/RealSense
### Tests
- Test application in Python or C++

## Known Issues
- You must manually specify the plugin location. For example:
```
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0/
export GST_PLUGIN_PATH
```
- If the camera is not connected the plugin will fail to initialize. Running gst-inspect will give an error about "no valid klass field"

```bash
> gst-inspect-1.0 mbuild/src/libgstrealsensesrc.so

(gst-inspect-1.0:24981): GStreamer-WARNING **: 20:31:47.666: Element factory metadata for 'realsensesrc' has no valid klass field
Could not load plugin file: File "mbuild/src/libgstrealsensesrc.so" appears to be a GStreamer plugin, but it failed to initialize
```
- serial number property segfaults. need to figure out how to pass a string property. Alternatively could pass as uint.
