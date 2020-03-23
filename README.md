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
    - Mostly works. See known issue below.
- Add IMU data
- src/gstrealsenseplugin.cpp:257:  /* TODO: use allocator or use from pool if that's more efficient or safer*/
- src/gstrealsenseplugin.cpp:284:      /* TODO: use orc_memcpy */
- src/gstrealsenseplugin.cpp:314:      /* TODO: use orc_memcpy */
- src/gstrealsensedemux.cpp:219:  // TODO Handle any necessary src queries
- src/gstrealsensedemux.cpp:235:  // TODO Handle any sink queries
- src/gstrealsensedemux.cpp:333:    // TODO handle src pad events here
- src/gstrealsensedemux.cpp:508:  // TODO What do we need to do in _flush?
- src/gstrealsenseplugin.cpp:264:    src->info.finfo->format, // FIXME won't be correct if we only have depth
- src/gstrealsenseplugin.cpp:268:    GST_VIDEO_FORMAT_GRAY16_LE //FIXME could be _LE or _BE
- src/gstrealsenseplugin.cpp:530:          // FIXME Not exact format match
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