#include <cstddef>
#include <iostream>

#define die(print) do { auto &log = std::cout; print << '\n'; exit(2); } while(0)

#ifdef DEBUG
#define debug(statement) do { auto &log = std::cout; statement; } while(0)
#else
#define debug(_)
#endif

const char *physicalDeviceTypeToString(int t);

class Logger {
    static size_t tabs;
    const char *label;

public:
    Logger(const char *label);
    ~Logger();
    
    template<typename T>
    std::ostream &operator<<(T val) {
        for (size_t i = 0; i < tabs; ++i) std::cout << "   ";
        return (std::cout << val);
    }
};
