"""
Python wrapper for Gst Realsense meta lib.

Install to $GST_PLUGIN_PATH/python
"""
import os
import ctypes
import ctypes.util
import gi
from typing import Optional

gi.require_version('Gst', '1.0')
from gi.repository import Gst

import pyrealsense2 as rs

# load the library
library_path = ctypes.util.find_library("gstrealsense_meta")
gstrs_meta_lib = ctypes.CDLL('/usr/local/lib/x86_64-linux-gnu/' + library_path)
assert gstrs_meta_lib.gst_buffer_realsense_get_depth_meta is not None

# define a CTypes struct to receive the intrinsics
class INTRINSICS(ctypes.Structure):
    _fields_ = [('width', ctypes.c_int),
                ('height', ctypes.c_int),
                ('ppx', ctypes.c_float),
                ('ppy', ctypes.c_float),
                ('fx', ctypes.c_float),
                ('fy', ctypes.c_float),
                ('model', ctypes.c_float),
                ('coeffs', ctypes.c_float * 5)]

# function signatures
gstrs_meta_lib.gst_buffer_realsense_get_depth_meta.argtypes = [ctypes.c_void_p]
gstrs_meta_lib.gst_buffer_realsense_get_depth_meta.restype = ctypes.c_float

gstrs_meta_lib.gst_buffer_realsense_meta_get_instrinsics.argtypes = [ctypes.c_void_p]
gstrs_meta_lib.gst_buffer_realsense_meta_get_instrinsics.restype = ctypes.POINTER(INTRINSICS)

def get_depth_units(buffer: Gst.Buffer) -> Optional[float]:
    units = gstrs_meta_lib.gst_buffer_realsense_get_depth_meta(hash(buffer))
    if units:
        return units
    else:
        return None


def get_color_intrinsics(buffer: Gst.Buffer) -> Optional[rs.intrinsics]:   
    pi = gstrs_meta_lib.gst_buffer_realsense_meta_get_instrinsics(hash(buffer))
    intrs = pi.contents
    print(intrs.width)
    return intrs
