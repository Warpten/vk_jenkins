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
    JenkinsGpuHash(uint32_t frameCount) : _frameCount(frameCount) { _frames.resize(frameCount); }

    void run();

private:

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;

    Device _device;

    VkQueue _computeQueue;

    Descriptor _descriptor;

    Pipeline _pipeline;

    VkCommandPool _commandPool;

    uint32_t _frameCount;
    uint32_t _currentFrame;

    struct Frame {
        buffer_t<uint32_t> output;
        buffer_t<uploaded_string> input;

        Buffer stagingInput;

        VkCommandBuffer commandBuffer;

        VkSemaphore semaphore;

        VkFence flightFence;
    };

    std::vector<Frame> _frames;

    void initVulkan();

    void mainLoop();

    void cleanup();

    void createInstance();

    void setupDebugMessenger();

    void pickPhysicalDevice();

    void createLogicalDevice();

    void createComputePipeline();

    void createCommandPool();

    void createCommandBuffers();

    void submitWork();

    void createBuffers();

    void uploadInput(std::vector<uploaded_string> const& input);

    VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);

    VkShaderModule createShaderModule(const std::vector<char>& code);

    bool isDeviceSuitable(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    std::vector<const char*> getRequiredExtensions();

    bool checkValidationLayerSupport();

    static std::vector<char> readFile(const std::string& filename);

    void createSyncObjects();
};


