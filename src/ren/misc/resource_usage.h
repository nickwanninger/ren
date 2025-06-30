#pragma once

#include <ren/types.h>

#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
  #pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
  #include <mach/mach.h>
  #include <mach/task.h>
#elif defined(__linux__)
  #include <unistd.h>
  #include <fstream>
  #include <string>
#endif

namespace ren {
  inline u64 getCurrentProcessRSS() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
      return static_cast<u64>(pmc.WorkingSetSize);
    }
    return 0;

#elif defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &infoCount) == KERN_SUCCESS) {
      return static_cast<u64>(info.resident_size);
    }
    return 0;

#elif defined(__linux__)
    // Read from /proc/self/status for more reliable parsing
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) { return 0; }

    std::string line;
    while (std::getline(status, line)) {
      if (line.compare(0, 6, "VmRSS:") == 0) {
        // Extract the number (in kB)
        size_t start = line.find_first_of("0123456789");
        if (start == std::string::npos) { return 0; }

        size_t end = line.find_first_not_of("0123456789", start);
        if (end == std::string::npos) { end = line.length(); }

        try {
          u64 rssKB = std::stoull(line.substr(start, end - start));
          return rssKB * 1024;  // Convert kB to bytes
        } catch (...) { return 0; }
      }
    }
    return 0;

#else
    // Unsupported platform
    return 0;
#endif
  }

}  // namespace ren