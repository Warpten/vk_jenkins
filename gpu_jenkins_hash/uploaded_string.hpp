#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#pragma pack(push, 1)
struct uploaded_string {
    uint32_t word_count;
    uint32_t words[32 * 3];
};
#pragma pack(pop)

template <typename T>
struct buffer_t
{
    // The actual handle
    VkBuffer buffer;
    VkDeviceMemory memory;

    uint32_t count;

    const uint32_t get_data_size() const {
        return sizeof(T) * count;
    }
};