cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.1.0-koala")
project(FoloToy-AI-Passport)

# Include the default content partition in the verified merged image.
# Later card-pack replacements write only this data partition, not the application.
configure_file("${CMAKE_SOURCE_DIR}/assets/packs/western-australia/cards.klp"
               "${CMAKE_BINARY_DIR}/cards.klp" COPYONLY)
esptool_py_flash_target_image(flash travel_cards "0x600000" "${CMAKE_BINARY_DIR}/cards.klp")
