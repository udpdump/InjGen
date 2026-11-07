#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <cstdint>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
typedef uint32_t DWORD;
typedef uintptr_t DWORD_PTR;
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#ifndef __vectorcall
#define __vectorcall
#endif
#endif

constexpr size_t MIN_STRING_LENGTH = 12;

enum inst_set : int {
    INSTRUCTION_SET_NONE,
    INSTRUCTION_SET_SSE,
    INSTRUCTION_SET_AVX,
    INSTRUCTION_SET_AVX512
};

void __fastcall start_memory_scan(const DWORD pid);
