#define die(print) do { auto &log = std::cout; print << '\n'; exit(2); } while(0)

#ifdef DEBUG
#define debug(statement) do { auto &log = std::cout; statement; } while(0)
#else
#define debug(_)
#endif

const char *physicalDeviceTypeToString(int t);
