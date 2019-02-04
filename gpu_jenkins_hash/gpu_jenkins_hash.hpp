#pragma once

#include <cstdint>
#include <vector>

#include "uploaded_string.hpp"

#include <vulkan/vulkan.h>
#include <functional>
#include <algorithm>
#include <optional>

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
    JenkinsGpuHash(uint32_t frameCount) {
		_frames.resize(frameCount);

		createInstance();
		setupDebugMessenger();
		pickPhysicalDevice();
		createLogicalDevice();
	}

    void run();

	template <typename F>
	inline void setDataProvider(F f) {
		_dataProvider = std::function<void(std::vector<uploaded_string>*)>(std::move(f));
	}

	template <typename F>
	inline void setOutputHandler(F f) {
		_outputHandler = std::function<void(std::vector<uploaded_string>*, uint32_t)>(std::move(f));
	}

	struct params_t {
		uint32_t workgroupCount = 0;
        uint32_t workgroupSize = 64;
	};

	void setWorkgroupCount(uint32_t size) {
		params.workgroupCount = std::min(_device.properties.limits.maxComputeWorkGroupCount[0], size);
	}

    void setWorkgroupSize(uint32_t size) {
        params.workgroupSize = std::min(_device.properties.limits.maxComputeWorkGroupSize[0], size);
    }

	params_t const& getParams() { return params; }

private:

	params_t params;

    std::function<void(std::vector<uploaded_string>*)> _dataProvider;
	std::function<void(std::vector<uploaded_string>*, uint32_t)> _outputHandler;

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

    bool submitWork(std::vector<uploaded_string> const& data, bool first = false);

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


