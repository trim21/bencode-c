from glob import glob

from setuptools import setup, Extension

module = Extension(
    "bencode_c._bencode",
    sources=glob("./src/bencode_c/*.c"),
    include_dirs=["./src/bencode_c"],
)

setup(
    ext_modules=[module],
)
