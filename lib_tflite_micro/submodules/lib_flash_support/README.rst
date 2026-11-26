lib_flash_support
#################

Flash weights server for ``lib_tflite_micro``, for XS3 (xcore.ai) and VX4
(xcore-400).

Large model weights can be kept in QSPI flash instead of RAM. When the model
runs, each ``XC_ld_weights`` op asks a *weights server* for its weights over a
channel. This library provides the server that reads them from flash:

- ``flash_server()``: a thread that owns the QSPI ports and serves read
  requests from one or two clients.
- ``fl_connectToDevice()`` / ``fast_flash_init()`` / ``fast_flash_read()``:
  the flash backend, one per architecture.

Layout
******

::

   lib_flash_support/
     CMakeLists.txt              add_library(lib_flash_support) for CMake users
     lib_build_info.cmake        xcommon_cmake module
     api/flash_server.h          flash_server(), flash names for the protocol
     api/fast_flash.h            backend API, per-architecture types
     src/flash_server.c          protocol loop (all architectures)
     src/fast_flash_xs3.c        XS3 backend   (#if __XS3A__)
     src/fast_flash_read_loop_xs3.S
     src/fast_flash_vx4.c        VX4 backend   (#if __VX4B__)
     src/fast_flash_read_loop_vx4.S  (unused, to be removed)

Architecture-specific files are guarded in the source, so the whole ``src/``
directory can be compiled for either architecture.

Relationship to lib_tflite_micro
********************************

::

   lib_tflite_micro (runtime)                       lib_flash_support
     weights_server.h           <-- includes --------- flash_server.h
     load_weights.c  (client)                          flash_server.c, fast_flash_*.c
     tile_ram_server (weights in another tile's RAM)

The client/server protocol (``weights_server_header_t``,
``weights_server_cmd_t``, ``WEIGHTS_*`` commands) is generic and defined in
``lib_tflite_micro/api/weights_server.h``. Either server can serve the same
client. ``flash_server.h`` provides flash names for it, so existing code keeps
compiling:

==========================  =================================
Flash name                  Generic name
==========================  =================================
``flash_t``                 ``weights_server_header_t``
``flash_command_t``         ``weights_server_cmd_t``
``FLASH_READ_PARAMETERS``   ``WEIGHTS_READ_PARAMETERS``
``FLASH_SERVER_QUIT``       ``WEIGHTS_SERVER_QUIT``
``FLASH_SERVER_INIT``       ``WEIGHTS_SERVER_INIT``
==========================  =================================

Building and using it
*********************

There are three ways to use the library. Use only one of them in an
application; using two links ``flash_server`` twice.

1. **Packaged in the runtime (default).** ``lib_tflite_micro/CMakeLists.txt``
   builds this directory for device targets and merges its objects into
   ``libxtflitemicro_<arch>.a``; ``flash_server.h`` and ``fast_flash.h`` are
   installed with the other runtime headers. ``ai_tools`` adds the tools flash
   libraries to the link. An application only needs::

      set(APP_DEPENDENT_MODULES "ai_tools")

2. **CMake target**::

      add_subdirectory(<path>/lib_flash_support/lib_flash_support)
      target_link_libraries(app PRIVATE lib_flash_support)

3. **xcommon_cmake module** (``lib_build_info.cmake``), when building from
   source instead of the packaged runtime.

The tools flash libraries are linked per architecture:

======  =============================================
XS3     ``-lquadflash``
VX4     ``-lflash -lflashdata`` (``xmos_flash.h``, ``xmos_flashdata.h``)
======  =============================================

Application code, with the model on another thread::

   #include "flash_server.h"

   fl_QuadDeviceSpec flash_spec[] = {
   #ifdef __XS3A__
       FL_QUADDEVICE_DEFAULT
   #else
       FL_QUADDEVICE_VX4_DEFAULT        // {num_pages (0 = discover), core Hz, max SCLK Hz}
   #endif
   };
   fl_QSPIPorts qspi = { PORT_SQI_CS, PORT_SQI_SCLK, PORT_SQI_SIO, XS1_CLKBLK_2 };

   flash_t headers[1];
   PAR_JOBS(
       PJOB(flash_server, (&c_flash.end_a, headers, 1, &qspi, flash_spec, 1)),
       PJOB(nn_thread,    (c_flash.end_b)));   // model_init((void *)chanend)
   // the client ends with: chan_out_word(c, FLASH_SERVER_QUIT);

On VX4 the tools do not generate ``PORT_*`` macros from the ``.xn`` file; the
application defines ``PORT_SQI_CS/SCLK/SIO`` itself (``XS1_PORT_1B``,
``XS1_PORT_1C``, ``XS1_PORT_4B`` on the xcore-400 EVK).

Generate the flash image with ``xf.generate_flash()`` and program it with
``xflash`` (see ``ai_tools/examples/app_flash_single_model``).

Finding the weights in flash
****************************

At start-up the backend asks the tools where the data partition starts
(XS3 ``fl_getDataPartitionBase()``, VX4 ``flashptn_data_address()``). Both
read this from information at the start of flash that ``xflash`` writes with
a program image. ``xflash --data`` on its own writes the weights at address 0
and overwrites that information, so the backend checks the reported base: the
image must have the version word at offset 32. If not, it uses address 0. If
neither matches it fails with ``No weights image found``. Both flows work:

- full image: ``xflash --boot-partition-size 0x100000 app.xe --data xcore_flash_binary.out``
- data only, then ``xrun``: ``xflash --target <board> --data xcore_flash_binary.out``

VX4 backend
***********

``fast_flash_vx4.c`` uses the tools libraries: ``flash_connect()``,
``flashdata_mount()`` / ``flashptn_data_address()`` and calibrated "boost"
reads (``flashboost_clock_make_config()``, ``flashboost_start()``,
``flash_read_boost()``). Boost is calibrated against the bootloader's pattern
when the partition information is valid, otherwise against the 32-byte pattern
at the start of the weights image. If calibration fails, it uses ordinary
quad reads.

The image is stored nibble-swapped (``generate_flash()`` does this for the XS3
reader). Boost reads undo the swap in the transfer; quad reads return the
bytes as stored, so the backend swaps them back. It prints which path is in
use, e.g. ``Flash reads: boost (calibration pattern: image)``.

Simulation (xsim, XS3)
**********************

xsim's ``QSPI.so`` plugin can simulate the flash. Build a full image (boot +
data partition) and pass it to the plugin::

   xflash --noinq --boot-partition-size 0x100000 app.xe \
          --data xcore_flash_binary.out -o flash_image.bin

   # spi.xml
   <SPIFlash size="4194304" addressBytes="3" msbFirst="true">
     <Load name="flash_image.bin" write="false"/>
   </SPIFlash>

   xsim --xscope "-offline trace.xmt" \
        --plugin QSPI.so "-xml spi.xml -core tile[0] XS1_PORT_1B XS1_PORT_1C XS1_PORT_4B" app.xe

Notes:

- The simulated flash has no SFDP table, so ``FL_QUADDEVICE_DEFAULT`` cannot
  find the flash size. Under ``_is_simulation()`` the application should set
  ``flash_spec[0].flashId = 1`` and ``flash_spec[0].numPages`` explicitly.
- Apps built with ``-fxscope`` only show ``printf`` / assert output in xsim
  when ``--xscope`` is given.
- "pad and plugin driving in opposite directions" warnings during start-up
  come from the timing-training sweep and are expected.
- On VX4, the slipgate xsim xscope plugin currently fails to load
  (``__sodFlag`` on tile[1]), so output cannot be seen in simulation.

Status
******

- XS3 (XTC 15.3.1): verified in xsim and on XK-EVK-XU316 (data-only flow).
- VX4 (slipgate): verified on XK-EVK-XU416, full-image and data-only flows,
  both using boost reads.
