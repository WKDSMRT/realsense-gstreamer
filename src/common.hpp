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

  bool operator!=(const RSHeader &rhs)
  {
    if (color_height != rhs.color_height)
      return true;
    if (color_width != rhs.color_width)
      return true;
    if (color_stride != rhs.color_stride)
      return true;
    if (color_format != rhs.color_format)
      return true;
    if (depth_height != rhs.depth_height)
      return true;
    if (depth_width != rhs.depth_width)
      return true;
    if (depth_stride != rhs.depth_stride)
      return true;
    if (depth_format != rhs.depth_format)
      return true;

    return false;
  }

  bool operator==(const RSHeader& rhs)
  {
    return !(*this != rhs);
  }
};

#endif // __GST_RSCOMMON_H__