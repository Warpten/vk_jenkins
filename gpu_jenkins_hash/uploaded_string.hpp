#pragma once

#include <cstdint>
#include <array>
#include <vulkan/vulkan.h>

#pragma pack(push, 1)
struct uploaded_string {
    uint32_t word_count = 0;
    uint32_t words[32 * 3] = {0};
};
#pragma pack(pop)

template <typename T, size_t N = 64>
struct buffer_t
{
    // The actual handle
    VkBuffer buffer;
    VkDeviceMemory memory;

    uint32_t binding;

    VkDescriptorSet set;

    constexpr static const size_t item_count = N;
    constexpr static const size_t size = item_count * sizeof(T);

    void update(VkDevice device)
    {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = set;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet.descriptorCount = 1;

        writeDescriptorSet.dstBinding = binding;

        VkDescriptorBufferInfo bufferInfo { buffer, 0, VK_WHOLE_SIZE };

        writeDescriptorSet.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
    }

    std::array<T, item_count> read(VkDevice device)
    {
        void* mapped;
        if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS)
            return {};

        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(device, 1, &mappedRange);

        std::array<T, item_count> data;
        memcpy(data.data(), mapped, size);

        vkUnmapMemory(device, memory);
        return data;
    }

    void write(VkDevice device, std::array<T, item_count> data)
    {
        void* mapped;
        if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS)
            return;

        memcpy(mapped, data.data(), size);

        // Flush writes to the device
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.offset = 0;
        mappedRange.size = data.size();
        vkFlushMappedMemoryRanges(device, 1, &mappedRange);
        vkUnmapMemory(device, memory);
    }
};