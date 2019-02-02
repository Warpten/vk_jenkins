
#include <iostream>
#include <cstdint>
#include <optional>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

#include "gpu_jenkins_hash.hpp"

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

int main() {
    JenkinsGpuHash app;

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void JenkinsGpuHash::run()
{
    initVulkan();
    mainLoop();
    cleanup();
}

void JenkinsGpuHash::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();

    createBuffers(1);

    createComputePipeline();
    createCommandPool();

    createCommandBuffers(1);
}

void JenkinsGpuHash::createBuffers(uint32_t frameCount)
{
    VkDeviceSize inputBufferSize = _device.properties.limits.maxComputeWorkGroupSize[0] * sizeof(uploaded_string);
    VkDeviceSize outputBufferSize = _device.properties.limits.maxComputeWorkGroupSize[0] * sizeof(uint32_t);

    _stagingBuffers.resize(frameCount);
    _inputBuffers.resize(frameCount);
    _outputBuffers.resize(frameCount);

    for (uint32_t i = 0; i < frameCount; ++i) {
        createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            &_stagingBuffers[i].buffer,
            &_stagingBuffers[i].memory,
            inputBufferSize);

        createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &_inputBuffers[i].buffer,
            &_inputBuffers[i].memory,
            inputBufferSize);

        createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &_outputBuffers[i].buffer,
            &_outputBuffers[i].memory,
            outputBufferSize);
    }

    /*// Copy to staging buffer
    VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
    cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool = _commandPool.commandPool;
    cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer copyCmd;
    vkAllocateCommandBuffers(_device.device, &cmdBufAllocateInfo, &copyCmd);

    VkCommandBufferBeginInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(copyCmd, &cmdBufInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(copyCmd, _stagingBuffer.buffer, _inputBuffer.buffer, 1, &copyRegion);
    vkEndCommandBuffer(copyCmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &copyCmd;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;

    VkFence fence;
    vkCreateFence(_device.device, &fenceInfo, nullptr, &fence);

    vkQueueSubmit(_computeQueue, 1, &submitInfo, fence);
    vkWaitForFences(_device.device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(_device.device, fence, nullptr);
    vkFreeCommandBuffers(_device.device, _commandPool.commandPool, 1, &copyCmd);*/
}

void JenkinsGpuHash::uploadInput(std::vector<uploaded_string> const& input, uint32_t currentFrame)
{
    void* mapped;
    vkMapMemory(_device.device, _stagingBuffers[currentFrame].memory, 0, VK_WHOLE_SIZE, 0, &mapped);

    memcpy(mapped, input.data(), input.size() * sizeof(uploaded_string));

    // Flush writes to the device
    VkMappedMemoryRange mappedRange{};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = _stagingBuffers[currentFrame].memory;
    mappedRange.offset = 0;
    mappedRange.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(_device.device, 1, &mappedRange);
    vkUnmapMemory(_device.device, _stagingBuffers[currentFrame].memory);
}

void JenkinsGpuHash::mainLoop()
{
    while (true) {
        submitWork();
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
    VkShaderModuleCreateInfo createInfo = {};
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
    vkDestroyCommandPool(_device.device, _commandPool.commandPool, nullptr);

    vkDestroyPipeline(_device.device, _pipeline.pipeline, nullptr);
    vkDestroyPipelineLayout(_device.device, _pipeline.layout, nullptr);

    vkDestroyDevice(_device.device, nullptr);

    vkDestroyInstance(_instance, nullptr);
}

void JenkinsGpuHash::createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport())
        throw std::runtime_error("validation layers requested, but not available!");

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Jenkins GPU Bruteforcer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Jenkins GPU Bruteforcer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {};
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

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
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
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
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
    auto computeShaderCode = readFile("shaders/comp.spv");

    VkShaderModule computeShaderModule = createShaderModule(computeShaderCode);

    // Descriptor set bindings.
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        // Binding 0: Input buffer (read only)
        VkDescriptorSetLayoutBinding{ 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Binding 1: Output buffer (write)
        VkDescriptorSetLayoutBinding{ 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };

    // Create the descriptor set layout.
    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.bindingCount = setLayoutBindings.size();
    descriptorLayout.pBindings = setLayoutBindings.data();
    if (vkCreateDescriptorSetLayout(_device.device, &descriptorLayout, nullptr, &_descriptor.setLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set layout!");

    // Pipeline shader stage info.
    VkPipelineShaderStageCreateInfo compShaderStageInfo{};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = computeShaderModule;
    compShaderStageInfo.pName = "main";

    std::vector<VkDescriptorPoolSize> poolSizes = {
        // Compute pipelines uses a storage buffer for reads and writes
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
    };
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(_device.device, &descriptorPoolInfo, nullptr, &_descriptor.pool) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor pool");

    // Allocate descriptor sets.
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = _descriptor.pool;
    descriptorSetAllocateInfo.pSetLayouts = &_descriptor.setLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    if (vkAllocateDescriptorSets(_device.device, &descriptorSetAllocateInfo, &_descriptor.set) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set!");

    // Create the pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptor.setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(_device.device, &pipelineLayoutInfo, nullptr, &_pipeline.layout) != VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline layout!");

    for (uint32_t i = 0; i < _outputBuffers.size(); ++i)
    {
        std::vector<VkDescriptorBufferInfo> descriptorBufferInfos = {
            VkDescriptorBufferInfo { _outputBuffers[i].buffer, 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo { _inputBuffers[i].buffer,  0, VK_WHOLE_SIZE }
        };

        // Create a write descriptor set for both the input and output buffer, because the shader needs access to them
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = _descriptor.set;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet.descriptorCount = 1;

        // Input is on binding 0, output in binding 1
        writeDescriptorSet.dstBinding = 1;
        writeDescriptorSet.pBufferInfo = &descriptorBufferInfos[0];

        vkUpdateDescriptorSets(_device.device, 1, &writeDescriptorSet, 0, nullptr);

        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pBufferInfo = &descriptorBufferInfos[1];

        vkUpdateDescriptorSets(_device.device, 1, &writeDescriptorSet, 0, nullptr);
    }

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

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();

    if (vkCreateCommandPool(_device.device, &poolInfo, nullptr, &_commandPool.commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void JenkinsGpuHash::createCommandBuffers(uint32_t frameCount)
{
    // TODO: Implement flight frames to keep feeding data to the GPU and not stall after each frame for a fence
    // (Said fence isnt even in here yet)
    _commandPool.commandBuffers.resize(frameCount);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = _commandPool.commandBuffers.size();

    if (vkAllocateCommandBuffers(_device.device, &allocInfo, _commandPool.commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers!");

    for (size_t i = 0; i < _commandPool.commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(_commandPool.commandBuffers[i], &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("failed to begin recording command buffer!");

        // Assume that by the time we reached this "frame", we got data to upload
        VkBufferCopy copyRegion = {};
        copyRegion.size = VK_WHOLE_SIZE;
        vkCmdCopyBuffer(_commandPool.commandBuffers[i], _stagingBuffers[i].buffer, _inputBuffers[i].buffer, 1, &copyRegion);

        vkCmdBindPipeline(_commandPool.commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline.pipeline);
        vkCmdBindDescriptorSets(_commandPool.commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline.layout, 0, 1, &_descriptor.set, 0, nullptr);
        vkCmdDispatch(_commandPool.commandBuffers[i],
            // TODO: Try to find a sweet spot instead of being a cunt and going straight to the limit
            _device.properties.limits.maxComputeWorkGroupCount[0],
            1,
            1);

        if (vkEndCommandBuffer(_commandPool.commandBuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to record command buffer!");
    }
}

void JenkinsGpuHash::submitWork()
{
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandPool.commandBuffers[0];

    // VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    if (vkQueueSubmit(_computeQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer!");
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

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}
