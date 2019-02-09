#pragma once

#include <cstdint>
#include <string>
#include <algorithm>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

#include "lookup3.hpp"

struct pattern_t;

#pragma pack(push, 1)
struct uploaded_string {
     // easier for this to have internals access
    friend struct pattern_t;

private:
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

    uploaded_string& operator = (std::string_view const& sv) {
        hash = 0;
        char_count = sv.size();
        memset(words, 0, sizeof(words));
        memcpy(words, sv.data(), sv.size());
        return *this;
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

    void* mapped = nullptr;

    uint32_t binding = 0;

    size_t item_count = 0;
    size_t max_size = sizeof(T);

    void setMaximumElementCount(uint32_t workgroup_count, uint32_t workgroup_size) {
        max_size = sizeof(T) * size_t(workgroup_size) * size_t(workgroup_count);
    }

    size_t size() {
        return item_count * sizeof(T);
    }

    void release(VkDevice device) {
        if (mapped != nullptr) {
            vkUnmapMemory(device, memory);
            mapped = nullptr;
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

    std::vector<T> read(VkDevice device)
    {
        if (mapped == nullptr)
        {
            if (vkMapMemory(device, memory, 0, max_size, 0, &mapped) != VK_SUCCESS)
                return {};
        }

        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkInvalidateMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to invalidate memory");

        std::vector<T> data(item_count);
        memcpy(data.data(), mapped, size());

        return data;
    }

    std::vector<T> swap(VkDevice device, std::vector<T> newData) {
        if (mapped == nullptr)
        {
            if (vkMapMemory(device, memory, 0, max_size, 0, &mapped) != VK_SUCCESS)
                return {};
        }

        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkInvalidateMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to invalidate memory");

        std::vector<T> oldData(item_count);
        memcpy(oldData.data(), mapped, size());

        memset(mapped, 0, max_size);
        memcpy(mapped, newData.data(), newData.size() * sizeof(T));
        item_count = newData.size();

        // Flush writes to the device
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkFlushMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to flush memory");

        return oldData;
    }

    void write_raw(VkDevice device, void* data, size_t size)
    {
        if (mapped == nullptr)
        {
            if (vkMapMemory(device, memory, 0, max_size, 0, &mapped) != VK_SUCCESS)
                return;
        }

        memcpy(mapped, data, size);

        // Flush writes to the device
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        if (vkFlushMappedMemoryRanges(device, 1, &mappedRange) != VK_SUCCESS)
            throw std::runtime_error("Failed to flush memory");
    }

    void write(VkDevice device, std::vector<T> data)
    {
        if (mapped == nullptr)
        {
            if (vkMapMemory(device, memory, 0, max_size, 0, &mapped) != VK_SUCCESS)
                return;
        }

        memset(mapped, 0, max_size);
        memcpy(mapped, data.data(), data.size() * sizeof(T));
        item_count = data.size();

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
