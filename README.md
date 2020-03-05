# realsense-gstreamer

GStreamer source plugin for the Intel RealSense line of cameras. 

The plugin is set up as a GstPushSrc, based on [gst-vision-plugins](https://github.com/joshdoe/gst-plugins-vision) and GstVideoTestSrc.

[RealSense Examples](https://github.com/IntelRealSense/librealsense/tree/master/examples)
[RealSense Reference](https://dev.intelrealsense.com/docs/api-architecture)

## Supported Models
D435i is currently supported with limitations.

## To Do
### Source
- add clocking
- Add metadata
- Test alignment property
    - Something is coming thru. Need demuxer to see data
- Add Depth channel
    - I've attempted to extend the output buffer and pack the depth data into that buffer. The consumer will need to unpack it. The format, frame size, stride for color and depth will need to be passed thru the pipeline 
- Add IMU data
- src/gstrealsenseplugin.cpp:86:// TODO update formats
- src/gstrealsenseplugin.cpp:210:    // TODO properties
- src/gstrealsenseplugin.cpp:258:  /* TODO: use allocator or use from pool if that's more efficient or safer*/
- src/gstrealsenseplugin.cpp:262:  // TODO: update log
- src/gstrealsenseplugin.cpp:271:  // TODO refactor this section into cleaner code
- src/gstrealsenseplugin.cpp:276:      /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:303:      /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:347:    /* TODO: set timestamps */
- set plugin defines specific to WKD.SMRT/RealSense
- Maybe add capability to generate synthetic data if no camera is connected.
    - Should be develop mode only

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
