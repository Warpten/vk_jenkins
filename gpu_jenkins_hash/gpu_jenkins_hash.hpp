#pragma once

#include <cstdint>
#include <vector>

#include "uploaded_string.hpp"

#include <vulkan/vulkan.h>
#include <functional>
#include <optional>

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


class JenkinsGpuHash {
public:
    JenkinsGpuHash(uint32_t frameCount) : _frameCount(frameCount) { _frames.resize(frameCount); }

    void run();

    template <typename F>
    inline void setDataProvider(F f) {
        _dataProvider = std::function<uint32_t(std::array<uploaded_string, 64>*)>(std::move(f));
    }

private:

    std::function<uint32_t(std::array<uploaded_string, 64>*)> _dataProvider;

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
        buffer_t<uploaded_string, 64> deviceBuffer;
        buffer_t<uploaded_string, 64> hostBuffer;

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

    bool submitWork(std::array<uploaded_string, 64uLL> data);

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


