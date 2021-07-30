#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include <string>
#include <map>
#include <array>
#include <algorithm>
#include <functional>

#include "debug.h"

using std::unique_ptr;
using std::optional;
using std::clamp;
using std::function;

#define SECTION(message) std::cout << '\n' << message << '\n'

// This all uses https://vulkan-tutorial.com/en as a reference.

template<typename VkHandle>
struct HandleWrapper {
    VkHandle handle;
    function<void(VkHandle &)> destructor;

    HandleWrapper(
        VkHandle value,
        function<void(VkHandle &)> cleanup
    ) : handle(value), destructor(cleanup) { }

    HandleWrapper(HandleWrapper<VkHandle> &copy) : handle(copy.handle),
                                                   destructor(copy.destructor)
    { copy.handle = nullptr; }

    ~HandleWrapper() {
        if (handle == nullptr) return;
        destructor(handle);
    }

    operator VkHandle() { return handle; }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    SwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);

        // Get formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, formats.data());
        }

        // Get present modes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, presentModes.data());
        }
    }

    // The GPU might support many color formats, this picks the best one.
    VkSurfaceFormatKHR bestSurfaceFormat() {
        for (const auto& format : formats) {
            // This is "the best"
            if (
                format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            ) {
                return format;
            }
        }

        // Who cares at this point
        return formats[0];
    }

    // The "present mode" is like, how many framebuffers do we have and what's the algorithm for
    // showing them on the screen vs filling them.
    VkPresentModeKHR bestPresentMode() {
        // Hmmm... We could check for VK_PRESENT_MODE_MAILBOX_KHR. That would let us go wild
        // but I don't really see the point in rendering multiple times per display refresh...
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // The "swap extent" is the size of our framebuffers in pixels.
    VkExtent2D swapExtent(GLFWwindow *window) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        }
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D result = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        result.width = clamp(
            result.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        result.height = clamp(
            result.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );

        return result;
    }

    uint32_t clampImageCount(uint32_t wanted) {
        if (capabilities.maxImageCount == 0) return wanted;
        if (capabilities.maxImageCount < wanted) return capabilities.maxImageCount;
        return wanted;
    }
};

class RenderState {
    // Variables
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue presentQueue;
    VkQueue graphicsQueue;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkExtent2D swapchainExtent;
    VkSurfaceFormatKHR swapchainSurfaceFormat;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkCommandBuffer> commandBuffers;

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkCommandPool commandPool;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;

    optional<uint32_t> graphicsQueueFamily;
    optional<uint32_t> presentQueueFamily;

    std::array<const char*, 1> requiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    size_t howGoodIsThisDevice(VkPhysicalDevice device) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        // Make sure the device has the extensions we need
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        unique_ptr<VkExtensionProperties[]> extensions(new VkExtensionProperties[extensionCount]);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.get());

        std::set<std::string> remainingExtensions(requiredExtensions.begin(), requiredExtensions.end());
        for (size_t i = 0; i < extensionCount; ++i) {
            auto erased = remainingExtensions.erase(extensions[i].extensionName);
            if (erased > 0)
                std::cout << "\tsupports " << extensions[i].extensionName << "!\n";
        }
        if (remainingExtensions.size() > 0) {
            std::cout << "\tdoesn't support: ";
            for (auto &ext : remainingExtensions) std::cout << ext << ' ';
            std::cout << "- forget it.\n";
            return 0;
        }
        std::cout << "\thas all the extensions we need. not bad\n";

        // Make sure the swapchain is actually functional
        SwapchainSupport swapchainSupport(device, surface);
        if (swapchainSupport.formats.empty()) {
            std::cout << "\tswap chain has no formats. forget it!\n";
            return 0;
        }
        if (swapchainSupport.presentModes.empty()) {
            std::cout << "\tswap chain has no present modes. forget it!\n";
            return 0;
        }
        std::cout << "\tswap chain looks good.\n";

        // From here on, we will try to estimate how powerful the card is.
        // We'll start at 1 here 'cause 0 means unusable.
        size_t score = 1;

        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
            std::cout << "\tdiscrete card. awesome";
        }

        return score;
    }

    void createSwapchain(GLFWwindow *window) {
        Logger log("createSwapchain");
        SwapchainSupport support(physicalDevice, surface);

        swapchainExtent = support.swapExtent(window);
        swapchainSurfaceFormat = support.bestSurfaceFormat();

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = support.clampImageCount(support.capabilities.minImageCount + 1);
        log << "minImageCount: " << createInfo.minImageCount << '\n';

        createInfo.imageFormat = swapchainSurfaceFormat.format;
        createInfo.imageColorSpace = swapchainSurfaceFormat.colorSpace;
        createInfo.imageExtent = swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (graphicsQueueFamily != presentQueueFamily) {
            die(log << "2 separate queue families! not supported yet. see "
                    << "https://vulkan-tutorial.com/en/Drawing_a_triangle/Presentation/Swap_chain I guess.");
        }
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = support.bestPresentMode();
        createInfo.clipped = VK_TRUE;

        // Oh boy!!!
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        auto result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
        if (result != VK_SUCCESS) die(log << "omg, failed to create swapchain. " << result);

        // Swapchain is made! Last step: grab its images for later.
        log << "Swapchain created! Now fetching images:\n";
        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        log << "Fetched " << imageCount << " swapchain images\n";
    }

    void createImageViews() {
        Logger log("createImageViews");
        SwapchainSupport support(physicalDevice, surface);
        swapchainImageViews.resize(swapchainImages.size());

        log << "creating " << swapchainImages.size() << " imageViews\n";
        for (size_t i = 0 ; i < swapchainImages.size(); ++i) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = support.bestSurfaceFormat().format;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            auto result = vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]);
            if (result != VK_SUCCESS) {
                die(log << "Failed to create image view!!?? " << i << '/' << swapchainImages.size());
            }
        }
    }

    VkShaderModule createShaderModule(const unsigned char *spirvCode, unsigned int len) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = len;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(spirvCode);

        VkShaderModule shaderModule;
        auto result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
        if (result != VK_SUCCESS) die(log << "Failed to send shader to GPU.. haha don't know which one lmfao.");

        return shaderModule;
    }

    void createRenderPass() {
        Logger log("createRenderPass");

        log << "creating color attachment\n";
        // I guess this routes the output of the fragment shader?
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainSurfaceFormat.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        log << "setting up subpass dependency\n";
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstSubpass = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        log << "creating render pass\n";
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        auto result = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
        if (result != VK_SUCCESS) die(log << "Failed to create render pass!!" << result);
    }

    void createGraphicsPipeline() {
        Logger log("createGraphicsPipeline");
#include "triangle.vert.h"
#include "triangle.frag.h"

        log << "creating basic triangle vertex shader module\n";
        HandleWrapper<VkShaderModule> vertModule(
            createShaderModule(triangle_vert_spv, triangle_vert_spv_len),
            [this](VkShaderModule mod) { vkDestroyShaderModule(device, mod, nullptr); }
        );
        log << "creating basic triangle fragment shader module\n";
        HandleWrapper<VkShaderModule> fragModule(
            createShaderModule(triangle_frag_spv, triangle_frag_spv_len),
            [this](VkShaderModule mod) { vkDestroyShaderModule(device, mod, nullptr); }
        );

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertModule;
        vertShaderStageInfo.pName = "main";
        // TODO: for the future, there is a pSpecializationInfo field for passing constants.

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragModule;
        fragShaderStageInfo.pName = "main";

        log << "setting up vertex input\n";
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

        log << "setting up \"input assembly\"\n";
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        log << "setting up viewport and scissor\n";
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)swapchainExtent.width;
        viewport.height = (float)swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        log << "setting up rasterizer\n";
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        log << "setting up multisample (useless for this)\n";
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        log << "setting up color blending\n";
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE; // TODO: false! maybe we want alpha blending at some point though
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0; // Optional
        pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        auto result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) die(log << "Couldn't create pipeline wtf! " << result);

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        log << "building the actual pipeline\n";
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr; // Optional
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr; // Optional
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0; // <- index of the subpass that uses this pipeline
        // Optional - the following 2 attribuges are for copying from another pipeline
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
        if (result != VK_SUCCESS) die(log << "Failed to create graphics pipeline!! " << result);
    }

    void createFramebuffers() {
        Logger log("createFramebuffers");
        swapchainFramebuffers.resize(swapchainImageViews.size());

        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            log << "framebuffer " << i+1 << '/' << swapchainImageViews.size() << '\n';
            VkImageView attachments[] = { swapchainImageViews[i] };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapchainExtent.width;
            framebufferInfo.height = swapchainExtent.height;
            framebufferInfo.layers = 1;

            auto result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]);
            if (result != VK_SUCCESS) die(log << "Auuughghghghghgh " << result);
        }
    }

    void createCommandPool() {
        Logger log("createCommandPool");

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamily.value();
        poolInfo.flags = 0; // Optional

        auto result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
        if (result != VK_SUCCESS) die(log << "wheres my command pool? " << result);

        log << "created command pool\n";
    }

    void createCommandBuffers() {
        Logger log("createCommandbuffers");

        commandBuffers.resize(swapchainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        auto result = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
        if (result != VK_SUCCESS) die(log << "Failed to allocate command buffer :((((( " << result);
        log << "allocated buffers\n";

        for (size_t i = 0; i < commandBuffers.size(); ++i) {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            result = vkBeginCommandBuffer(commandBuffers[i], &beginInfo);
            if (result != VK_SUCCESS) {
                die(log << "Failed to start recording buffer " << i+1 << '/' << commandBuffers.size() << ' ' << result);
            }

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapchainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapchainExtent;

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            log << "recording render pass " << i+1 << '/' << commandBuffers.size() << '\n';
            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

                uint32_t vertexCount = 3, instanceCount = 1, firstVertex = 0, firstInstance = 0;
                vkCmdDraw(commandBuffers[i], vertexCount, instanceCount, firstVertex, firstInstance);

            vkCmdEndRenderPass(commandBuffers[i]);

            if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
                die(log << "Failed to start recording buffer " << i+1 << '/' << commandBuffers.size());
            }
        }
    }

    void createSemaphores() {
        Logger log("createSemaphores");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        auto result1 = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
        auto result2 = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore);
        if (result1 != VK_SUCCESS) die(log << "Failed to create imageAvailableSemaphore");
        log << "created imageAvailableSemaphore\n";
        if (result2 != VK_SUCCESS) die(log << "Failed to create renderFinishedSemaphore");
        log << "created renderFinishedSemaphore\n";
    }

public:
    void initVulkan(GLFWwindow *window) {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        SECTION("=== Create Vulkan \"Instance\" ===");
        {
            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "Hello Triangle";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "Unreal Engine 9000";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = VK_API_VERSION_1_0;

            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = glfwExtensionCount;
            createInfo.ppEnabledExtensionNames = glfwExtensions;
            createInfo.enabledLayerCount = 0;

            VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
            if (result != VK_SUCCESS) die(log << "ah fuck " << result);
            std::cout << "easy" << '\n';
        }

        SECTION("=== Dick around with extensions (completely unnecessary) ===");
        {
            uint32_t extensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
            unique_ptr<VkExtensionProperties[]> extensions(new VkExtensionProperties[extensionCount]);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.get());

            std::cout << "extensions requested by GLFW:\n";
            for (size_t i = 0; i < glfwExtensionCount; ++i) {
                std::cout << '\t' << glfwExtensions[i] << '\n';
            }
            std::cout << "available extensions:\n";
            for (size_t i = 0; i < extensionCount; ++i) {
                std::cout << '\t' << extensions[i].extensionName << '\n';
            }
        }

        SECTION("=== Create window surface ===");
        {
            auto createResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
            if (createResult != VK_SUCCESS) die(log << "glfwCreateWindowSurface failed! " << createResult);

            std::cout << "done\n";
        }

        SECTION("=== Pick a physical graphics device ===");
        {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
            unique_ptr<VkPhysicalDevice[]> devices(new VkPhysicalDevice[deviceCount]);
            vkEnumeratePhysicalDevices(instance, &deviceCount, devices.get());

            if (deviceCount == 0) die(log << "You don't even have a GPU dude!");

            std::map<size_t, size_t> ranking;
            for (size_t i = 0; i < deviceCount; ++i) {
                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

                std::cout << "device " << i+1 << '/' << deviceCount << ": "
                          << physicalDeviceTypeToString(deviceProperties.deviceType)
                          << '\n';

                size_t score = howGoodIsThisDevice(devices[i]);
                ranking.emplace(score, i);
                std::cout << "\tfinal score: " << score << '\n';
            }

            size_t winnerIndex = ranking.rbegin()->second;
            physicalDevice = devices[winnerIndex];
            std::cout << winnerIndex+1 << '/' << deviceCount << " wins.\n";
        }

        SECTION("=== Gather queue families ===");
        {
            // TODO (1): looks like we should actually use this as a way to determine if a physical device is usable.
            // Like, "must have graphics queue and sexy compute queue!!" or something.
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            unique_ptr<VkQueueFamilyProperties[]> queueFamilies(new VkQueueFamilyProperties[queueFamilyCount]);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.get());

            std::cout << "queue family count: " << queueFamilyCount << ".\n";
            for (uint32_t i = 0; i < queueFamilyCount; ++i) {
                // Graphics queue family
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    std::cout << "going with queue family #" << i << " for graphics.\n";
                    graphicsQueueFamily = i;
                }

                // Present queue family (probably, hopefully, the same as the graphics queue family)
                VkBool32 presentSupport;
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
                if (presentSupport) {
                    std::cout << "going with queue family #" << i << " for present.\n";
                    presentQueueFamily = i;
                }
            }

            if (!graphicsQueueFamily.has_value()) die(log << "couldn't find graphics queue :(");
            if (!presentQueueFamily.has_value()) die(log << "couldn't find present queue :(");
        }

        SECTION("=== Create logical device ===");
        {
            float queuePriority = 1.0f;

            // TODO: hah actually isn't this already done above? I'm supposed to change it right?
            // TODO: yes. confirmed. this code belongs in the logical device step. these queues are
            // created alongside the logical device.
            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
            std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily.value(), presentQueueFamily.value()};
            std::cout << "creating " << uniqueQueueFamilies.size() << " queues.\n";
            for (uint32_t queueFamily : uniqueQueueFamilies) {
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = queueFamily;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;
                queueCreateInfos.push_back(queueCreateInfo);
            }

            // TODO: We can populate this with feature we want later
            VkPhysicalDeviceFeatures deviceFeatures{};

            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pQueueCreateInfos = queueCreateInfos.data();
            createInfo.queueCreateInfoCount = queueCreateInfos.size();
            createInfo.pEnabledFeatures = &deviceFeatures;
            createInfo.enabledLayerCount = 0;
            createInfo.enabledExtensionCount = requiredExtensions.size();
            createInfo.ppEnabledExtensionNames = requiredExtensions.data();

            auto createResult = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
            if (createResult != VK_SUCCESS) die(log << "vkCreateDevice failed! " << createResult);

            std::cout << "logical devices created! now grabbing the queues\n";
            vkGetDeviceQueue(device, presentQueueFamily.value(), 0, &presentQueue);
            vkGetDeviceQueue(device, graphicsQueueFamily.value(), 0, &graphicsQueue);

            std::cout << "done\n";
        }

        SECTION("=== Swapchain and friends. This is stuff that may happen a lot ===");
        createSwapchain(window);
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createCommandBuffers();
        createSemaphores();
        std::cout << "done!\n";
    }

    void drawFrame() {
        vkQueueWaitIdle(presentQueue);

        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        VkSemaphore semaphoresToSignal[] = {renderFinishedSemaphore};
        VkSwapchainKHR swapchainsToPresent[] = {swapchain};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = semaphoresToSignal;

        auto result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) die(log << "Failed to submit draw command buffer! " << result);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = semaphoresToSignal;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchainsToPresent;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(presentQueue, &presentInfo);
    }

    void cleanupSwapchain() {
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        for (auto framebuffer : swapchainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto imageView : swapchainImageViews) vkDestroyImageView(device, imageView, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void cleanup() {
        vkQueueWaitIdle(presentQueue);

        cleanupSwapchain();
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
};


static void glfwError(int id, const char* description) {
  std::cout << "GLFW: (" << id << ") " << description << std::endl;
}

int main() {
    std::cout << ":)\n";
    RenderState renderer;

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) {
        const char *error;
        glfwGetError(&error);
        std::cout << ":( " << error << "\n";
        return 1;
    }

    // TODO: probably want to support resizing eventually. I think that was like,
    // we need to rebuild the swap chain or something.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto window = glfwCreateWindow(800, 600, "Shapes!??", nullptr, nullptr);
    if (!window) {
        const char *error;
        glfwGetError(&error);
        std::cout << ":( " << error << "\n";
        return 2;
    }
    renderer.initVulkan(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        renderer.drawFrame();
    }

    renderer.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
