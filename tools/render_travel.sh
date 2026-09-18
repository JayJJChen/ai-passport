#!/usr/bin/env bash
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
lvgl="${1:?Usage: render_travel.sh <pinned-lvgl-directory>}"
output="${repo}/build/ui-preview"
mkdir -p "${output}"
cmake -S "${lvgl}" -B "${output}/lvgl-build" \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF \
    -DCONFIG_LV_USE_THORVG_INTERNAL=OFF \
    -DLV_BUILD_CONF_PATH="${repo}/tools/render_lv_conf.h" -DCMAKE_C_FLAGS=""
cmake --build "${output}/lvgl-build" -j 4
cc -std=c11 -Wall -Wextra -Werror \
    -DLV_CONF_PATH="\"${repo}/tools/render_lv_conf.h\"" \
    -I"${lvgl}" -I"${repo}/main" -I"${repo}/components/bsp/src" \
    -I"${IDF_PATH}/components/json/cJSON" \
    "${repo}/tools/render_travel.c" "${repo}/main/travel_model.c" \
    "${repo}/main/travel_pack.c" "${repo}/main/travel_content.c" "${repo}/main/travel_ui.c" \
    "${repo}/assets/fonts/travel_ui_font_24.c" "${repo}/assets/fonts/travel_ui_font_36.c" \
    "${repo}/assets/images/koala_sprite.c" "${repo}/components/bsp/src/bsp_display_rounding.c" \
    "${IDF_PATH}/components/json/cJSON/cJSON.c" \
    "${output}/lvgl-build/lib/liblvgl.a" -lm -o "${output}/render_travel"
"${output}/render_travel" "${repo}/assets/packs/shanghai/cards.klp" "${output}"
