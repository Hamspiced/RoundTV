idf_component_register(SRCS "main.cpp" "board.cpp" "avi.cpp" "catalog.cpp" "player.cpp" "splash.cpp" "gif_player.cpp" "vendor/esp_lcd_st77916.c" "vendor/panel_variant.c" "vendor/es8311.c"
    INCLUDE_DIRS "." "vendor" REQUIRES sdmmc esp_lcd driver fatfs esp_timer esp_hw_support nvs_flash lvgl esp_new_jpeg)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
