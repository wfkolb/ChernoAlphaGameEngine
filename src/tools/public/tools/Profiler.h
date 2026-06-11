#pragma once
// CPU profiling (PROFILE_SCOPE) lives in engine::core so networking and
// physics can use it without depending on engine::tools.
#include <core/Profiler.h>

// GPU scope macros: wrap PIXBeginEvent / PIXEndEvent in DevRel; no-op otherwise.
#ifdef ENGINE_DEVREL
#  include <pix3.h>
#  define PROFILE_GPU_SCOPE(cmdList, name)  PIXBeginEvent(cmdList, 0, name)
#  define PROFILE_GPU_END(cmdList)          PIXEndEvent(cmdList)
#else
#  define PROFILE_GPU_SCOPE(cmdList, name)  ((void)0)
#  define PROFILE_GPU_END(cmdList)          ((void)0)
#endif
