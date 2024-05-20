from glob import glob

from setuptools import setup, Extension, find_packages

module = Extension(
    "bencode_c._bencode",
    sources=glob("./src/bencode_c/*.c"),
    include_dirs=["./src/bencode_c"],
)

setup(
    ext_modules=[module],
    packages=find_packages("src"),
    package_dir={"": "src"},
    options={'bdist_wheel': {'py_limited_api': 'cp38'}},
)
