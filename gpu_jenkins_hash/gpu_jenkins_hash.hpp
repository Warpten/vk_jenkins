#pragma once

#include <cstdint>
#include <vector>

#include "uploaded_string.hpp"

#include <vulkan/vulkan.h>
#include <functional>
#include <algorithm>
#include <optional>

// FUTURE
/*
struct jenkins_shader_32 {
    using result_type = uint32_t;

    constexpr static const char shader_name[] = "shaders/jenkins32.comp";
};

struct jenkins_shader_64 {
    using result_type = uint64_t;

    constexpr static const char shader_name[] = "shaders/jenkins64.comp";
};
*/

struct Device {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties = { 0 };
};

struct Descriptor {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
};

struct Pipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> computeFamily;

    bool isComplete() {
        return computeFamily.has_value();
    }
};

class JenkinsGpuHash {
public:
    JenkinsGpuHash(size_t frameCount) {
        _frames.resize(frameCount);

        createInstance();
        setupDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();
    }

    void run();

    template <typename F>
    inline void setDataProvider(F f) {
        _dataProvider = std::function<size_t(uploaded_string*, size_t)>(std::move(f));
    }

    template <typename F>
    inline void setOutputHandler(F f) {
        _outputHandler = std::function<void(uploaded_string*, size_t)>(std::move(f));
    }

    struct params_t {
        uint32_t workgroupCount[3] = { 0, 0, 0 };
        uint32_t workgroupSize[3] = { 64, 0, 0 };

		size_t getCompleteDataSize() const {
			size_t promotedSize { workgroupSize[0] }; //-V101
			promotedSize *= workgroupSize[1]; //-V101
			promotedSize *= workgroupSize[2]; //-V101
			promotedSize *= workgroupCount[0]; //-V101
			promotedSize *= workgroupCount[1]; //-V101
			promotedSize *= workgroupCount[2]; //-V101
			return promotedSize;
        }
    };

    void setWorkgroupCount(uint32_t x, uint32_t y, uint32_t z) {
        params.workgroupCount[0] = std::min(_device.properties.limits.maxComputeWorkGroupCount[0], x);
        params.workgroupCount[1] = std::min(_device.properties.limits.maxComputeWorkGroupCount[1], y);
        params.workgroupCount[2] = std::min(_device.properties.limits.maxComputeWorkGroupCount[2], z);
    }

    void setWorkgroupSize(uint32_t x, uint32_t y, uint32_t z) {
        params.workgroupSize[0] = std::min(_device.properties.limits.maxComputeWorkGroupSize[0], x);
        params.workgroupSize[1] = std::min(_device.properties.limits.maxComputeWorkGroupSize[1], y);
        params.workgroupSize[2] = std::min(_device.properties.limits.maxComputeWorkGroupSize[2], z);
    }

    params_t const& getParams() const { return params; }
    size_t getFrameCount() const { return _frames.size(); }

private:

    params_t params;

    std::function<size_t(uploaded_string*, size_t)> _dataProvider;
    std::function<void(uploaded_string*, size_t)> _outputHandler;

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;

    Device _device;

    VkQueue _computeQueue = VK_NULL_HANDLE;

    Descriptor _descriptor;

    Pipeline _pipeline;

    VkCommandPool _commandPool = VK_NULL_HANDLE;

    size_t _currentFrame = 0u;

    struct Frame {
        buffer_t<uploaded_string> deviceBuffer;
        buffer_t<uploaded_string> hostBuffer;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkSemaphore semaphore = VK_NULL_HANDLE;

        VkFence flightFence = VK_NULL_HANDLE;

        void clear(VkDevice device) {
            vkDestroyFence(device, flightFence, nullptr);
            vkDestroySemaphore(device, semaphore, nullptr);

            deviceBuffer.release(device);
            hostBuffer.release(device);
        }
    };
    buffer_t<VkDispatchIndirectCommand> dispatchBuffer;

    std::vector<Frame> _frames;

    void mainLoop();

    void cleanup();

    void createInstance();

    void setupDebugMessenger();

    void pickPhysicalDevice();

    void createLogicalDevice();

    void createComputePipeline();

    void createCommandPool();

    void createCommandBuffers();

    void beginFrame();
    VkResult submitFrame();

    void createBuffers();

    VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);

    VkShaderModule createShaderModule(const std::vector<char>& code);

    bool isDeviceSuitable(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    std::vector<const char*> getRequiredExtensions();

    bool checkValidationLayerSupport();

    static std::vector<char> readFile(const std::string& filename);

    void createSyncObjects();

public:
    VkPhysicalDeviceProperties const& getDeviceProperties() {
        return _device.properties;
    }
};


