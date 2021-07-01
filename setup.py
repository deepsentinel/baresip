from setuptools import Extension, setup
from Cython.Build import cythonize

ext_modules = [
    Extension("baresip",
              sources=["python_binding/baresip_binding.pyx"],
              libraries=["dssipapp","baresip"],  # Unix-like specific
              library_dirs=["/usr/local/lib"],
              )
]

setup(name="baresip",
      ext_modules=cythonize(ext_modules))