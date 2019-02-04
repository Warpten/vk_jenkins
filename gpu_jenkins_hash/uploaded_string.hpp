#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#pragma pack(push, 1)
struct uploaded_string {
    uint32_t word_count = 0;
    uint32_t hash = 0;
    uint32_t words[32 * 3] = {0};

    std::string value() const {
        return std::string(reinterpret_cast<const char*>(words), word_count);
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
    uint32_t item_count = 0;
    uint32_t max_size = sizeof(T);

    void setMaximumElementCount(uint32_t workgroup_count, uint32_t workgroup_size) {
        max_size = sizeof(T) * workgroup_size * workgroup_count;
    }

    uint32_t size() {
        return uint32_t(item_count * sizeof(T));
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

        memcpy(mapped, newData.data(), newData.size() * sizeof(T));
        item_count = (uint32_t)newData.size();

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

        memcpy(mapped, data.data(), data.size() * sizeof(T));
        item_count = (uint32_t)data.size();

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
