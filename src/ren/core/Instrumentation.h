#pragma once

#include "instrumenting/ftr.h"

#include <vulkan/vulkan.h>

// #define REN_PROFILE

#ifdef REN_PROFILE
   // Resolve which function signature macro will be used. Note that this only
  // is resolved when the (pre)compiler starts, so the syntax highlighting
  // could mark the wrong one in your editor!
  #if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) || (defined(__ICC) && (__ICC >= 600)) || defined(__ghs__)
    #define REN_FUNC_SIG __PRETTY_FUNCTION__
  #elif defined(__DMC__) && (__DMC__ >= 0x810)
    #define REN_FUNC_SIG __PRETTY_FUNCTION__
  #elif (defined(__FUNCSIG__) || (_MSC_VER))
    #define REN_FUNC_SIG __FUNCSIG__
  #elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
    #define REN_FUNC_SIG __FUNCTION__
  #elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
    #define REN_FUNC_SIG __FUNC__
  #elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
    #define REN_FUNC_SIG __func__
  #elif defined(__cplusplus) && (__cplusplus >= 201103)
    #define REN_FUNC_SIG __func__
  #else
    #define REN_FUNC_SIG "REN_FUNC_SIG unknown!"
  #endif

  #define REN_PROFILE_BEGIN(name) ftr_init_file(name ".fxt.gz")
  #define REN_PROFILE_END_SESSION() ftr_close()
  #define REN_PROFILE_SCOPE(name) FTR_SCOPE(name)
  #define REN_PROFILE_FUNCTION() FTR_FUNCTION()
  #define REN_PROFILE_MARK(name) FTR_MARK(name)
  #define REN_PROFILE_COUNTER(name, value) FTR_COUNTER(name, value)
  #define REN_PROFILE_EXPR(name, expr) FTR_EXPR(name, expr)
#else
  #define REN_PROFILE_BEGIN(name)
  #define REN_PROFILE_END_SESSION()
  #define REN_PROFILE_SCOPE(name)
  #define REN_PROFILE_FUNCTION()
  #define REN_PROFILE_MARK(name)
  #define REN_PROFILE_COUNTER(name, value) ((void)(value))
  #define REN_PROFILE_EXPR(name, expr) (expr)
#endif
