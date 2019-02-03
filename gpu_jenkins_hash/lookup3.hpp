#pragma once

#include <cstdint>

uint32_t hashword(const uint32_t* source, size_t length, uint32_t initval);

uint32_t hashlittle(const void *key, size_t length, uint32_t initval);
