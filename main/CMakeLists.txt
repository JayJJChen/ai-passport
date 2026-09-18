idf_component_register(
    SRCS "main.c" "travel_model.c" "travel_pack.c" "travel_content.c"
         "travel_ui.c" "travel_wifi.c" "travel_blufi_security.c"
         "../assets/fonts/travel_ui_font_24.c" "../assets/fonts/travel_ui_font_36.c"
         "../assets/images/koala_sprite.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_netif esp_timer esp_wifi nvs_flash esp_partition json mbedtls
)
target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../assets/packs/shanghai/cards.klp" BINARY RENAME_TO koala_default)
