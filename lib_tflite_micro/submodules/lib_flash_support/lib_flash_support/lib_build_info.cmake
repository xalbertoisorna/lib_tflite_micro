set(LIB_NAME lib_flash_support)
set(LIB_VERSION 0.1.0)
set(LIB_INCLUDES api)
# weights_server.h (weights_server_header_t, weights_server_cmd_t) comes from
# lib_tflite_micro, via the ai_tools runtime
set(LIB_DEPENDENT_MODULES "ai_tools")
# Sources are globbed from src/. Architecture-specific files are guarded:
#   *_xs3.{c,S}  #if __XS3A__   (libquadflash + bit-banged reads)
#   *_vx4.{c,S}  #if __VX4B__   (libflash + libflashdata)
XMOS_REGISTER_MODULE()

# Flash libraries from the tools, per architecture
if(APP_BUILD_ARCH STREQUAL "xs3a")
    set(LIB_FLASH_SUPPORT_LINK_LIBS -lquadflash)
elseif(APP_BUILD_ARCH STREQUAL "vx4b")
    set(LIB_FLASH_SUPPORT_LINK_LIBS -lflash -lflashdata)
else()
    message(FATAL_ERROR "lib_flash_support: unsupported architecture '${APP_BUILD_ARCH}'")
endif()

foreach(target ${APP_BUILD_TARGETS})
    target_link_options(${target} PRIVATE ${LIB_FLASH_SUPPORT_LINK_LIBS})
endforeach()
