cmake_minimum_required(VERSION 3.13)

set(COMPONENT_REQUIRES)
set(COMPONENT_PRIV_REQUIRES)
set(COMPONENT_SRCS "main/galvo_main.c" "main/mirror_dac.c" "main/bd_processor.c" "main/fb_processor.c" "main/ttl_controller.c" "main/dot_buffer.c" "main/serial_protocol.c")
set(COMPONENT_ADD_INCLUDEDIRS "main")

include($ENV{IDF_PATH}/tools/cmake/component.cmake)