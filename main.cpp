#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>
#include <string>

#define die(print) do { std::cout << print << "\n"; exit(2); } while(0)

using std::unique_ptr;

class HelloTriangleApplication {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;

public:
    void initVulkan() {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        //=== Create Vulkan "Instance" ===//
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
            if (result != VK_SUCCESS) die("ah fuck " << result);
        }

        //=== Dick around with extensions (completely unnecessary) ===//
        {
            uint32_t extensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
            unique_ptr<VkExtensionProperties[]> extensions(new VkExtensionProperties[extensionCount]);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.get());

            std::cout << "available extensions:\n";
            for (size_t i = 0; i < extensionCount; ++i) {
                std::cout << '\t' << extensions[i].extensionName << '\n';
            }
            std::cout << "extensions requested by GLFW:\n";
            for (size_t i = 0; i < glfwExtensionCount; ++i) {
                std::cout << '\t' << glfwExtensions[i] << '\n';
            }
        }

        //=== Pick a graphics device ===//
        {
            uint32_t deviceCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &deviceCount, nullptr);
            unique_ptr<VkPhysicalDevice[]> devices(new VkPhysicalDevice[deviceCount]);
            // TODO: what the hell is this? error?
            vkEnumerateInstanceExtensionProperties(nullptr, &deviceCount, devices.get());
        }
    }

    void mainLoop() {

    }

    void cleanup() {
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
    app.initVulkan();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    app.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
