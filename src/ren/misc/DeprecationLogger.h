#pragma once

#include <ren/types.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <span>


namespace ren {
  void printDeprecationWarning(const char* function, const char* file, int line,
                               std::span<void*> backtrace);
}  // namespace ren

#define REN_DEPRECATION_WARNING()                                                             \
  do {                                                                                        \
    static bool _ren_deprecation_logged = false;                                              \
    if (!_ren_deprecation_logged) {                                                           \
      void* buffer[128];                                                                      \
      size_t nptrs = backtrace(buffer, 128);                                                  \
      ren::printDeprecationWarning(__PRETTY_FUNCTION__, __FILE__, __LINE__, {buffer, nptrs}); \
      _ren_deprecation_logged = true;                                                         \
    }                                                                                         \
  } while (0);