import ctypes
path = "/usr/local/lib/"
module_path = "/usr/local/lib/baresip/modules/"
baresip = ctypes.CDLL(path+"libbaresip.so", mode=ctypes.RTLD_GLOBAL)


cdef extern from "dssipapp.h":
    cdef int simple_call(const char *uri)
    cdef int simple_hangup()
    cdef int simple_quit()
    cdef int start_sip(const char *config_path)

def py2c_simple_call(uri: str):
    binary_uri = uri.encode('utf-8')
    simple_call(binary_uri)

def py2c_simple_hangup():
    simple_hangup()

def py2c_simple_quit():
    simple_quit()

def py2c_start_sip(config_path: str):
    binary_config_path = config_path.encode('utf-8')
    start_sip(binary_config_path)

