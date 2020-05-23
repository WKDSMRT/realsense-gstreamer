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

# load the library
library_path = ctypes.util.find_library("gstrealsense_meta")
gstrs_meta_lib = ctypes.CDLL('/usr/local/lib/x86_64-linux-gnu/' + library_path)
assert gstrs_meta_lib.gst_buffer_realsense_get_depth_meta is not None

# function signatures
gstrs_meta_lib.gst_buffer_realsense_get_depth_meta.argtypes = [ctypes.c_void_p]
gstrs_meta_lib.gst_buffer_realsense_get_depth_meta.restype = ctypes.c_float

def get_depth_units(buffer: Gst.Buffer) -> Optional[float]:
    units = gstrs_meta_lib.gst_buffer_realsense_get_depth_meta(hash(buffer))
    if units:
        return units
    else:
        return None
