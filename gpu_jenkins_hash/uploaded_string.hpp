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

    uint32_t binding;

    VkDescriptorSet set;

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
};