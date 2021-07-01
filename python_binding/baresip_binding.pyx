import ctypes
path = "/usr/local/lib/"
module_path = "/usr/local/lib/baresip/modules/"
baresip = ctypes.CDLL(path+"libbaresip.so", mode=ctypes.RTLD_GLOBAL)

cdef extern from "dssipapp.h":
    cpdef int start_sip()