cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM "rp2040")

include(pico-sdk/pico_sdk_init.cmake)
project(smd_pico_controller)
pico_sdk_init()

set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk/lib/lwip)
set(LWIP_INCLUDE_DIRS "${LWIP_DIR}/src/include" "${CMAKE_CURRENT_SOURCE_DIR}")
include(${LWIP_DIR}/src/Filelists.cmake)

add_executable(${PROJECT_NAME}
	controller.c
	log.c
	${PICO_TINYUSB_PATH}/lib/networking/dhserver.c
	${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c)
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${LWIP_INCLUDE_DIRS}
	${PICO_TINYUSB_PATH}/lib/networking)
target_link_libraries(${PROJECT_NAME} PRIVATE pico_stdlib hardware_pio pico_unique_id tinyusb_device lwipallapps lwipcore)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/low_level_controller.pio)
pico_add_extra_outputs(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)
