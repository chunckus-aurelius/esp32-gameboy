set(core_srcs
    src/gb_core/gb_emulator.c
    src/gb_core/gb_video.c
    src/gb_core/vendor/minigb_apu/minigb_apu.c)
set(srcs
    ${core_srcs}
    src/storage/game_catalog.c
    src/storage/rom_file.c
    src/storage/save_file.c
    src/storage/state_file.c)
set(priv_requires heap esp_timer freertos)

if(CONFIG_SOC_USB_OTG_SUPPORTED AND CONFIG_GAMEBOY_USB_SERIAL_HOST)
    list(APPEND srcs src/controller/UsbSerialHost.cpp src/controller/UsbGamepad.cpp)
    if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
        list(APPEND priv_requires espressif__usb esp_hal_usb)
    else()
        list(APPEND priv_requires usb hal)
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include src/gb_core src/storage src/controller
                       PRIV_INCLUDE_DIRS src
                       PRIV_REQUIRES ${priv_requires})

# Core is a header-embedded interpreter; keep optimization local to it.
target_compile_definitions(${COMPONENT_LIB} PRIVATE MINIGB_APU_AUDIO_FORMAT_S16SYS)
set_source_files_properties(${core_srcs} PROPERTIES COMPILE_OPTIONS
    "-O3;-Wno-error=unused-label;-fjump-tables;-ftree-switch-conversion")
