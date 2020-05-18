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

#ifndef __GST_RSMUX_H__
#define __GST_RSMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <librealsense2/rs.hpp>

#include "common.hpp"

#include <iostream>
#include <tuple>
#include <cstring>

using buf_tuple = std::tuple<GstBuffer*, GstBuffer*, GstBuffer*>;

class RSMux 
{
public:
    template <typename Element>
    static RSHeader GetRSHeader(Element* src, GstBuffer* buffer)
    {               
        RSHeader header;
        RSHeader* in;
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);
        in = reinterpret_cast<RSHeader*>(map.data);

        header.color_height = in->color_height;
        header.color_width = in->color_width;
        header.color_stride = in->color_stride;
        header.color_format = in->color_format;
        header.depth_height = in->depth_height;
        header.depth_width = in->depth_width;
        header.depth_stride = in->depth_stride;
        header.depth_format = in->depth_format;
        header.accel_format = in->accel_format;
        header.gyro_format = in->gyro_format;
        
        gst_buffer_unmap(buffer, &map);

        return header;
    }

    static GstBuffer* mux(rs2::frameset& frame_set, const RSHeader& header, const GstRealsenseSrc* src)
    {
        GstMapInfo minfo;
        GstBuffer *buffer;

        auto cframe = frame_set.get_color_frame();
        auto color_sz = static_cast<size_t>(cframe.get_height() * src->gst_stride);
        auto depth = frame_set.get_depth_frame();
        auto depth_sz = static_cast<size_t>(depth.get_data_size());
        
        int imu_sz = 0;
        rs2::frame accel_frame;
        rs2::frame gyro_frame;
        if(src->imu_on)
        {
            accel_frame = frame_set.first_or_default(RS2_STREAM_ACCEL);
            gyro_frame = frame_set.first_or_default(RS2_STREAM_GYRO);
            imu_sz = accel_frame.get_data_size() + gyro_frame.get_data_size();
        }
        constexpr auto header_sz = sizeof(RSHeader);

        const auto buffer_sz = header_sz + color_sz + depth_sz + imu_sz + 1;
        buffer = gst_buffer_new_and_alloc(buffer_sz);
        if (buffer == nullptr)
        {
            GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("failed to allocate buffer"), (NULL));
            
            throw new std::runtime_error("failed to allocate buffer");
        }
        gst_buffer_map(buffer, &minfo, GST_MAP_WRITE);

        GST_LOG_OBJECT(src,
                       "GstBuffer size=%lu, gst_stride=%d, frame_num=%llu",
                       minfo.size, src->gst_stride, cframe.get_frame_number());
        GST_LOG_OBJECT(src, "Buffer timestamp %f", cframe.get_timestamp());

        std::memcpy(minfo.data, &header, sizeof(header));

        // TODO refactor this section into cleaner code
        // NOTE We're always StreamMux now. If color or depth is off, then those portions will be 0
        // if(src->stream_type == StreamType::StreamColor || src->stream_type == StreamType::StreamMux)
        // {
        int rs_stride = 0;
        auto outdata = minfo.data + sizeof(RSHeader);
        rs_stride = cframe.get_stride_in_bytes();
        if (src->gst_stride == rs_stride)
        {
            std::memcpy(outdata, ((guint8 *)cframe.get_data()), color_sz);
            outdata += color_sz;
        }
        else
        {
            int i;
            GST_INFO_OBJECT(src, "Image strides not identical, copy will be slower.");
            for (i = 0; i < src->height; i++)
            {
                std::memcpy(outdata,
                       ((guint8 *)cframe.get_data()) + i * rs_stride,
                       rs_stride);
                outdata += src->gst_stride;
            }
        }

        if (depth_sz != 0)
        {
            std::memcpy(outdata, depth.get_data(), depth_sz);
            outdata += depth_sz;
        }

        if (imu_sz != 0 && src->imu_on)
        {
#ifdef DEBUG
            auto print_imu = [](const float *ptr, const auto descrtion) {
                std::cout << descrtion << ": ";
                for (size_t idx = 0; idx < 3; ++idx)
                {
                    std::cout << *ptr << ',';
                    ++ptr;
                }
                std::cout << std::endl;
            };
            print_imu(static_cast<const float*>(accel_frame.get_data()), "accel");
            print_imu(static_cast<const float*>(gyro_frame.get_data()), "gyro");
#endif
            std::memcpy(outdata, accel_frame.get_data(), accel_frame.get_data_size());
            outdata += accel_frame.get_data_size();
            std::memcpy(outdata, gyro_frame.get_data(), gyro_frame.get_data_size());
            outdata += gyro_frame.get_data_size();
        }
        gst_buffer_unmap(buffer, &minfo);

        return buffer;
    }

    static buf_tuple demux(GstBuffer *buffer, const RSHeader &header)
    {
        GstMapInfo inmap, cmap, dmap, imumap;
        gst_buffer_map(buffer, &inmap, GST_MAP_READ);

        auto color_sz = header.color_height * header.color_stride;
        auto colorbuf = gst_buffer_new_and_alloc(color_sz);
        gst_buffer_map(colorbuf, &cmap, GST_MAP_WRITE);
        auto cdata = inmap.data + sizeof(RSHeader);
        std::memcpy(cmap.data, cdata, color_sz);

        auto depth_sz = header.depth_height * header.depth_stride;
        auto depthbuf = gst_buffer_new_and_alloc(depth_sz);
        gst_buffer_map(depthbuf, &dmap, GST_MAP_WRITE);
        auto ddata = cdata + color_sz;
        std::memcpy(dmap.data, ddata, depth_sz);

        GstBuffer* imubuf = nullptr;
        if (header.accel_format != GST_AUDIO_FORMAT_UNKNOWN && header.gyro_format != GST_AUDIO_FORMAT_UNKNOWN)
        {
            imubuf = gst_buffer_new_and_alloc(depth_sz);
            gst_buffer_map(imubuf, &imumap, GST_MAP_READ);
            constexpr auto imu_sz = 2*sizeof(rs2_vector);
            auto imudata = ddata + depth_sz;
            std::memcpy(imumap.data, imudata, imu_sz);
            GST_BUFFER_TIMESTAMP(imubuf) = GST_BUFFER_TIMESTAMP(buffer);
            gst_buffer_unmap(imubuf, &imumap);
        }

        GST_BUFFER_TIMESTAMP(colorbuf) = GST_BUFFER_TIMESTAMP(buffer);
        GST_BUFFER_TIMESTAMP(depthbuf) = GST_BUFFER_TIMESTAMP(buffer);

        gst_buffer_unmap(buffer, &inmap);
        gst_buffer_unmap(colorbuf, &cmap);
        gst_buffer_unmap(depthbuf, &dmap);

        return std::make_tuple(colorbuf, depthbuf, imubuf);
    }
};

#endif // __GST_RS_MUX__