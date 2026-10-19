file(GLOB FONT_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c")
idf_component_register(SRCS "main.c" "ui.c" "assets/orb_assets.c" ${FONT_SRCS}
  INCLUDE_DIRS "."
  REQUIRES esp_wifi esp_netif nvs_flash esp_event esp_timer esp_driver_gpio esp_driver_i2c
           esp_driver_usb_serial_jtag json)
