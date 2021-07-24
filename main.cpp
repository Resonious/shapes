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

#include "debug.h"

using std::unique_ptr;
using std::optional;
using std::clamp;

#define SECTION(message) std::cout << '\n' << message << '\n'

// This all uses https://vulkan-tutorial.com/en as a reference.

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
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue presentQueue;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

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
        SwapchainSupport support(physicalDevice, surface);

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = support.clampImageCount(support.capabilities.minImageCount + 1);

        auto surfaceFormat = support.bestSurfaceFormat();
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = support.swapExtent(window);
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
        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
    }

    void createImageViews() {
        SwapchainSupport support(physicalDevice, surface);
        swapchainImageViews.resize(swapchainImages.size());

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

            std::cout << "logical devices created! now grabbing the present queue\n";
            vkGetDeviceQueue(device, presentQueueFamily.value(), 0, &presentQueue);

            std::cout << "done\n";
        }

        SECTION("=== Swapchain and friends. This is stuff that may happen a lot ===");
        createSwapchain(window);
        createImageViews();
        std::cout << "done!\n";
    }

    void mainLoop() {

    }

    void cleanup() {
        for (auto imageView : swapchainImageViews) vkDestroyImageView(device, imageView, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
};

int main() {
    std::cout << ":)\n";
    RenderState renderer;

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
    renderer.initVulkan(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        renderer.mainLoop();
    }

    renderer.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
