#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "utils.hpp"
#include "vma.h"

template <typename T>
struct buffer_t
{
    // The actual handle
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info{};
    VkDescriptorSet set = VK_NULL_HANDLE;

    // mapped data if any
    T* data = nullptr;

    // binding index
    uint32_t binding = 0;

    // actual number of elements in the buffer
    size_t item_count = 0;

    // sizeof(T)
    constexpr static const size_t item_size = sizeof(T);

    // total data size
    size_t size() {
        return item_count * sizeof(T);
    }

    /// Allocate memory for this buffer.
    void create(VmaAllocator allocator, VkBufferUsageFlags usage, VmaMemoryUsage memUsage, size_t dataSize) {
        std::cout << ">> Allocating " << pretty_bytesize(dataSize)
            << " of "
            << (memUsage == VMA_MEMORY_USAGE_GPU_ONLY ? "GPU" : "CPU")
            << " memory." << std::endl;

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = dataSize;
        bufferInfo.usage = usage;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memUsage;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
        vmaGetAllocationInfo(allocator, allocation, &allocation_info);
    }

    /// Releases handles and memory associated to the buffer.
    void release(VmaAllocator allocator) {
        if (data != nullptr) {
            vmaUnmapMemory(allocator, allocation);
            data = nullptr;
        }

        vmaDestroyBuffer(allocator, buffer, allocation);
    }

    /// Updates the buffer regarding its descriptor set.
    void update(VkDevice device)
    {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = set;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet.descriptorCount = 1;

        writeDescriptorSet.dstBinding = binding;

        VkDescriptorBufferInfo bufferInfo{ buffer, 0, allocation_info.size };

        writeDescriptorSet.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    }

    /// Invalidates memory of the allocation on the range specified.
    void invalidate(VmaAllocator allocator, VkDeviceSize size = VK_WHOLE_SIZE) {
        vmaInvalidateAllocation(allocator, allocation, 0, size);
    }

    /// Maps memory.
    void map(VmaAllocator allocator) {
        vmaMapMemory(allocator, allocation, (void**)&data);
    }

    /// Writes the provided bytes to the mapped memory and flushes the corresponding range.
    void write_raw(VmaAllocator allocator, void* new_data, size_t size) {
        memcpy(data, new_data, size);

        flush(allocator, size);
    }

    /// Flushes memory of the allocation on the range specified.
    void flush(VmaAllocator allocator, VkDeviceSize size = VK_WHOLE_SIZE) {
        vmaFlushAllocation(allocator, allocation, 0, size);
    }
};

