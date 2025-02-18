#include <iostream>
#include <sstream>
#include <cstdint>
#include <optional>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>
#include <array>
#include <string>

#include "gpu_jenkins_hash.hpp"
#include "renderdoc.hpp"
#include "metrics.hpp"

#include <vulkan/vulkan.h>

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

const std::vector<const char*> validationLayers = {
    "VK_LAYER_LUNARG_standard_validation"
};

#ifdef NDEBUG
constexpr const bool enableValidationLayers = false;
#else
constexpr const bool enableValidationLayers = true;
#endif


VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

void JenkinsGpuHash::run()
{
    createCommandPool();
    createBuffers();

    createComputePipeline();
    createSyncObjects();

    createCommandBuffers();

    renderdoc::init();

    mainLoop();

    // called by atexit
    // cleanup();
}

void JenkinsGpuHash::createBuffers()
{
    for (Frame& frame : _frames) {
        // Input staging buffer
        frame.hostInputBuffer.create(_device.allocator,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            params.getCompleteDataSize() * frame.hostInputBuffer.item_size);

        frame.hostOutputBuffer.create(_device.allocator,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_TO_CPU,
            params.getCompleteDataSize() * frame.hostOutputBuffer.item_size);

        frame.deviceBuffer.create(_device.allocator,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            params.getCompleteDataSize() * frame.deviceBuffer.item_size);

        // Input buffer on binding 0
        frame.deviceBuffer.binding = 0;

        frame.hostInputBuffer.map(_device.allocator);
        frame.hostOutputBuffer.map(_device.allocator);
    }

    dispatchBuffer.create(_device.allocator,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, sizeof(VkDispatchIndirectCommand));
}

void JenkinsGpuHash::mainLoop()
{
    auto provide_data = [this](uploaded_string* data) -> size_t {
        return this->_dataProvider(data, this->params.getCompleteDataSize());
    };
    try {
        metrics::start();

        VkResult result;

		std::cout << ">> Initializing (this may take a while, sit tight!)" << std::endl;

        // Execute each frame once so that we can have output for the main loop
        for (Frame& currentFrame : _frames) {
            // begin the frame
            beginFrame();

            // Upload data
            size_t written_count = provide_data(currentFrame.hostInputBuffer.data);
            currentFrame.hostInputBuffer.item_count = written_count;

            // Nothing left to process?
            if (written_count == 0)
                break;

            // Increment metrics
            metrics::increment(written_count);

            // Flush memory to the device
            currentFrame.hostInputBuffer.flush(_device.allocator, written_count * currentFrame.hostInputBuffer.item_count);

            result = submitFrame();
#if _DEBUG
            if (result != VK_SUCCESS)
                throw std::runtime_error("vkQueueSubmit failed");
#endif
        }

        _currentFrame = 0;

		std::cout << ">> Hashing ..." << std::endl;

        while (true) {
            beginFrame();

			Frame& currentFrame = _frames[_currentFrame];

            // Handle previous output
			currentFrame.hostOutputBuffer.invalidate(_device.allocator);
            _outputHandler(currentFrame.hostOutputBuffer.data, currentFrame.hostInputBuffer.item_count);

            // Write new input
            size_t written_count = provide_data(currentFrame.hostInputBuffer.data);
            currentFrame.hostInputBuffer.item_count = written_count;

            // Nothing left to process?
            if (written_count == 0)
                break;

			currentFrame.hostInputBuffer.flush(_device.allocator);

            metrics::increment(written_count);

            // execute frame
            result = submitFrame();
#if _DEBUG
            if (result != VK_SUCCESS)
                throw std::runtime_error("vkQueueSubmit failed");
#endif
        }

        std::cout << ">> Finalizing ..." << std::endl;

        // We may have left the loop because we got no data to send,
        // but there is still some data left in the pipe
        // We thus must iterate a third time, but just collect data
        for (size_t i = 0; i < _frames.size(); ++i) {
            if (_currentFrame == _frames.size())
                _currentFrame = 0;

            Frame& frame = _frames[_currentFrame++];

            // There was no data in the pipe
            if (frame.hostInputBuffer.item_count == 0)
                continue;

            vkWaitForFences(_device.device, 1, &frame.flightFence, VK_TRUE, UINT64_MAX);
            _outputHandler(frame.hostOutputBuffer.data, frame.hostOutputBuffer.item_count);

        }

        metrics::stop();

		std::cout << ">> Done!" << std::endl;
    }
    catch (std::exception const& e) {
        std::cerr << e.what() << std::endl;
    }

    vkDeviceWaitIdle(_device.device);
}

VkShaderModule JenkinsGpuHash::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); //-V206

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(_device.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("failed to create shader module!");

    return shaderModule;
}

void JenkinsGpuHash::cleanup()
{
    for (Frame& frame : _frames)
        frame.clear(_device.device, _device.allocator);

    dispatchBuffer.release(_device.allocator);

    vkDestroyCommandPool(_device.device, _commandPool, nullptr);

    vkDestroyDescriptorSetLayout(_device.device, _descriptor.setLayout, nullptr);
    vkDestroyDescriptorPool(_device.device, _descriptor.pool, nullptr);

    vkDestroyPipeline(_device.device, _pipeline.pipeline, nullptr);
    vkDestroyPipelineLayout(_device.device, _pipeline.layout, nullptr);

    vmaDestroyAllocator(_device.allocator);

    vkDestroyDevice(_device.device, nullptr);

    vkDestroyInstance(_instance, nullptr);
}

void JenkinsGpuHash::createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport())
        throw std::runtime_error("validation layers requested, but not available!");

    VkApplicationInfo appInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Jenkins GPU Bruteforcer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Jenkins GPU Bruteforcer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }

    if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
        throw std::runtime_error("failed to create instance!");
}

void JenkinsGpuHash::setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("failed to set up debug messenger!");
}

void JenkinsGpuHash::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
        throw std::runtime_error("failed to find GPUs with Vulkan support!");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            _device.physicalDevice = device;
            break;
        }
    }

    if (_device.physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    vkGetPhysicalDeviceProperties(_device.physicalDevice, &_device.properties);
}

void JenkinsGpuHash::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(_device.physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.computeFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures {};

    VkDeviceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(_device.physicalDevice, &createInfo, nullptr, &_device.device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.device = _device.device;
    allocInfo.frameInUseCount = 0;
    allocInfo.pHeapSizeLimit = nullptr;
    allocInfo.physicalDevice = _device.physicalDevice;
    vmaCreateAllocator(&allocInfo, &_device.allocator);

    vkGetDeviceQueue(_device.device, indices.computeFamily.value(), 0, &_computeQueue);
}

void JenkinsGpuHash::createComputePipeline()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)_frames.size() },
    };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = (uint32_t)_frames.size();
    if (vkCreateDescriptorPool(_device.device, &descriptorPoolInfo, nullptr, &_descriptor.pool) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor pool");

    // Descriptor set bindings.
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings {
        VkDescriptorSetLayoutBinding{ 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    };

    // Create the descriptor set layout.
    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
    descriptorLayout.pBindings = setLayoutBindings.data();
    if (vkCreateDescriptorSetLayout(_device.device, &descriptorLayout, nullptr, &_descriptor.setLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set layout!");

    // Create the pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &_descriptor.setLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(_device.device, &pipelineLayoutCreateInfo, nullptr, &_pipeline.layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline layout!");

    // Allocate descriptor sets.

    for (size_t i = 0; i < _frames.size(); ++i)
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = _descriptor.pool;
        allocInfo.pSetLayouts = &_descriptor.setLayout;
        allocInfo.descriptorSetCount = 1;
        if (vkAllocateDescriptorSets(_device.device, &allocInfo, &_frames[i].deviceBuffer.set) != VK_SUCCESS)
            throw std::runtime_error("failed to create descriptor set!");
    }

    auto computeShaderCode = readFile("shaders/comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(computeShaderCode);

    std::vector<VkSpecializationMapEntry> specMapEntries{
        VkSpecializationMapEntry{ 1, 0, 4 }, // Constant ID, offset, size
        VkSpecializationMapEntry{ 2, 4, 4 }, // Constant ID, offset, size
        VkSpecializationMapEntry{ 3, 8, 4 }, // Constant ID, offset, size
    };

    VkSpecializationInfo shaderSpecInfo{};
    shaderSpecInfo.mapEntryCount = static_cast<uint32_t>(specMapEntries.size());
    shaderSpecInfo.pMapEntries = specMapEntries.data();

    shaderSpecInfo.dataSize = sizeof(params.workgroupSize);
    shaderSpecInfo.pData = params.workgroupSize;

    // Pipeline shader stage info.
    VkPipelineShaderStageCreateInfo compShaderStageInfo{};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = computeShaderModule;
    compShaderStageInfo.pName = "main";
    compShaderStageInfo.pSpecializationInfo = &shaderSpecInfo;

    // Create the pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = _pipeline.layout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.stage = compShaderStageInfo;

    if (vkCreateComputePipelines(_device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline.pipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics pipeline!");

    vkDestroyShaderModule(_device.device, computeShaderModule, nullptr);

    { // Upload the constants for indirect dispatch now that we have a pipeline
        renderdoc::begin_frame();

        VkCommandBuffer uploadCmd;

        // Reuse the first frame's staging buffer for this upload
        VkDispatchIndirectCommand dispatch_args{ params.workgroupCount[0], params.workgroupCount[1], params.workgroupCount[2] };
        _frames[0].hostInputBuffer.write_raw(_device.allocator, &dispatch_args, sizeof(VkDispatchIndirectCommand));

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(_device.device, &allocInfo, &uploadCmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(uploadCmd, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("failed to begin recording command buffer!");

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(VkDispatchIndirectCommand);
        copyRegion.dstOffset = 0;
        copyRegion.srcOffset = 0;
        vkCmdCopyBuffer(uploadCmd, _frames[0].hostInputBuffer.buffer, dispatchBuffer.buffer, 1, &copyRegion);

        vkEndCommandBuffer(uploadCmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pCommandBuffers = &uploadCmd;
        submitInfo.commandBufferCount = 1;

        vkQueueSubmit(_computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
        renderdoc::end_frame();

        vkQueueWaitIdle(_computeQueue);

        vkFreeCommandBuffers(_device.device, _commandPool, 1, &uploadCmd);
    }
}

void JenkinsGpuHash::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(_device.physicalDevice);

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();

    if (vkCreateCommandPool(_device.device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create command pool!");
}

void JenkinsGpuHash::createCommandBuffers()
{
    for (Frame& frame : _frames) {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(_device.device, &allocInfo, &frame.commandBuffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("failed to begin recording command buffer!");

        frame.deviceBuffer.update(_device.device);

        VkBufferCopy copyRegion{};
        copyRegion.size = frame.hostInputBuffer.allocation_info.size;
        copyRegion.dstOffset = 0;
        copyRegion.srcOffset = 0;
        vkCmdCopyBuffer(frame.commandBuffer, frame.hostInputBuffer.buffer, frame.deviceBuffer.buffer, 1, &copyRegion);

        VkBufferMemoryBarrier bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.buffer = frame.deviceBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.buffer = frame.deviceBuffer.buffer;
        bufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(frame.commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline.pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            _pipeline.layout,
            0,
            1,
            &frame.deviceBuffer.set,
            0,
            nullptr);

        vkCmdDispatchIndirect(frame.commandBuffer, dispatchBuffer.buffer, 0);

        // Barrier to ensure that shader writes are finished before buffer is read back from GPU
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bufferBarrier.buffer = frame.deviceBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(
            frame.commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        vkCmdCopyBuffer(frame.commandBuffer, frame.deviceBuffer.buffer, frame.hostOutputBuffer.buffer, 1, &copyRegion);

        // Barrier to ensure that buffer copy is finished before host reading from it
        bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        bufferBarrier.buffer = frame.hostOutputBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(
            frame.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("failed to record command buffer!");
    }
}

void JenkinsGpuHash::beginFrame()
{
    renderdoc::begin_frame();

    vkWaitForFences(_device.device, 1, &_frames[_currentFrame].flightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(_device.device, 1, &_frames[_currentFrame].flightFence);
}

VkResult JenkinsGpuHash::submitFrame()
{
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_frames[_currentFrame].commandBuffer;

    VkResult result = vkQueueSubmit(_computeQueue, 1, &submitInfo, _frames[_currentFrame].flightFence);

    renderdoc::end_frame();

    _currentFrame = (_currentFrame + 1);
    if (_currentFrame == _frames.size()) {
        _currentFrame = 0;
    }

    return result;
}

bool JenkinsGpuHash::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = findQueueFamilies(device);
    return indices.isComplete();
}

QueueFamilyIndices JenkinsGpuHash::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.computeFamily = i;
        }

        if (indices.isComplete())
            break;

        i++;
    }

    return indices;
}

std::vector<const char*> JenkinsGpuHash::getRequiredExtensions()
{
    std::vector<const char*> extensions;

    if (enableValidationLayers)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

bool JenkinsGpuHash::checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<char> JenkinsGpuHash::readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

void JenkinsGpuHash::createSyncObjects()
{
    for (Frame& frame : _frames)
    {
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(_device.device, &createInfo, nullptr, &frame.transferSemaphore);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence(_device.device, &fenceInfo, nullptr, &frame.flightFence);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}
