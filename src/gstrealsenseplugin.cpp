/* GStreamer RealSense is a set of plugins to acquire frames from 
 * Intel RealSense cameras into GStreamer pipeline.
 * Copyright (C) <2020> Tim Connelly/WKD.SMRT <timpconnelly@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-realsensesrc
 *
 * Source element for Intel RealSense camera. This source takes the 
 * frame_set from the RealSense SDK and multiplexes it into a single buffer
 * that is pushed out on the source pad. Downstream elements may receive this buffer
 * and demux it themselves (use RSMux::demux) or use the rsdemux element to split
 * the color and depth into separate buffers.
 *
 * Example launch line
 * |[
 * gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
 * ]|
 * 
 * The example pipeline will display muxed data, so the depth and IMU data 
 * will not be displayed correctly. See rsdemux element to split the sources
 * into seperate streams.
 * 
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "gstrealsensesrc.h"
#include "gstrealsensedemux.h"

#ifndef PACKAGE
#define PACKAGE "realsensesrc"
#endif

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
realsensesrc_init (GstPlugin * realsensesrc)
{ 
  if (!gst_element_register (realsensesrc, "rsdemux", GST_RANK_MARGINAL, GST_TYPE_RSDEMUX))
    return FALSE;

  if(!gst_element_register (realsensesrc, "realsensesrc", GST_RANK_PRIMARY, GST_TYPE_REALSENSESRC))
    return FALSE;

  return TRUE;
}

/* gstreamer looks for this structure to register realsensesrc */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    realsensesrc,
    "Realsense plugin",
    realsensesrc_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
