# lib_flash_support: flash weights server for lib_tflite_micro (device builds only).
#
# Used by lib_tflite_micro/CMakeLists.txt (objects are packaged into the
# runtime library), or directly by a CMake project:
#   add_subdirectory(<path>/lib_flash_support/lib_flash_support)
#   target_link_libraries(app PRIVATE lib_flash_support)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "XCORE_XS3A")
    set(LIB_FLASH_SUPPORT_ARCH xs3a)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "XCORE_VX4" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "XCORE_VX4B"
       OR CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv32")
    set(LIB_FLASH_SUPPORT_ARCH vx4b)
else()
    message(FATAL_ERROR "lib_flash_support: unsupported CMAKE_SYSTEM_PROCESSOR '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

# Architecture-specific files are guarded in the sources (__XS3A__ / __VX4B__)
file(GLOB LIB_FLASH_SUPPORT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.S"
)
add_library(lib_flash_support STATIC ${LIB_FLASH_SUPPORT_SOURCES})

# weights_server.h (the weights-server protocol) lives in lib_tflite_micro/api
set(WEIGHTS_SERVER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../api"
    CACHE PATH "Directory containing weights_server.h")
target_include_directories(lib_flash_support
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/api"
    PUBLIC  "${WEIGHTS_SERVER_INCLUDE_DIR}"
)

if(LIB_FLASH_SUPPORT_ARCH STREQUAL "xs3a")
    target_compile_options(lib_flash_support PRIVATE -O3 -g -Wall -mcmodel=large -Wno-xcore-fptrgroup)
    # Tools library providing fl_connectToDevice() / fl_getDataPartitionBase()
    target_link_options(lib_flash_support INTERFACE -lquadflash)
else()
    target_compile_options(lib_flash_support PRIVATE -O3 -g -Wall -Wno-fptrgroup -ffunction-sections -fdata-sections)
    # Tools libraries: libflash (xmos_flash.h), libflashdata (xmos_flashdata.h)
    target_link_options(lib_flash_support INTERFACE -lflash -lflashdata)
endif()
