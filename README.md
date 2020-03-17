# realsense-gstreamer

GStreamer source plugin for the Intel RealSense line of cameras. 

The plugin is actually two elements, a pure source and a demuxer. The source is set up as a GstPushSrc, based on [gst-vision-plugins](https://github.com/joshdoe/gst-plugins-vision) and GstVideoTestSrc. The demuxer is based on GstDVDemux from the gst-plugins-good package. 

The source element combines color and depth channels into a single buffer passed to its source pad. The demuxer receives that buffer on its sink pad and splits it into color and depth buffers and passes the buffers into the respective source pads. 

The primary reason for this configuration is that GstBaseSrc, which GstPushSrc inherits, allows for only a single source pad. The use of the demuxer is not required. A downstream element may demux the itself buffer. This may be useful for processing that requires synchronized color and depth information.

[RealSense Examples](https://github.com/IntelRealSense/librealsense/tree/master/examples)
[RealSense Reference](https://dev.intelrealsense.com/docs/api-architecture)

## Supported Models
D435i is currently supported.

## To Do
### Source
- Add metadata
- Test alignment property
    - Something is coming thru. Need demuxer to see data
- Add Depth channel
    - I've attempted to extend the output buffer and pack the depth data into that buffer. The consumer will need to unpack it. The format, frame size, stride for color and depth will need to be passed thru the pipeline 
- Add IMU data
- src/gstrealsensedemux.h:47:// TODO review all members
- src/gstrealsensedemux.h:54:// TODO audio becomes IMU stream
- src/gstrealsensedemux.h:60:// TODO put encode/decode all in a single file/class
- src/gstrealsensedemux.h:82:// TODO What do these values do? Are they needed?
- src/gstrealsenseplugin.cpp:87:// TODO update formats
- src/gstrealsenseplugin.cpp:208:    // TODO properties
- src/gstrealsenseplugin.cpp:257:  /* TODO: use allocator or use from pool if that's more efficient or safer*/
- src/gstrealsenseplugin.cpp:278:  // TODO refactor this section into cleaner code
- src/gstrealsenseplugin.cpp:284:      /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:314:      /* TODO: use orc_memcpy */
- src/gstrealsensedemux.cpp:1125:  // TODO create colorbuf and depth buf and fill them
- src/gstrealsensedemux.cpp:1150:  // TODO What is duration? some sort of timestamp?
- src/gstrealsensedemux.cpp:1321:  // TODO What do we need to do in _flush?
- set plugin defines specific to WKD.SMRT/RealSense
- Maybe add capability to generate synthetic data if no camera is connected.
    - Should be develop mode only

### Tests
- Test application in Python or C++
- The source may be run using gst-launch
```
gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
```

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
