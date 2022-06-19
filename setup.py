import os
import subprocess
import shutil
import platform
import multiprocessing
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

# the script should only be called within keyiz/manylinux2010 container

GCC_VERSION = "11.3.0"
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # required for auto-detection of auxiliary "native" libs
        extdir = os.path.join(extdir, "fsim")
        if not os.path.exists(extdir):
            os.makedirs(extdir)

        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = "Debug" if self.debug else "Release"
        if "DEBUG" in os.environ:
            cfg = "Debug"

        # CMake lets you override the generator - we need to check this.
        # Can be set with Conda-Build, for example.
        is_linux = platform.system() == "Linux"
        cmake_args = [
            "-DCMAKE_BUILD_TYPE={}".format(cfg),  # not used on MSVC, but no harm
        ]
        if is_linux:
            cmake_args += ["-DSTATIC_BUILD=ON"]
        build_args = []
        num_cpu = multiprocessing.cpu_count()
        build_args += [f"-j{num_cpu}"]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp
        )

        make_targets = ["fsim-bin", "fsim-runtime"]
        subprocess.check_call(
            ["cmake", "--build", ".", "--target"] + make_targets + build_args, cwd=self.build_temp
        )
        # need to copy stuff over
        # first copy GCC, if exists
        gcc_dir = "/usr/local/gcc-" + GCC_VERSION
        if os.path.exists(gcc_dir):
            gcc_dirs = next(os.walk(gcc_dir))[1]
            for dirname in gcc_dirs:
                src = os.path.join(gcc_dir, dirname)
                dst = os.path.join(extdir, dirname)
                if not os.path.exists(dst):
                    shutil.copytree(src, dst)
            # need to delete unnecessary stuff to make the wheel smaller
            os.remove(os.path.join(extdir, "bin", f"lto-dump-{GCC_VERSION}"))
            shutil.rmtree(os.path.join(extdir, "share"))
        # now copy other include files
        extern_include = ["marl", "logic"]
        src_root = os.path.dirname(os.path.abspath(__file__))
        for extern_dir in extern_include:
            src = os.path.join(src_root, "extern", extern_dir, "include", extern_dir)
            dst = os.path.join(extdir, "include", extern_dir)
            if not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copytree(src, dst)
        # copy runtime header
        runtime_src = os.path.join(src_root, "src", "runtime")
        runtime_dst = os.path.join(extdir, "include", "runtime")
        if not os.path.exists(runtime_dst):
            shutil.copytree(runtime_src, runtime_dst)
        # copy over the build runtime
        runtime_src = os.path.join(self.build_temp, "src", "runtime", "libfsim-runtime.so")
        runtime_dst = os.path.join(extdir, "lib", "libfsim-runtime.so")
        if not os.path.exists(runtime_dst):
            os.makedirs(os.path.dirname(runtime_dst), exist_ok=True)
            shutil.copy(runtime_src, runtime_dst)

        # copy fsim binary
        fsim_src = os.path.join(self.build_temp, "tools", "fsim")
        fsim_dst = os.path.join(extdir, "bin", "fsim")
        if not os.path.exists(fsim_dst):
            os.makedirs(os.path.dirname(fsim_dst), exist_ok=True)
            shutil.copy(fsim_src, fsim_dst)


with open(os.path.join(ROOT_DIR, "README.rst")) as f:
    long_description = f.read()

with open(os.path.join(ROOT_DIR, "VERSION")) as f:
    version = f.read().strip()

setup(
    name='fsim',
    version=version,
    author='Keyi Zhang',
    author_email='keyi@cs.stanford.edu',
    long_description=long_description,
    long_description_content_type='text/x-rst',
    url="https://github.com/Kuree/fsim",
    scripts=[os.path.join("scripts", "fsim")],
    ext_modules=[CMakeExtension("fsim")],
    # use ninja package
    install_requires=["ninja", "clang-format"],
    cmdclass={"build_ext": CMakeBuild},
)
