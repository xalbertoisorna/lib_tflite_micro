TFLite-micro container
======================

This repo is a wrapper for lib_tflite_micro.
This repo contains all third party repos that are needed to use tflite-micro on an XCORE.
It wraps this third-party C++ software up in a library that exposes three interfaces:

#. A C interface for use on embedded systems (``lib_tflite_micro``).

#. A command line interface for use on a host (``host_command_line_interface``)

The ``lib_tflite_micro`` library depends on ``lib_nn``.

It provides the following services:

* lib_tflite_micro/module_build_info: file that allows lib_tflite_micro to be integrated into normal XMOS build flow

* lib_tflite_micro/src: a function that wraps the C++ interpreter in C (inference_engine.cc), and a collection of
  kernels that we add to tflite-micro with XCORE specific operators
  
* lib_tflite_micro/api: .h files for the above

* host_cmd_line_interpeter: a command line wrapper for XTFLM, enabling it to be used over the command line.


C interface
-----------

It exposes a C interface comprising a datastructure (inference_engine_t)
with a few functions that can be used to initialise the structure and/or


Getting the XCORE.AI optimiser
------------------------------

You can get the XCORE.AI optimiser through pypi:

* https://pypi.org/project/xmos-ai-tools/

This gets you both a command line interface and python interface to the xcore-opt tool that optimises
a ``.tflite`` file for xcore
perform an inference. The data structure itself can be used to directly
read/write data into tensors, this enables sensors to directly operate
in the tensor space.

The C interface can be used with the standard XMOS build system, and is
built from the appropriate application directory

Command line interface
----------------------

The command line interface uses the C interface above and makes it accessible
from the command line, enabling the end user to send data through a TFLite model
using the XTFLM interpreter. The XTFLM intepreter will have XCORE specific operators
(such as 2D convolutions, loading from flash) that are emulated on the host.

The command line interface is built by invoking ``make install`` at top level or
inside ``host_command_line_interface``.

The command line interface cane be tested by invoking ``make test`` at either level. 

Batched INT8 convolution reproducer
----------------------------------

``XC_conv2d_v2`` currently requires batch size 1 and returns ``kTfLiteError``
for other batch sizes. The batch-2 reproducer therefore fails at invocation.

``tests/conv2d/src/conv2d_int8.tflite`` contains one INT8 identity 1x1 convolution:
input/output shape ``[2,1,1,4]``, identity weights shaped ``[4,1,1,4]``, zero
biases, unit quantization scales, zero zero-points, VALID padding and unit
stride/dilation. Its identity output has been verified with the TFLite reference
interpreter.

As in the hello-world example, CMake runs ``xcore-opt`` to generate the optimized
model and its C++ API. Generated files stay in the build directory.
``tests/conv2d/src/main.cpp`` calls ``model_init`` and ``model_invoke``, prints
both batches and returns nonzero on any mismatch. There is no handwritten
operator setup or manual batch loop around inference. This reproduces the
suspected runtime batching bug and is intentionally not marked as an expected
failure.

With ``xcore-opt`` on PATH, the VX4 XTC tools activated, and
``lib/libxtflitemicro_vx4b.a`` built::

  cmake -S tests -B tests/build_vx4 \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/lib_tflite_micro/submodules/xmos_cmake_toolchain/vx4_xcc.cmake"
  cmake --build tests/build_vx4 --target conv2d
  ctest --test-dir tests/build_vx4 -R '^conv2d$' --output-on-failure

Or use the pytest wrapper with the same activated tools::

  python -m pytest tests/test_conv2d.py --hw-target=vx4-xcc -v

The default ``--hw-target=native`` runs against the host library. The pytest
wrapper propagates the CTest failure rather than marking this regression as
an expected failure.
