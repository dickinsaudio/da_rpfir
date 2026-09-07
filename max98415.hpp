#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t MAX98415_REGISTER_COUNT = 217;

size_t max98415_register_count();
int max98415_format_register(size_t index, char *buffer, size_t length, bool *read_ok);