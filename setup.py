import sys
from glob import glob

from setuptools import setup, Extension, find_packages
import os
import sys

if sys.version_info[:2] > (3, 8):
    macro = [("Py_LIMITED_API", hex(sys.hexversion))]
    options = {"bdist_wheel": {"py_limited_api": f"cp3{sys.version_info[1]}"}}
else:
    macro = [("Py_LIMITED_API", "0x03080000")]
    options = {"bdist_wheel": {"py_limited_api": "cp38"}}

print(macro)
extra_compile_args = None
if os.environ.get("BENCODE_DEBUG") == "1":
    macro.append(("BENCODE_DEBUG", "1"))
    # if sys.platform == 'win32':
    #     extra_compile_args = ['/Z7', '/DEBUG']

module = Extension(
    "bencode_c._bencode",
    sources=glob("./src/bencode_c/*.c"),
    include_dirs=["./src/bencode_c", "./vendor/klib"],
    define_macros=macro,
    extra_compile_args=extra_compile_args,
    py_limited_api=True,
)

setup(
    ext_modules=[module],
    packages=find_packages("src"),
    package_dir={"": "src"},
    package_data={"": ["*.h", "*.c", "*.pyi", "py.typed"]},
    include_package_data=True,
    options=options,
)
