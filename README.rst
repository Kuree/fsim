fsim: Fiber-based SystemVerilog Simulator
===================================================

``fsim`` is a lightweight SystemVerilog simulator based on userland fibers. It leverages `slang`_
as a frontend to parse any SystemVerilog files. ``fsim`` is guided by the following design goals:

1. Concurrent and event-driven. Fine-grained multi-threading using fibers.
2. Performant. Compiles SystemVerilog into C++ code.
3. Fast to compile. Supports incremental and parallel builds builds via ``ninja``.


How to install
--------------
The easiest way to install is via ``pip``. Simply do

.. code:: bash

    pip install fsim

Notice that for maximum compatibility, the Linux wheel is shipped with ``gcc``, which allows xfim to be installed
on any Linux system. macOS should work out of the box if the development tools are installed.

Development
+++++++++++
To build from source, you can use the following commands. Notice that you also need ``ninja`` and
``clang-format`` installed in your environment path.

.. code:: bash

   git submodule update --init
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j

Usage
-----
Once ``fsim`` is installed, you should find ``fsim`` executable in your path. The usage is similar to other
commercial simulators and C/C++ compilers. For instance, to run a simple testbench, we can do

.. code:: bash

   fsim design.sv tb.sv

The command above will compile the SystemVerilog into an executable called ``fsim.out``. You can override it
with ``-o`` option. You can then start the simulation by running the executable. A working directory called
``fsim_dir`` is created to store compilation files.

To run the simulation automatically after the compilation, we can use ``-R`` flag, e.g.:

.. code:: bash

   fsim -R design.sv tb.sv

This feature is simular to Incisive/Xcelium. Notice that if the files are unmodified, subsequent run will not
trigger a new compilation.


.. _slang: https://github.com/MikePopoloski/slang/
