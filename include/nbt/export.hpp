#pragma once

#if defined(_WIN32) && defined(NBT_CPP_SHARED)
#if defined(NBT_CPP_EXPORTS)
#define NBT_CPP_API __declspec(dllexport)
#else
#define NBT_CPP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(NBT_CPP_SHARED)
#define NBT_CPP_API __attribute__((visibility("default")))
#else
#define NBT_CPP_API
#endif
