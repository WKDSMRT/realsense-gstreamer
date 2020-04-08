/* GStreamer RealSense is a set of plugins to acquire frames from 
 * Intel RealSense cameras into GStreamer pipeline.
 * Copyright (C) <2020> Tim Connelly/WKD.SMRT <timpconnelly@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

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
  int accel_format;
  int gyro_format;

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
    if (accel_format != rhs.accel_format)
      return true;
    if (gyro_format != rhs.gyro_format)
      return true;
    return false;
  }

  bool operator==(const RSHeader& rhs)
  {
    return !(*this != rhs);
  }
};

#endif // __GST_RSCOMMON_H__