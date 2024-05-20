import sys
from glob import glob

from setuptools import setup, Extension, find_packages

module = Extension(
    "bencode_c._bencode",
    sources=glob("./src/bencode_c/*.c"),
    include_dirs=["./src/bencode_c"],
    py_limited_api=True,
)

setup(
    ext_modules=[module],
    packages=find_packages("src"),
    package_dir={"": "src"},
    package_data={"": ["*.h", '*.c', '*.pyi', 'py.typed']},  # <- This line
    options={'bdist_wheel': {'py_limited_api': f'cp3{sys.version_info[1]}'}},
)
