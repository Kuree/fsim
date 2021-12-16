import os
import subprocess
import shutil
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

# the script should only be called within keyiz/manylinux2010 container

GCC_VERSION = "11.2.0"
SLANG_URL = "https://github.com/MikePopoloski/slang/releases/download/nightly/slang-linux.tar.gz"


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # required for auto-detection of auxiliary "native" libs
        extdir = os.path.join(extdir, "xsim")
        if not os.path.exists(extdir):
            os.makedirs(extdir)

        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = "Debug" if self.debug else "Release"
        if "DEBUG" in os.environ:
            cfg = "Debug"

        # CMake lets you override the generator - we need to check this.
        # Can be set with Conda-Build, for example.
        cmake_generator = os.environ.get("CMAKE_GENERATOR", "")
        cmake_args = [
            "-DSTATIC_BUILD=ON",
            "-DCMAKE_BUILD_TYPE={}".format(cfg),  # not used on MSVC, but no harm
        ]
        build_args = []
        build_args += ["-j"]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        # need to download the pre-built slang
        slang_dir = os.path.join(self.build_temp, "slang-dist")
        if not os.path.exists(slang_dir):
            os.makedirs(slang_dir, exist_ok=True)
            subprocess.check_call(["curl", "-OL", SLANG_URL], cwd=slang_dir)
            subprocess.check_call("tar xzf slang-linux.tar.gz --strip-components 1".split(), cwd=slang_dir)
            shutil.rmtree(os.path.join(slang_dir, "slang-linux.tar.gz"), ignore_errors=True)

        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp
        )

        make_targets = ["xsim-bin", "xsim-runtime"]
        subprocess.check_call(
            ["cmake", "--build", ".", "--target"] + make_targets + build_args, cwd=self.build_temp
        )
        # need to copy stuff over
        # first copy GCC
        gcc_dir = "/usr/local/gcc-" + GCC_VERSION
        gcc_dirs = next(os.walk(gcc_dir))[1]
        for dirname in gcc_dirs:
            src = os.path.join(gcc_dir, dirname)
            dst = os.path.join(extdir, dirname)
            if not os.path.exists(dst):
                shutil.copytree(src, dst)
        # now copy other include files
        extern_include = ["marl", "logic"]
        src_root = os.path.dirname(os.path.abspath(__file__))
        for extern_dir in extern_include:
            src = os.path.join(src_root, "extern", extern_dir, "include", extern_dir)
            dst = os.path.join(extdir, "include", extern_dir)
            if not os.path.exists(dst):
                shutil.copytree(src, dst)
        # copy runtime header
        runtime_src = os.path.join(src_root, "src", "runtime")
        runtime_dst = os.path.join(extdir, "include", "runtime")
        if not os.path.exists(runtime_dst):
            shutil.copytree(runtime_src, runtime_dst)
        # copy over the build runtime
        runtime_src = os.path.join(self.build_temp, "src", "runtime", "libxsim-runtime.so")
        runtime_dst = os.path.join(extdir, "lib", "libxsim-runtime.so")
        if not os.path.exists(runtime_dst):
            shutil.copy(runtime_src, runtime_dst)

        # copy xsim binary
        xsim_src = os.path.join(self.build_temp, "tools", "xsim")
        xsim_dst = os.path.join(extdir, "bin", "xsim")
        if not os.path.exists(xsim_dst):
            shutil.copy(xsim_src, xsim_dst)


setup(
    name='xsim-python',
    version="0.0.2",
    author='Keyi Zhang',
    author_email='keyi@cs.stanford.edu',
    long_description="",
    long_description_content_type='text/x-rst',
    url="https://github.com/Kuree/xsim",
    scripts=[os.path.join("scripts", "xsim")],
    ext_modules=[CMakeExtension("xsim")],
    # use ninja package
    install_requires=["ninja"],
    cmdclass={"build_ext": CMakeBuild},
)
