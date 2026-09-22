idf_component_register(
    SRCS "main.c" "travel_model.c" "travel_animation.c" "travel_pack.c" "travel_content.c"
         "travel_ui.c" "voice_state_payload.c" "voice_wifi.cpp" "voice_assistant.cpp"
         "../assets/fonts/travel_ui_font_24.c" "../assets/fonts/travel_ui_font_36.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_partition json esp_http_client esp-tls
)
target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../assets/packs/western-australia/cards.klp" BINARY RENAME_TO koala_default)
target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/koala_frames.bin" BINARY RENAME_TO koala_frames)
