#include "debug.h"

#include <iostream>

const char *physicalDeviceTypeToString(int t) {
    // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#VkPhysicalDeviceType
    switch (t) {
    case 0: return "other";
    case 1: return "integrated";
    case 2: return "discrete";
    case 3: return "virtual";
    case 4: return "CPU";
    }
    die(std::cout << "Huh? Unknown device type " << t);
}
