#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <cstring>
#include <cassert>
#include <iomanip>
#include <chrono>
#include <vulkan/vulkan.h>

#define MAX_PEAK_RECORDS 65536

struct uint128_gpu {
    uint64_t low;
    uint64_t high;
};

struct PeakRecordGpu {
    uint128_gpu start;
    uint128_gpu val;
};

struct GlobalPeaksGpu {
    uint128_gpu max_val;
    uint32_t max_steps;
    uint32_t max_sigma;
};

struct PeaksCountGpu {
    uint32_t max_val_count;
    uint32_t steps_count;
    uint32_t sigma_count;
};

struct GlobalMetricsGpu {
    uint32_t total_checked;
    uint32_t total_steps;
    uint32_t skipped_mod6;
    uint32_t overflowed;
};

struct PushConstantsGpu {
    uint64_t start_low;
    uint64_t start_high;
    uint64_t end_low;
    uint64_t end_high;
    uint32_t total_odds;
};

// Error check helper
#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err != VK_SUCCESS) { \
            std::cerr << "Vulkan Error: " << err << " at " << __LINE__ << std::endl; \
            exit(1); \
        } \
    } while (0)

std::vector<char> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

std::string u128_to_string(uint128_gpu n) {
    if (n.low == 0 && n.high == 0) return "0";
    std::string s = "";
    // standard 128-bit conversion
    unsigned __int128 temp = n.high;
    temp = (temp << 64) | n.low;
    while (temp > 0) {
        s = std::to_string((int)(temp % 10)) + s;
        temp /= 10;
    }
    return s;
}

uint128_gpu parse_uint128_gpu(const std::string& str) {
    uint128_gpu res = {0, 0};
    unsigned __int128 temp = 0;
    bool hex = false;
    size_t start_idx = 0;
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        hex = true;
        start_idx = 2;
    }
    for (size_t i = start_idx; i < str.size(); ++i) {
        char c = str[i];
        int val = 0;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (hex && c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;
        temp = temp * (hex ? 16 : 10) + val;
    }
    res.low = static_cast<uint64_t>(temp);
    res.high = static_cast<uint64_t>(temp >> 64);
    return res;
}

uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}
#include <algorithm>

void filter_and_print_peaks(const std::string& name, std::vector<PeakRecordGpu>& peaks, uint32_t count) {
    if (count == 0) {
        std::cout << "\n" << name << " (0):" << std::endl;
        return;
    }
    peaks.resize(count);

    // Sort by start value ascending
    std::sort(peaks.begin(), peaks.end(), [](const PeakRecordGpu& a, const PeakRecordGpu& b) {
        unsigned __int128 start_a = a.start.high;
        start_a = (start_a << 64) | a.start.low;
        unsigned __int128 start_b = b.start.high;
        start_b = (start_b << 64) | b.start.low;
        return start_a < start_b;
    });

    // Filter false positives (out-of-order execution artifacts)
    std::vector<PeakRecordGpu> true_peaks;
    unsigned __int128 running_max = 0;
    for (const auto& peak : peaks) {
        unsigned __int128 val = peak.val.high;
        val = (val << 64) | peak.val.low;
        if (val > running_max) {
            running_max = val;
            true_peaks.push_back(peak);
        }
    }

    std::cout << "\n" << name << " (" << true_peaks.size() << "):" << std::endl;
    for (const auto& peak : true_peaks) {
        std::cout << "  n = " << u128_to_string(peak.start) << " -> value = " << u128_to_string(peak.val) << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== Hailstone Vulkan Compute Search ===" << std::endl;

    uint128_gpu start = {3, 0};
    uint128_gpu end = {100000, 0};

    if (argc > 1) {
        start = parse_uint128_gpu(argv[1]);
    }
    if (argc > 2) {
        end = parse_uint128_gpu(argv[2]);
    }

    std::cout << "Searching range: [" << u128_to_string(start) << ", " << u128_to_string(end) << "]" << std::endl;

    unsigned __int128 start_val = start.high;
    start_val = (start_val << 64) | start.low;
    unsigned __int128 end_val = end.high;
    end_val = (end_val << 64) | end.low;

    if (start_val > end_val) {
        std::cerr << "Error: start > end" << std::endl;
        return 1;
    }

    // Force start to be odd
    if ((start_val & 1) == 0) {
        start_val += 1;
        start.low = static_cast<uint64_t>(start_val);
        start.high = static_cast<uint64_t>(start_val >> 64);
    }

    unsigned __int128 total_range = end_val - start_val + 1;
    uint32_t total_odds = static_cast<uint32_t>((total_range + 1) / 2);

    std::cout << "Total odd starting values to check: " << total_odds << std::endl;

    // 1. Initialize Vulkan
    VkInstance instance;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hailstone Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));

    // 2. Select physical device & find compute queue
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "Error: No Vulkan devices found!" << std::endl;
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice physicalDevice = devices[0];
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "Using GPU: " << deviceProperties.deviceName << std::endl;

    uint32_t queueFamilyIndex = uint32_t(-1);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndex = i;
            break;
        }
    }

    if (queueFamilyIndex == uint32_t(-1)) {
        std::cerr << "Error: No compute queue family found!" << std::endl;
        return 1;
    }

    // 3. Create logical device
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.shaderInt64 = VK_TRUE;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDevice device;
    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    VkQueue computeQueue;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &computeQueue);

    // 4. Load SPIR-V Shader Module
    std::vector<char> shaderCode;
    try {
        shaderCode = read_file("gpu_vulkan/shader.spv");
    } catch (...) {
        try {
            shaderCode = read_file("shader.spv");
        } catch (...) {
            std::cerr << "Error: shader.spv not found!" << std::endl;
            return 1;
        }
    }

    VkShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = shaderCode.size();
    shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule));

    // 5. Create buffers
    // Binding indices match shader bindings:
    // Binding 0: GlobalPeaks (24 bytes)
    // Binding 1: LockBuffer (4 bytes)
    // Binding 2: PeaksCount (12 bytes)
    // Binding 3: MaxValuePeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 4: StepsPeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 5: SigmaPeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 6: GlobalMetrics (16 bytes)
    std::vector<size_t> bufferSizes = {
        sizeof(GlobalPeaksGpu),
        4,
        sizeof(PeaksCountGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        sizeof(GlobalMetricsGpu)
    };

    std::vector<VkBuffer> buffers(7);
    std::vector<VkDeviceMemory> bufferMemories(7);

    for (size_t i = 0; i < 7; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSizes[i];
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[i]));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = find_memory_type(
            physicalDevice, memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemories[i]));
        VK_CHECK(vkBindBufferMemory(device, buffers[i], bufferMemories[i], 0));
    }

    double mem_transfer_time_ms = 0.0;
    double total_kernel_time_ms = 0.0;

    // Master metrics on host
    GlobalMetricsGpu masterMetrics = {0, 0, 0, 0};
    GlobalPeaksGpu masterPeaks = {{0, 0}, 0, 0};

    std::vector<PeakRecordGpu> masterMaxValPeaks;
    std::vector<PeakRecordGpu> masterStepsPeaks;
    std::vector<PeakRecordGpu> masterSigmaPeaks;

    // 6. Create descriptor pool & sets
    std::vector<VkDescriptorSetLayoutBinding> bindings(7);
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorSetLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 7;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    VkDescriptorPool descriptorPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet));

    // Update descriptor sets with our buffers
    std::vector<VkDescriptorBufferInfo> bufferInfos(7);
    std::vector<VkWriteDescriptorSet> descriptorWrites(7);
    for (uint32_t i = 0; i < 7; ++i) {
        bufferInfos[i].buffer = buffers[i];
        bufferInfos[i].offset = 0;
        bufferInfos[i].range = bufferSizes[i];

        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device, 7, descriptorWrites.data(), 0, nullptr);

    // 7. Create Pipeline Layout with Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantsGpu);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // 8. Create Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    // 9. Command Pool & Command Buffer
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdBufferAllocInfo{};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.commandPool = commandPool;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &commandBuffer));

    const unsigned __int128 CHUNK_SIZE = 2000000;
    unsigned __int128 current_chunk_start = start_val;

    while (current_chunk_start <= end_val) {
        unsigned __int128 current_chunk_end = current_chunk_start + CHUNK_SIZE - 1;
        if (current_chunk_end > end_val) {
            current_chunk_end = end_val;
        }

        unsigned __int128 chunk_start_val = current_chunk_start;
        if ((chunk_start_val & 1) == 0) {
            chunk_start_val += 1;
        }

        unsigned __int128 chunk_end_val = current_chunk_end;
        if (chunk_end_val >= chunk_start_val) {
            if ((chunk_end_val & 1) == 0) {
                chunk_end_val -= 1;
            }
        }

        if (chunk_start_val <= chunk_end_val) {
            unsigned __int128 chunk_odds_128 = (chunk_end_val - chunk_start_val) / 2 + 1;
            uint32_t chunk_odds = static_cast<uint32_t>(chunk_odds_128);

            // Copy masterPeaks to bufferMemories[0] and reset locks/counts/metrics
            {
                auto start_write = std::chrono::high_resolution_clock::now();
                void* data;
                VK_CHECK(vkMapMemory(device, bufferMemories[0], 0, bufferSizes[0], 0, &data));
                std::memcpy(data, &masterPeaks, sizeof(GlobalPeaksGpu));
                vkUnmapMemory(device, bufferMemories[0]);

                VK_CHECK(vkMapMemory(device, bufferMemories[1], 0, bufferSizes[1], 0, &data));
                std::memset(data, 0, bufferSizes[1]);
                vkUnmapMemory(device, bufferMemories[1]);

                VK_CHECK(vkMapMemory(device, bufferMemories[2], 0, bufferSizes[2], 0, &data));
                std::memset(data, 0, bufferSizes[2]);
                vkUnmapMemory(device, bufferMemories[2]);

                VK_CHECK(vkMapMemory(device, bufferMemories[6], 0, bufferSizes[6], 0, &data));
                std::memset(data, 0, bufferSizes[6]);
                vkUnmapMemory(device, bufferMemories[6]);
                auto end_write = std::chrono::high_resolution_clock::now();
                mem_transfer_time_ms += std::chrono::duration<double, std::milli>(end_write - start_write).count();
            }

            // Record commands
            VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            PushConstantsGpu pcs{};
            pcs.start_low = static_cast<uint64_t>(chunk_start_val);
            pcs.start_high = static_cast<uint64_t>(chunk_start_val >> 64);
            pcs.end_low = static_cast<uint64_t>(chunk_end_val);
            pcs.end_high = static_cast<uint64_t>(chunk_end_val >> 64);
            pcs.total_odds = chunk_odds;

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);

            uint32_t groupCount = (chunk_odds + 255) / 256;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            VK_CHECK(vkEndCommandBuffer(commandBuffer));

            // Submit work
            auto t_start_chunk = std::chrono::high_resolution_clock::now();

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VkFence fence;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

            VK_CHECK(vkQueueSubmit(computeQueue, 1, &submitInfo, fence));
            VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

            auto t_end_chunk = std::chrono::high_resolution_clock::now();
            total_kernel_time_ms += std::chrono::duration<double, std::milli>(t_end_chunk - t_start_chunk).count();

            vkDestroyFence(device, fence, nullptr);

            // Read results back
            GlobalMetricsGpu chunkMetrics{};
            PeaksCountGpu chunkCounts{};

            auto start_read = std::chrono::high_resolution_clock::now();
            {
                void* data;
                VK_CHECK(vkMapMemory(device, bufferMemories[6], 0, bufferSizes[6], 0, &data));
                std::memcpy(&chunkMetrics, data, sizeof(GlobalMetricsGpu));
                vkUnmapMemory(device, bufferMemories[6]);

                VK_CHECK(vkMapMemory(device, bufferMemories[2], 0, bufferSizes[2], 0, &data));
                std::memcpy(&chunkCounts, data, sizeof(PeaksCountGpu));
                vkUnmapMemory(device, bufferMemories[2]);

                if (chunkCounts.max_val_count > 0) {
                    uint32_t to_copy = std::min(chunkCounts.max_val_count, (uint32_t)MAX_PEAK_RECORDS);
                    std::vector<PeakRecordGpu> chunkMaxVal(to_copy);
                    VK_CHECK(vkMapMemory(device, bufferMemories[3], 0, bufferSizes[3], 0, &data));
                    std::memcpy(chunkMaxVal.data(), data, to_copy * sizeof(PeakRecordGpu));
                    vkUnmapMemory(device, bufferMemories[3]);
                    masterMaxValPeaks.insert(masterMaxValPeaks.end(), chunkMaxVal.begin(), chunkMaxVal.end());
                }

                if (chunkCounts.steps_count > 0) {
                    uint32_t to_copy = std::min(chunkCounts.steps_count, (uint32_t)MAX_PEAK_RECORDS);
                    std::vector<PeakRecordGpu> chunkSteps(to_copy);
                    VK_CHECK(vkMapMemory(device, bufferMemories[4], 0, bufferSizes[4], 0, &data));
                    std::memcpy(chunkSteps.data(), data, to_copy * sizeof(PeakRecordGpu));
                    vkUnmapMemory(device, bufferMemories[4]);
                    masterStepsPeaks.insert(masterStepsPeaks.end(), chunkSteps.begin(), chunkSteps.end());
                }

                if (chunkCounts.sigma_count > 0) {
                    uint32_t to_copy = std::min(chunkCounts.sigma_count, (uint32_t)MAX_PEAK_RECORDS);
                    std::vector<PeakRecordGpu> chunkSigma(to_copy);
                    VK_CHECK(vkMapMemory(device, bufferMemories[5], 0, bufferSizes[5], 0, &data));
                    std::memcpy(chunkSigma.data(), data, to_copy * sizeof(PeakRecordGpu));
                    vkUnmapMemory(device, bufferMemories[5]);
                    masterSigmaPeaks.insert(masterSigmaPeaks.end(), chunkSigma.begin(), chunkSigma.end());
                }
            }
            auto end_read = std::chrono::high_resolution_clock::now();
            mem_transfer_time_ms += std::chrono::duration<double, std::milli>(end_read - start_read).count();

            // Accumulate metrics
            masterMetrics.total_checked += chunkMetrics.total_checked;
            masterMetrics.total_steps += chunkMetrics.total_steps;
            masterMetrics.skipped_mod6 += chunkMetrics.skipped_mod6;
            masterMetrics.overflowed += chunkMetrics.overflowed;

            // Update master peaks based on this chunk's results
            GlobalPeaksGpu chunkPeaks{};
            {
                void* data;
                VK_CHECK(vkMapMemory(device, bufferMemories[0], 0, bufferSizes[0], 0, &data));
                std::memcpy(&chunkPeaks, data, sizeof(GlobalPeaksGpu));
                vkUnmapMemory(device, bufferMemories[0]);
            }

            unsigned __int128 current_master_val = masterPeaks.max_val.high;
            current_master_val = (current_master_val << 64) | masterPeaks.max_val.low;
            unsigned __int128 chunk_max_val = chunkPeaks.max_val.high;
            chunk_max_val = (chunk_max_val << 64) | chunkPeaks.max_val.low;

            if (chunk_max_val > current_master_val) {
                masterPeaks.max_val = chunkPeaks.max_val;
            }
            if (chunkPeaks.max_steps > masterPeaks.max_steps) {
                masterPeaks.max_steps = chunkPeaks.max_steps;
            }
            if (chunkPeaks.max_sigma > masterPeaks.max_sigma) {
                masterPeaks.max_sigma = chunkPeaks.max_sigma;
            }
        }

        current_chunk_start += CHUNK_SIZE;
    }

    std::cout << "\n=== Vulkan Search Completed ===" << std::endl;
    std::cout << "Memory Transfer Time: " << std::fixed << std::setprecision(2) << mem_transfer_time_ms << " ms" << std::endl;
    std::cout << "Kernel Execution Time: " << std::fixed << std::setprecision(2) << total_kernel_time_ms << " ms" << std::endl;
    std::cout << "Global Max Val Peak: " << u128_to_string(masterPeaks.max_val) << std::endl;
    std::cout << "Global Max Steps Peak: " << masterPeaks.max_steps << std::endl;
    std::cout << "Global Max Sigma Peak: " << masterPeaks.max_sigma << std::endl;
    std::cout << "Numbers Checked: " << masterMetrics.total_checked << std::endl;
    std::cout << "Steps Computed: " << masterMetrics.total_steps << std::endl;
    std::cout << "Average Steps: " << (masterMetrics.total_checked > 0 ? (double)masterMetrics.total_steps / masterMetrics.total_checked : 0.0) << std::endl;
    std::cout << "Skipped (Even): " << total_odds << " (implicitly skipped)" << std::endl;
    std::cout << "Skipped (Mod 6): " << masterMetrics.skipped_mod6 << std::endl;
    std::cout << "Overflowed (> 2^128): " << masterMetrics.overflowed << std::endl;

    double m_ips = (masterMetrics.total_checked / 1000000.0) / (total_kernel_time_ms / 1000.0);
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << m_ips << " M numbers/s" << std::endl;

    std::cout << "\n=== Peaks Found (Vulkan) ===" << std::endl;
    filter_and_print_peaks("Max Value Peaks", masterMaxValPeaks, masterMaxValPeaks.size());
    filter_and_print_peaks("Steps Peaks", masterStepsPeaks, masterStepsPeaks.size());
    filter_and_print_peaks("Stopping Time (sigma) Peaks", masterSigmaPeaks, masterSigmaPeaks.size());

    // 13. Clean up Vulkan
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    for (size_t i = 0; i < 7; ++i) {
        vkDestroyBuffer(device, buffers[i], nullptr);
        vkFreeMemory(device, bufferMemories[i], nullptr);
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    return 0;
}
