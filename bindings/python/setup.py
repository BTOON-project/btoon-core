"""
Setup script for BTOON Python bindings
"""

from setuptools import setup, Extension, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext
import pybind11
import platform
import os

# Version
VERSION = "0.0.1"

# Read long description from README
def read_long_description():
    try:
        with open("../../README.md", "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        return "BTOON - Binary Tree Object Notation for Python"

# Platform-specific compiler flags
extra_compile_args = ["-std=c++20", "-O3"]
extra_link_args = []

if platform.system() == "Windows":
    extra_compile_args = ["/std:c++20", "/O2"]
elif platform.system() == "Darwin":
    extra_compile_args.extend(["-stdlib=libc++", "-mmacosx-version-min=10.14"])
    extra_link_args.extend(["-stdlib=libc++"])

# Check for optional compression libraries
libraries = []
define_macros = []

# Try to find compression libraries
try:
    import pkgconfig
    if pkgconfig.exists('zlib'):
        libraries.append('z')
        define_macros.append(('HAS_ZLIB', '1'))
    if pkgconfig.exists('liblz4'):
        libraries.append('lz4')
        define_macros.append(('HAS_LZ4', '1'))
    if pkgconfig.exists('libzstd'):
        libraries.append('zstd')
        define_macros.append(('HAS_ZSTD', '1'))
except ImportError:
    # Fallback: assume standard libraries are available
    libraries = ['z']  # Most systems have zlib

# Define the extension
ext_modules = [
    Pybind11Extension(
        "_btoon_native",
        ["module.cpp"],
        include_dirs=[
            # Include directories
            pybind11.get_include(),
            "../../include",
            "/usr/local/include",
        ],
        libraries=libraries,
        library_dirs=[
            "../../build",  # Where libbtoon_core.a is built
            "/usr/local/lib",
        ],
        extra_objects=["../../build/libbtoon_core.a"],
        define_macros=define_macros,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        cxx_std=20,
    ),
]

setup(
    name="btoon",
    version=VERSION,
    author="BTOON Developers",
    author_email="dev@btoon.org",
    description="High-performance binary serialization format for Python",
    long_description=read_long_description(),
    long_description_content_type="text/markdown",
    url="https://github.com/btoon/btoon-core",
    packages=find_packages(),
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.7",
    install_requires=[
        "pybind11>=2.6.0",
    ],
    extras_require={
        "dev": [
            "pytest>=6.0",
            "pytest-cov",
            "pytest-benchmark",
            "black",
            "flake8",
            "mypy",
        ],
        "numpy": ["numpy>=1.19.0"],
        "pandas": ["pandas>=1.0.0"],
    },
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: C++",
        "Topic :: Software Development :: Libraries",
        "Topic :: System :: Archiving",
    ],
    keywords="serialization binary msgpack btoon compression schema",
    project_urls={
        "Documentation": "https://btoon.readthedocs.io",
        "Bug Reports": "https://github.com/btoon/btoon-core/issues",
        "Source": "https://github.com/btoon/btoon-core",
    },
    entry_points={
        "console_scripts": [
            "btoon=btoon.cli:main",
        ],
    },
    package_data={
        "btoon": ["py.typed", "*.pyi"],
    },
)
