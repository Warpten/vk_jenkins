#pragma once

#include <cstdint>
#include <vector>

#include "gpu_string.hpp"
#include "uploaded_string.hpp"

#include <vulkan/vulkan.h>

struct Device {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties;
};

struct Descriptor {
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkDescriptorSetLayout setLayout;
};

struct Pipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct CommandPool {
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> computeFamily;

    bool isComplete() {
        return computeFamily.has_value();
    }
};

struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

class JenkinsGpuHash {
public:
    void run();

private:

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;

    Device _device;

    VkQueue _computeQueue;

    Descriptor _descriptor;

    // Buffer to which the compute shader writes.
    std::vector<buffer_t<uint32_t>> _outputBuffers;
    // Buffer from which the compute shader reads.
    std::vector<buffer_t<uploaded_string>> _inputBuffers;

    // Staging buffer to the input, host-visible (can be memory mapped)
    std::vector<Buffer> _stagingBuffers;

    Pipeline _pipeline;

    CommandPool _commandPool;

    void initVulkan();

    void mainLoop();

    void cleanup();

    void createInstance();

    void setupDebugMessenger();

    void pickPhysicalDevice();

    void createLogicalDevice();

    void createComputePipeline();

    void createCommandPool();

    void createCommandBuffers(uint32_t frameCount = 1);

    void submitWork();

    void createBuffers(uint32_t frameCount = 1);

    void uploadInput(std::vector<uploaded_string> const& input, uint32_t currentFrame);

    VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);

    VkShaderModule createShaderModule(const std::vector<char>& code);

    bool isDeviceSuitable(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    std::vector<const char*> getRequiredExtensions();

    bool checkValidationLayerSupport();

    static std::vector<char> readFile(const std::string& filename);

};


