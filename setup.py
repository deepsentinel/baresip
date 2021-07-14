import os
from setuptools import Extension, setup
from Cython.Build import cythonize

DESTDIR = os.environ.get("DESTDIR")
if DESTDIR:
    ext_modules = [
        Extension("baresip",
                sources=["python_binding/baresip_binding.pyx"],
                libraries=["dssipapp","baresip"],  # Unix-like specific
                library_dirs=["/data/ds_venv/lib",DESTDIR+"/lib"],#/data/ds_venv... for runtime, DESTDIR... for compilation time
                include_dirs=["/data/ds_venv/include",DESTDIR+"/include"],
                )
    ]
else:
    ext_modules = [
        Extension("baresip",
                sources=["python_binding/baresip_binding.pyx"],
                libraries=["dssipapp","baresip"],  # Unix-like specific
                library_dirs=["/usr/local/lib"],
                )
    ]

setup(name="baresip",
      version="0.0.1",
      ext_modules=cythonize(ext_modules))