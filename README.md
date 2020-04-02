# realsense-gstreamer

GStreamer source plugin for the Intel RealSense line of cameras. 

The plugin is actually two elements, a pure source and a demuxer. The source is set up as a GstPushSrc, based on [gst-vision-plugins](https://github.com/joshdoe/gst-plugins-vision) and GstVideoTestSrc. The demuxer is based on GstDVDemux from the gst-plugins-good package. 

The source element combines color and depth channels  and IMU into a single buffer passed to out thru its source pad. The demuxer receives that buffer on its sink pad and splits it into color, depth and IMU buffers and and passes the buffers into the respective source pads. The IMU pad is configured as an audio pad with 6 channels of 32-bit floating point data.

The primary reason for this configuration is that GstBaseSrc, which GstPushSrc inherits, allows for only a single source pad. The use of the demuxer is not required. A downstream element may demux the itself buffer. This may be useful for processing that requires synchronized color and depth information.

[RealSense Examples](https://github.com/IntelRealSense/librealsense/tree/master/examples)
[RealSense Reference](https://dev.intelrealsense.com/docs/api-architecture)

## Supported Models
D435i is currently supported.

## To Do
### Source
- control debug output and messages
- Create bin element
- Test application
- Add metadata
    - What metadata is needed?
- Add IMU data
- src/rsmux.hpp:64:        /* TODO: use allocator or use from pool if that's more efficient or safer*/
- src/rsmux.hpp:82:        // TODO refactor this section into cleaner code
- src/rsmux.hpp:89:        /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:2: * TODO add license
- src/gstrealsensedemux.cpp:205:  // TODO Handle any necessary src queries
- src/gstrealsensedemux.cpp:221:  // TODO Handle any sink queries
- src/gstrealsensedemux.cpp:317:    // TODO handle src pad events here
- src/gstrealsensedemux.cpp:454:  // TODO What do we need to do in _flush?
- src/gstrealsenseplugin.cpp:334:          // FIXME Not exact format match
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
- When aligning to the depth frame, some areas of rgb frame are blacked out. It's not clear with this is a RealSense bug or problem with the plugins. 