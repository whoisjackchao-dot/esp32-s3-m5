idf_component_register(SRCS "main.c" "s3p_proto.c" "s3p_state.c" "audio_capture.c" "audio_session.c" "button.c" "ble_gatt.c" "display.c" "pm1.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt nvs_flash spi_flash esp_lcd)
