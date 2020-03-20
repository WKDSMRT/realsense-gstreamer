#ifndef __GST_RSCOMMON_H__
#define __GST_RSCOMMON_H__

enum StreamType
{
  StreamColor,
  StreamDepth,
  StreamMux // Color and depth crammed into the same buffer
};

enum Align
{
  None,
  Color,
  Depth
};

struct RSHeader {
  int color_height;
  int color_width;
  int color_stride;
  int color_format;
  int depth_height;
  int depth_width;
  int depth_stride;
  int depth_format;
};

#endif // __GST_RSCOMMON_H__