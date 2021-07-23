#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "debug.h"

using std::unique_ptr;
using std::optional;

#define SECTION(message) std::cout << '\n' << message << '\n'

class HelloTriangleApplication {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue presentQueue;

    VkSurfaceKHR surface;

    optional<uint32_t> graphicsQueueFamily;
    optional<uint32_t> presentQueueFamily;

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
            if (result != VK_SUCCESS) die(std::cout << "ah fuck " << result);
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

        SECTION("=== Pick a graphics device ===");
        {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
            unique_ptr<VkPhysicalDevice[]> devices(new VkPhysicalDevice[deviceCount]);
            vkEnumeratePhysicalDevices(instance, &deviceCount, devices.get());

            if (deviceCount == 0) die(std::cout << "You don't even have a GPU dude!");

            for (size_t i = 0; i < deviceCount; ++i) {
                std::cout << "device " << i+1 << '/' << deviceCount << '\n';

                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

                std::cout << "\tdevice type: "
                         << physicalDeviceTypeToString(deviceProperties.deviceType)
                         << '\n';
            }
            for (size_t i = 0; i < deviceCount; ++i) {
                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

                if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    std::cout << "picking " << i+1 << '/' << deviceCount << " 'cause it's descrete!\n";
                    physicalDevice = devices[i];
                    break;
                }
                if (i+1 == deviceCount) {
                    std::cout << "alright I guess " << i+1 << '/' << deviceCount << " is good enough.\n";
                    physicalDevice = devices[i];
                    break;
                }
                // TODO (1): the below queue families step should be involved here.
            }
        }

        SECTION("=== Create window surface ===");
        {
            auto createResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
            if (createResult != VK_SUCCESS) die(std::cout << "glfwCreateWindowSurface failed! " << createResult);

            std::cout << "done\n";
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

            if (!graphicsQueueFamily.has_value()) die(std::cout << "couldn't find graphics queue :(");
            if (!presentQueueFamily.has_value()) die(std::cout << "couldn't find present queue :(");
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

            auto createResult = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
            if (createResult != VK_SUCCESS) die(std::cout << "vkCreateDevice failed! " << createResult);

            std::cout << "logical devices created! now grabbing the present queue\n";
            vkGetDeviceQueue(device, presentQueueFamily.value(), 0, &presentQueue);

            std::cout << "done\n";
        }
    }

    void mainLoop() {

    }

    void cleanup() {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
};

int main() {
    std::cout << ":)\n";
    HelloTriangleApplication app;

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
    app.initVulkan(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        app.mainLoop();
    }

    app.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
