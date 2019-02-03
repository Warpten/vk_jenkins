#include <iostream>
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
    initVulkan();

    renderdoc::init();

    mainLoop();
    cleanup();
}

void JenkinsGpuHash::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();

    createBuffers();

    createComputePipeline();
    createCommandPool();
    createSyncObjects();

    createCommandBuffers();
}

void JenkinsGpuHash::createBuffers()
{
    VkDeviceSize inputBufferSize = 64 * sizeof(uploaded_string);
    VkDeviceSize outputBufferSize = 64 * sizeof(uint32_t);

    for (uint32_t i = 0; i < _frameCount; ++i) {
        // Staging buffer
        createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &_frames[i].hostBuffer.buffer,
            &_frames[i].hostBuffer.memory,
            inputBufferSize);

        // Device buffer
        createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &_frames[i].deviceBuffer.buffer,
            &_frames[i].deviceBuffer.memory,
            inputBufferSize);

        // Input buffer on binding 0
        _frames[i].deviceBuffer.binding = 0;

    }
}

void JenkinsGpuHash::mainLoop()
{
    while (true) {
        std::array<uploaded_string, 64uLL> data;
        uint32_t count = _dataProvider(&data);
        if (count == 0)
            break;

        submitWork(data);
    }

    vkDeviceWaitIdle(_device.device);
}

VkResult JenkinsGpuHash::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data)
{
    // Create the buffer handle
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = usageFlags;
    bufferCreateInfo.size = size;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device.device, &bufferCreateInfo, nullptr, buffer) != VK_SUCCESS)
        throw std::runtime_error("unable to create buffer");

    // Create the memory backing up the buffer handle
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(_device.physicalDevice, &deviceMemoryProperties);
    VkMemoryRequirements memReqs;
    VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vkGetBufferMemoryRequirements(_device.device, *buffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    // Find a memory type index that fits the properties of the buffer
    bool memTypeFound = false;
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & 1) == 1) {
            if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags) {
                memAlloc.memoryTypeIndex = i;
                memTypeFound = true;
            }
        }
        memReqs.memoryTypeBits >>= 1;
    }

    if (!memTypeFound)
        throw std::runtime_error("memory type not found");

    if (vkAllocateMemory(_device.device, &memAlloc, nullptr, memory) != VK_SUCCESS)
        throw std::runtime_error("unable to allocate memory for the buffer");

    if (data != nullptr) {
        void *mapped;
        if (vkMapMemory(_device.device, *memory, 0, size, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("Unable to map buffer");
        memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(_device.device, *memory);
    }

    if (vkBindBufferMemory(_device.device, *buffer, *memory, 0) != VK_SUCCESS)
        throw std::runtime_error("unable to bind buffer memory");

    return VK_SUCCESS;
}

VkShaderModule JenkinsGpuHash::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(_device.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("failed to create shader module!");

    return shaderModule;
}

void JenkinsGpuHash::cleanup()
{
    vkDestroyCommandPool(_device.device, _commandPool, nullptr);

    vkDestroyPipeline(_device.device, _pipeline.pipeline, nullptr);
    vkDestroyPipelineLayout(_device.device, _pipeline.layout, nullptr);

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

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

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

    vkGetDeviceQueue(_device.device, indices.computeFamily.value(), 0, &_computeQueue);
}

void JenkinsGpuHash::createComputePipeline()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frameCount },
    };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = _frameCount;
    if (vkCreateDescriptorPool(_device.device, &descriptorPoolInfo, nullptr, &_descriptor.pool) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor pool");

    // Descriptor set bindings.
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings {
        VkDescriptorSetLayoutBinding{ 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    };

    // Create the descriptor set layout.
    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.bindingCount = setLayoutBindings.size();
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

    for (uint32_t i = 0; i < _frames.size(); ++i)
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = _descriptor.pool;
        allocInfo.pSetLayouts = &_descriptor.setLayout;
        allocInfo.descriptorSetCount = 1;
        if (vkAllocateDescriptorSets(_device.device, &allocInfo, &_frames[i].deviceBuffer.set) != VK_SUCCESS)
            throw std::runtime_error("failed to create descriptor set!");
    }

    _frames[0].deviceBuffer.update(_device.device);

    auto computeShaderCode = readFile("shaders/comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(computeShaderCode);

    // Pipeline shader stage info.
    VkPipelineShaderStageCreateInfo compShaderStageInfo{};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = computeShaderModule;
    compShaderStageInfo.pName = "main";

    // Create the pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = _pipeline.layout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.stage = compShaderStageInfo;

    if (vkCreateComputePipelines(_device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline.pipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics pipeline!");

    vkDestroyShaderModule(_device.device, computeShaderModule, nullptr);
}

void JenkinsGpuHash::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(_device.physicalDevice);

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();

    if (vkCreateCommandPool(_device.device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void JenkinsGpuHash::createCommandBuffers()
{
    for (size_t i = 0; i < _frames.size(); i++) {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(_device.device, &allocInfo, &_frames[i].commandBuffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(_frames[i].commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("failed to begin recording command buffer!");

        _frames[i].deviceBuffer.update(_device.device);

        VkBufferCopy copyRegion{};
        copyRegion.size = 64uLL * sizeof(uploaded_string);
        copyRegion.dstOffset = 0;
        copyRegion.srcOffset = 0;
        vkCmdCopyBuffer(_frames[i].commandBuffer, _frames[i].hostBuffer.buffer, _frames[i].deviceBuffer.buffer, 1, &copyRegion);

        // Barrier for cmdCopyBuffer above
        VkBufferMemoryBarrier bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.buffer = _frames[i].deviceBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(_frames[i].commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        vkCmdBindPipeline(_frames[i].commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline.pipeline);
        uint32_t dynamicOffset = sizeof(uploaded_string) * _currentFrame;
        vkCmdBindDescriptorSets(_frames[i].commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            _pipeline.layout,
            0,
            1,
            &_frames[i].deviceBuffer.set,
            0,
            nullptr);
        vkCmdDispatch(_frames[i].commandBuffer,
            64, //_device.properties.limits.maxComputeWorkGroupCount[0],
            1,
            1);

        // Barrier to ensure that shader writes are finished before buffer is read back from GPU
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bufferBarrier.buffer = _frames[i].deviceBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(
            _frames[i].commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        vkCmdCopyBuffer(_frames[i].commandBuffer, _frames[i].deviceBuffer.buffer, _frames[i].hostBuffer.buffer, 1, &copyRegion);

        // Barrier to ensure that buffer copy is finished before host reading from it
        bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        bufferBarrier.buffer = _frames[i].hostBuffer.buffer;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(
            _frames[i].commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0,
            0, nullptr,
            1, &bufferBarrier,
            0, nullptr);

        if (vkEndCommandBuffer(_frames[i].commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("failed to record command buffer!");
    }
}

bool JenkinsGpuHash::submitWork(std::array<uploaded_string, 64uLL> inputData)
{
    renderdoc::begin_frame();

    vkResetFences(_device.device, 1, &_frames[_currentFrame].flightFence);

    _frames[_currentFrame].hostBuffer.write(_device.device, inputData);

    VkSubmitInfo submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_frames[_currentFrame].commandBuffer;

    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    VkResult result = vkQueueSubmit(_computeQueue, 1, &submitInfo, _frames[_currentFrame].flightFence);
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string{ "failed to submit command buffer! Frame #" } + std::to_string(_currentFrame));

    vkWaitForFences(_device.device, 1, &_frames[_currentFrame].flightFence, VK_TRUE, UINT64_MAX);
    auto outputData = _frames[_currentFrame].deviceBuffer.read(_device.device);

    renderdoc::end_frame();

    vkDeviceWaitIdle(_device.device);

    _currentFrame = (_currentFrame + 1);
    if (_currentFrame == _frameCount)
        _currentFrame = 0;

    return true;
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

        vkCreateSemaphore(_device.device, &createInfo, nullptr, &frame.semaphore);

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
