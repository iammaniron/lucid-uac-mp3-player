idf_component_register(SRCS "main.c" "lucid_board.c" "beep.c"
    INCLUDE_DIRS "."
    REQUIRES usb_host_uac usb esp_driver_gpio esp_driver_spi fatfs sdmmc)
