#pragma once
#include <vector>
#include <cstdint>
#include <string>
// Structural bounds checking before handing user-supplied GIF bytes to LVGL.
bool validate_gif(const std::vector<uint8_t> &data, std::string &error);
