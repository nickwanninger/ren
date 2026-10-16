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
#ifdef __linux__
  // Read a "<field> <n> kB" line from /proc/self/status and return the value in bytes.
  inline u64 readProcSelfStatusBytes(const char *field) {
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) { return 0; }

    const size_t fieldLength = std::char_traits<char>::length(field);
    std::string line;
    while (std::getline(status, line)) {
      if (line.compare(0, fieldLength, field) == 0) {
        // Extract the number (in kB)
        size_t start = line.find_first_of("0123456789");
        if (start == std::string::npos) { return 0; }

        size_t end = line.find_first_not_of("0123456789", start);
        if (end == std::string::npos) { end = line.length(); }

        try {
          u64 valueKB = std::stoull(line.substr(start, end - start));
          return valueKB * 1024;  // Convert kB to bytes
        } catch (...) { return 0; }
      }
    }
    return 0;
  }
#endif

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
    return readProcSelfStatusBytes("VmRSS:");

#else
    // Unsupported platform
    return 0;
#endif
  }

  // Resident anonymous memory (heap, stacks, private mappings) of the current process.
  // This excludes file-backed pages such as mapped executables and libraries.
  inline u64 getCurrentProcessAnonymousRSS() {
#if defined(__APPLE__)
    // `internal` is the resident anonymous memory. Compressed pages are not included.
    task_vm_info_data_t info;
    mach_msg_type_number_t infoCount = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &infoCount) == KERN_SUCCESS) {
      return static_cast<u64>(info.internal);
    }
    return 0;

#elif defined(__linux__)
    return readProcSelfStatusBytes("RssAnon:");

#else
    // Windows has no direct value for resident anonymous memory.
    return 0;
#endif
  }

}  // namespace ren