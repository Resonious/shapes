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

size_t Logger::tabs = 0;

Logger::Logger(const char *label) : label(label) {
    *this << "== " << label << " ==\n";
    tabs += 1;
}

Logger::~Logger() {
    tabs -= 1;
    *this << '\n';
}
