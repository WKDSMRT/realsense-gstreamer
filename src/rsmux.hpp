#ifndef __GST_RSMUX_H__
#define __GST_RSMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <librealsense2/rs.hpp>

#include "common.hpp"

#include <tuple>

using buf_tuple = std::tuple<GstBuffer*, GstBuffer*>;

class RSMux 
{
public:
    template <typename Element>
    static RSHeader GetRSHeader(Element* src, GstBuffer* buffer)
    {
        gst_element_post_message(GST_ELEMENT_CAST(src), 
                gst_message_new_info(GST_OBJECT_CAST(src), NULL, "extracting header"));
                
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
        constexpr auto header_sz = sizeof(RSHeader);

        /* TODO: use allocator or use from pool if that's more efficient or safer*/
        const auto buffer_sz = header_sz + color_sz + depth_sz + 1;
        buffer = gst_buffer_new_and_alloc(buffer_sz);
        if (buffer == nullptr)
        {
            gst_element_post_message(GST_ELEMENT_CAST(src),
                                     gst_message_new_info(GST_OBJECT_CAST(src), NULL, "failed to allocate buffer"));
            throw new std::runtime_error("failed to allocate buffer");
        }
        gst_buffer_map(buffer, &minfo, GST_MAP_WRITE);

        // GST_ELEMENT_WARNING (src, RESOURCE, FAILED,
        //         ("Specified serial number %lu not found. Using first found device.", src->serial_number),
        //         (NULL));
        GST_LOG_OBJECT(src,
                       "GstBuffer size=%lu, gst_stride=%d, frame_num=%llu",
                       minfo.size, src->gst_stride, cframe.get_frame_number());
        GST_LOG_OBJECT(src, "Buffer timestamp %f", cframe.get_timestamp());

        gst_element_post_message(GST_ELEMENT_CAST(src),
                                 gst_message_new_info(GST_OBJECT_CAST(src), NULL, "copying header"));
        memcpy(minfo.data, &header, sizeof(header));

        // TODO refactor this section into cleaner code
        // NOTE We're always StreamMux now. If color or depth is off, then those portions will be 0
        // if(src->stream_type == StreamType::StreamColor || src->stream_type == StreamType::StreamMux)
        // {
        int rs_stride = 0;
        auto outdata = minfo.data + sizeof(RSHeader);
        rs_stride = cframe.get_stride_in_bytes();
        /* TODO: use orc_memcpy */
        if (src->gst_stride == rs_stride)
        // if (color_sz != 0)
        {
            gst_element_post_message(GST_ELEMENT_CAST(src),
                                     gst_message_new_info(GST_OBJECT_CAST(src), NULL, "copying color frame"));
            memcpy(outdata, ((guint8 *)cframe.get_data()), color_sz);
            outdata += color_sz;
        }
        else
        {
            int i;
            gst_element_post_message(GST_ELEMENT_CAST(src),
                                     gst_message_new_info(GST_OBJECT_CAST(src), NULL, "Image strides not identical, copy will be slower."));
            for (i = 0; i < src->height; i++)
            {
                memcpy(outdata,
                       ((guint8 *)cframe.get_data()) + i * rs_stride,
                       rs_stride);
                outdata += src->gst_stride;
            }
        }

        // if(src->stream_type == StreamType::StreamMux)
        if (depth_sz != 0)
        {
            gst_element_post_message(GST_ELEMENT_CAST(src),
                                     gst_message_new_info(GST_OBJECT_CAST(src), NULL,
                                                          _gst_element_error_printf("copying depth frame. buffer_end=%p, depth end=%p", minfo.data + buffer_sz, outdata + depth_sz)));
            memcpy(outdata, depth.get_data(), depth_sz);
        }

        gst_element_post_message(GST_ELEMENT_CAST(src),
                                 gst_message_new_info(GST_OBJECT_CAST(src), NULL, "Unmapping buffer"));
        gst_buffer_unmap(buffer, &minfo);

        return buffer;
    }

    static buf_tuple demux(GstBuffer *buffer, const RSHeader &header)
    {
        GstMapInfo inmap, cmap, dmap;
        gst_buffer_map(buffer, &inmap, GST_MAP_READ);

        // gst_element_post_message(GST_ELEMENT_CAST(rsdemux),
        //                          gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "copying color buffer to src pad"));

        auto color_sz = header.color_height * header.color_stride;
        auto colorbuf = gst_buffer_new_and_alloc(color_sz);
        gst_buffer_map(colorbuf, &cmap, GST_MAP_WRITE);
        auto cdata = inmap.data + sizeof(RSHeader);
        memcpy(cmap.data, cdata, color_sz);

        // gst_element_post_message(GST_ELEMENT_CAST(rsdemux),
        //                          gst_message_new_info(GST_OBJECT_CAST(rsdemux), NULL, "copying depth buffer to src pad"));

        auto depth_sz = header.depth_height * header.depth_stride;
        auto depthbuf = gst_buffer_new_and_alloc(depth_sz);
        gst_buffer_map(depthbuf, &dmap, GST_MAP_WRITE);
        auto ddata = cdata + color_sz;
        memcpy(dmap.data, ddata, depth_sz);

        GST_BUFFER_TIMESTAMP(colorbuf) = GST_BUFFER_TIMESTAMP(buffer);
        GST_BUFFER_TIMESTAMP(depthbuf) = GST_BUFFER_TIMESTAMP(buffer);

        gst_buffer_unmap(buffer, &inmap);
        gst_buffer_unmap(colorbuf, &cmap);
        gst_buffer_unmap(depthbuf, &dmap);

        return std::make_tuple(colorbuf, depthbuf);
    }
};

#endif // __GST_RS_MUX__