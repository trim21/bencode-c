from glob import glob

from setuptools import setup, Extension, find_packages
import os

macro = []

if os.environ.get("BENCODE_DEBUG") == "1":
    macro.append(("BENCODE_DEBUG", "1"))

module = Extension(
    "bencode_c._bencode",
    sources=glob("./src/bencode_c/*.c"),
    include_dirs=["./src/bencode_c", "vendor/klib"],
    define_macros=macro,
    py_limited_api=True,
)

setup(
    ext_modules=[module],
    packages=find_packages("src"),
    package_dir={"": "src"},
    package_data={"": ["*.h", "*.c", "*.pyi", "py.typed"]},
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)
