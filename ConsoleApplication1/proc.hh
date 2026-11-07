#pragma once

#include <unordered_set>
#include <string>
#include <utility>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
typedef uint32_t DWORD;
typedef uintptr_t DWORD_PTR;
#ifndef __fastcall
#define __fastcall
#endif
#endif

void __fastcall detect_all_instances(std::unordered_set<DWORD>& processes);
std::pair<DWORD_PTR, DWORD_PTR> __fastcall __get_mem_range(const DWORD processID);
