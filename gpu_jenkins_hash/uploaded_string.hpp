#pragma once

#include <cstdint>
#include <string>
#include <algorithm>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

#include "lookup3.hpp"

#pragma pack(push, 1)
struct pattern_t;

struct uploaded_string {
private:
    friend struct pattern_t;

    int32_t char_count;
    uint32_t hash;

    uint32_t words[32 * 3];

public:
    uint32_t get_hash() const {
        return hash;
    }

    uint32_t get_cpu_hash() const {
        return hashlittle((const void*)words, char_count, 0);
    }

    std::string_view value() const {
        return std::string_view(reinterpret_cast<const char*>(words), static_cast<size_t>(char_count)); //-V206
    }

    uploaded_string() {
        memset(words, 0, sizeof(words));
        char_count = 0;
        hash = 0;
    }

    uploaded_string& operator = (std::string const& sv) {
        hash = 0;
        char_count = int32_t(sv.size());
        memset(words, 0, sizeof(words));
        memcpy(words, sv.data(), sv.size());
        return *this;
    }

    void reset()
    {
        memset(words, 0, sizeof(words));
        char_count = 0;
    }

    void append(std::string_view const& sv) {
        memcpy(reinterpret_cast<char*>(words) + char_count, sv.data(), sv.size());
        char_count += int32_t(sv.size());
    }
};
#pragma pack(pop)

template <typename T>
struct buffer_t
{
    // The actual handle
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;

    T* data = nullptr;

    uint32_t binding = 0;

    size_t item_count = 0;
    size_t max_size = sizeof(T);

    void setMaximumElementCount(size_t itemCount) {
        max_size = sizeof(T) * itemCount;
    }

    size_t size() {
        return item_count * sizeof(T);
    }

    void release(VkDevice device) {
        if (data != nullptr) {
            vkUnmapMemory(device, memory);
            data = nullptr;
        }

        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
    }

    void update(VkDevice device)
    {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = set;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet.descriptorCount = 1;

        writeDescriptorSet.dstBinding = binding;

        VkDescriptorBufferInfo bufferInfo { buffer, 0, max_size };

        writeDescriptorSet.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    }

    void invalidate() {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkInvalidateMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to invalidate memory");
    }

    void map(VkDevice device) {
        // map here because whatever, it has to be done anyways
        if (vkMapMemory(device, memory, 0, max_size, 0, (void**)&data) != VK_SUCCESS)
            throw std::runtime_error("failed to map");
    }

    void write_raw(VkDevice device, void* new_data, size_t size) {
        memcpy(data, new_data, size);

        // Flush writes to the device
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkFlushMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to flush memory");
    }

    void flush(VkDevice device) {
        // Flush writes to the device
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkFlushMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to flush memory");
    }
};
