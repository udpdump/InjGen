#include "mem.hpp"
#include "proc.hh"

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#include <intrin.h>
#include "krnljmper.h"
#else
#include <cpuid.h>
#include <immintrin.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <dirent.h>
#include <cerrno>
#endif

using __simd__mem_scan_ptr = std::vector<std::string>(__vectorcall*)(unsigned char*, size_t);
using __mem_scan_ptr = std::vector<std::string>(__fastcall*)(unsigned char*, size_t);

__forceinline inst_set __intr() {
    int info[4];
    __cpuidex(info, 0, 0);
    int nIds = info[0];

    __cpuidex(info, 0x80000000, 0);

    inst_set instructionSet = INSTRUCTION_SET_NONE;

    if (nIds >= 0x00000001) {
        __cpuidex(info, 0x00000001, 0);
        if (info[3] & (1 << 25)) {
            instructionSet = static_cast<inst_set>(INSTRUCTION_SET_SSE);
        }
        if (info[2] & (1 << 28)) {
            instructionSet = static_cast<inst_set>(INSTRUCTION_SET_AVX);
        }
    }

    if (nIds >= 0x00000007) {
        __cpuidex(info, 0x00000007, 0);
        if (info[1] & (1 << 5)) {
            instructionSet = static_cast<inst_set>(INSTRUCTION_SET_AVX);
        }
        else if (instructionSet == INSTRUCTION_SET_AVX) {
            instructionSet = static_cast<inst_set>(INSTRUCTION_SET_SSE);
        }
        if (info[1] & (1 << 16)) {
            instructionSet = static_cast<inst_set>(INSTRUCTION_SET_AVX512);
        }
    }

    return instructionSet;
}

inline static int countTrailingZeros(uint32_t mask) {
#if defined(_MSC_VER)
    unsigned long index;
    if (_BitScanForward(&index, mask)) {
        return static_cast<int>(index);
    }
#else
    if (mask != 0) {
        return __builtin_ctz(mask);
    }
#endif
    return 32;
}

inline static size_t safe_tzcnt_u64(uint64_t mask) {
#if defined(_MSC_VER)
    return _tzcnt_u64(mask);
#else
    return mask != 0 ? static_cast<size_t>(__builtin_ctzll(mask)) : 64;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx512f,avx512bw,bmi,bmi2")))
#endif
inline static std::vector<std::string> __vectorcall avx512_mem_scn(unsigned char* buffer, size_t bytesRead) {
    constexpr size_t s512 = 64;
    const size_t numBlocks512 = bytesRead / s512;
    std::vector<std::string> capturedStrings;
    std::string partialString;

    for (size_t i = 0; i < numBlocks512; ++i) {
        __m512i data512 = _mm512_loadu_si512(reinterpret_cast<__m512i*>(buffer + i * s512));

        __mmask64 isPrintable512 = _mm512_cmpgt_epi8_mask(data512, _mm512_set1_epi8(31)) &
            _mm512_cmplt_epi8_mask(data512, _mm512_set1_epi8(127));

        uint64_t mask = isPrintable512;
        size_t start = 0;

        while (mask != 0) {
            size_t tz = safe_tzcnt_u64(mask);
            start += tz;
            mask >>= tz;

            size_t len = 0;
            while (mask & 1) {
                ++len;
                mask >>= 1;
            }
            start += len;

            if (len + partialString.size() >= MIN_STRING_LENGTH) {
                if (!partialString.empty()) {
                    partialString.append(reinterpret_cast<char*>(buffer + i * s512 + start - len), len);
                    capturedStrings.emplace_back(std::move(partialString));
                    partialString.clear();
                }
                else {
                    capturedStrings.emplace_back(reinterpret_cast<char*>(buffer + i * s512 + start - len), len);
                }
            }
            else {
                partialString.append(reinterpret_cast<char*>(buffer + i * s512 + start - len), len);
            }
        }
    }

    if (partialString.size() >= MIN_STRING_LENGTH) {
        capturedStrings.emplace_back(std::move(partialString));
    }

    return capturedStrings;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline static std::vector<std::string> __vectorcall avx_mem_scn(unsigned char* buffer, size_t bytesRead) {
    constexpr size_t s256 = 32;
    const size_t numBlocks256 = bytesRead / s256;
    std::vector<std::string> capturedStrings;
    std::string partialString;

    for (size_t i = 0; i < numBlocks256; ++i) {
        __m256i data256 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(buffer + i * s256));

        __m256i isPrintable256 = _mm256_and_si256(
            _mm256_cmpgt_epi8(data256, _mm256_set1_epi8(31)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8(127), data256)
        );

        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(isPrintable256));

        size_t start = 0;
        while (mask != 0) {
            int tz = countTrailingZeros(mask);
            start += tz;
            mask >>= tz;

            size_t len = 0;
            while (mask & 1) {
                ++len;
                mask >>= 1;
            }
            start += len;

            if (len + partialString.size() >= MIN_STRING_LENGTH) {
                if (!partialString.empty()) {
                    partialString.append(reinterpret_cast<char*>(buffer + i * s256 + start - len), len);
                    capturedStrings.emplace_back(std::move(partialString));
                    partialString.clear();
                }
                else {
                    capturedStrings.emplace_back(reinterpret_cast<char*>(buffer + i * s256 + start - len), len);
                }
            }
            else {
                partialString.append(reinterpret_cast<char*>(buffer + i * s256 + start - len), len);
            }
        }
    }

    if (partialString.size() >= MIN_STRING_LENGTH) {
        capturedStrings.emplace_back(std::move(partialString));
    }

    return capturedStrings;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.1,sse2")))
#endif
inline static std::vector<std::string> __vectorcall sse_mem_scn(unsigned char* buffer, size_t bytesRead) {
    constexpr size_t s128 = 16;
    const size_t numBlocks128 = bytesRead / s128;
    std::vector<std::string> capturedStrings;
    std::string partialString;

    for (size_t i = 0; i < numBlocks128; ++i) {
        __m128i data128 = _mm_loadu_si128(reinterpret_cast<__m128i*>(buffer + i * s128));

        __m128i isPrintable128 = _mm_and_si128(
            _mm_cmpgt_epi8(data128, _mm_set1_epi8(31)),
            _mm_cmplt_epi8(data128, _mm_set1_epi8(127))
        );

        uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(isPrintable128));

        size_t start = 0;
        while (mask != 0) {
            int tz = countTrailingZeros(mask);
            start += tz;
            mask >>= tz;

            size_t len = 0;
            while (mask & 1) {
                ++len;
                mask >>= 1;
            }
            start += len;

            if (len + partialString.size() >= MIN_STRING_LENGTH) {
                if (!partialString.empty()) {
                    partialString.append(reinterpret_cast<char*>(buffer + i * s128 + start - len), len);
                    capturedStrings.emplace_back(std::move(partialString));
                    partialString.clear();
                }
                else {
                    capturedStrings.emplace_back(reinterpret_cast<char*>(buffer + i * s128 + start - len), len);
                }
            }
            else {
                partialString.append(reinterpret_cast<char*>(buffer + i * s128 + start - len), len);
            }
        }
    }

    if (partialString.size() >= MIN_STRING_LENGTH) {
        capturedStrings.emplace_back(std::move(partialString));
    }

    return capturedStrings;
}

inline static std::vector<std::string> __fastcall generic_memory_scan(unsigned char* buffer, size_t bytesRead) {
    std::vector<std::string> __mm_dump;
    std::string currentString;
    bool insidePrintableSeq = false;

    for (size_t i = 0; i < bytesRead; ++i) {
        if (buffer[i] >= 32 && buffer[i] <= 126) /* ASCII range */ {
            if (!insidePrintableSeq) {
                insidePrintableSeq = true;
            }
            currentString += buffer[i];
        }
        else {
            if (insidePrintableSeq && currentString.size() >= MIN_STRING_LENGTH) {
                __mm_dump.push_back(currentString);
            }
            insidePrintableSeq = false;
            currentString.clear();
        }
    }

    if (insidePrintableSeq && currentString.size() >= MIN_STRING_LENGTH) {
        __mm_dump.push_back(currentString);
    }

    return __mm_dump;
}

inline static std::vector<std::string> __fastcall __trigger_pattern(
    unsigned char* buffer, size_t bytesRead,
    std::vector<std::string>(*memory_scan)(unsigned char*, size_t)) {
    return memory_scan(buffer, bytesRead);
}

#ifdef _WIN32

static __forceinline bool __fastcall __vld_pattern(const HANDLE hProcess) {
    inst_set cpu_instruction = __intr();
    __mem_scan_ptr memory_scan = nullptr;
    __simd__mem_scan_ptr simd_scan = nullptr;

    switch (cpu_instruction) {
    case INSTRUCTION_SET_AVX512:
        simd_scan = avx512_mem_scn;
        break;
    case INSTRUCTION_SET_AVX:
        simd_scan = avx_mem_scn;
        break;
    case INSTRUCTION_SET_SSE:
        simd_scan = sse_mem_scn;
        break;
    default:
        memory_scan = generic_memory_scan;
        break;
    }

    const HANDLE hCurrentProcess = GetCurrentProcess();
    MEMORY_BASIC_INFORMATION mbi = { 0 };
    unsigned char* address = nullptr;

    while (KeNtQueryVirtualMemory(hProcess, address, MemoryBasicInformation, &mbi, sizeof(mbi), nullptr) == 0) {
        if ((mbi.State == MEM_COMMIT) &&
            (mbi.Protect & PAGE_READWRITE) &&
            (!(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))) {

            unsigned char* baseAddress = nullptr;
            SIZE_T bufferSize = mbi.RegionSize / 2;

            const NTSTATUS allocStatus = KeNtAllocateVirtualMemory(
                hCurrentProcess,
                reinterpret_cast<PVOID*>(&baseAddress),
                0,
                &bufferSize,
                MEM_COMMIT,
                PAGE_READWRITE
            );

            if (allocStatus != ((NTSTATUS)0x00000000L)) {
                continue;
            }

            unsigned char* pageStart = reinterpret_cast<unsigned char*>(mbi.BaseAddress);
            SIZE_T bytesRead = 0;
            const NTSTATUS rpm = KeNtReadVirtualMemory(hProcess, pageStart, baseAddress, bufferSize, &bytesRead);

            if (rpm == 0) {
                std::vector<std::string> memory_dump;

                if (simd_scan) 
                    memory_dump = simd_scan(baseAddress, bytesRead);   
                else 
                    memory_dump = memory_scan(baseAddress, bytesRead);
                
                for (const auto& build : memory_dump) {
                    if (build.find("com/lunarclient") != std::string::npos) {
                        KeNtFreeVirtualMemory(hCurrentProcess, reinterpret_cast<PVOID*>(&baseAddress), &bufferSize, MEM_RELEASE);
                        return true;
                    }
                }
            }

            KeNtFreeVirtualMemory(hCurrentProcess, reinterpret_cast<PVOID*>(&baseAddress), &bufferSize, MEM_RELEASE);
        }

        address = reinterpret_cast<unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return false;
}

static __forceinline bool __fastcall trace(const HANDLE hProcess, const DWORD_PTR baseAddress, const DWORD_PTR endAddress, const DWORD x /* expected */) {
    MEMORY_BASIC_INFORMATION mbi = { 0 };
    unsigned char* currentAddress = reinterpret_cast<unsigned char*>(baseAddress);
    const HANDLE hCurrentProcess = GetCurrentProcess();

    while (currentAddress < reinterpret_cast<unsigned char*>(endAddress)) {
        if (KeNtQueryVirtualMemory(hProcess, currentAddress, MemoryBasicInformation, &mbi, sizeof(mbi), nullptr) != 0) {
            break;
        }

        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE)) {
            SIZE_T bytesRead = 0;
            size_t bufferSize = mbi.RegionSize;

            PVOID buffer = nullptr;
            const NTSTATUS __mm_alloc = KeNtAllocateVirtualMemory(hCurrentProcess, &buffer, 0, &bufferSize, MEM_COMMIT, PAGE_READWRITE);
            if (__mm_alloc != 0)
                return false;       

            const NTSTATUS _mm_read = KeNtReadVirtualMemory(hProcess, currentAddress, buffer, bufferSize, &bytesRead);
            if (_mm_read >= 0 && bytesRead == bufferSize) {
                for (size_t i = 0; i < bytesRead; i += sizeof(DWORD)) {
                    if (i + sizeof(DWORD) <= bytesRead) {
                        const DWORD value = *reinterpret_cast<DWORD*>(static_cast<unsigned char*>(buffer) + i);
                        if (value == x) {
                            KeNtFreeVirtualMemory(hProcess, &buffer, &bufferSize, MEM_RELEASE);
                            return true;
                        }
                    }
                }
            }

            KeNtFreeVirtualMemory(hProcess, &buffer, &bufferSize, MEM_RELEASE);
        }

        currentAddress += mbi.RegionSize;
    }

    return false;
}

static __forceinline void __fastcall __trigger_err() {
    const DWORD mseconds = 10000;
    fprintf((__acrt_iob_func(2)), "Minecraft Java not detected. Closing program in 10s...\n");
    LARGE_INTEGER delay = { 0 };

    const __int64 dwmseconds64 = (__int64)mseconds;
    const __int64 conversionFactor = 10000;
    const __int64 result = -(dwmseconds64 * conversionFactor);

    delay.QuadPart = result;
    KeNtDelayExecution(0, &delay);
    __fastfail(ERROR_SUCCESS);
}

void __fastcall start_memory_scan(const DWORD pid) {
    auto [baseAddress, endAddress] = __get_mem_range(pid);
    if (baseAddress == 0 || endAddress == 0) __trigger_err();

    HANDLE hProcess;
    OBJECT_ATTRIBUTES objAttr = { 0 };
    CLIENT_ID clientId = { 0 };
    clientId.UniqueProcess = UlongToHandle(pid);

    InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);
    const NTSTATUS status = KeNtOpenProcess(&hProcess, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, &objAttr, &clientId);
    if (!((NTSTATUS)(status) >= 0)) __trigger_err();

    bool flag = true;
    flag &= trace(hProcess, baseAddress, endAddress, 524294); // +B8F450 - 06- push es
    flag &= trace(hProcess, baseAddress, endAddress, 4242546329); /* +B8F400 - 99 - cdq +B8F401 - 1E - push ds +B8F402 - E0 FC - loopne +B8F400 */

    if (flag) {
        std::cout << "Validating detection...\n";
        if (__vld_pattern(hProcess)) {
            std::cout << "[!] Generic injection detected.\n";
        }
        else {
            std::cout << "[-] Injection detected in untested game client.\n";
        }
    }
    else {
        std::cout << "[+] No suspicious java agents were loaded in the game instance.\n";
    }

    KeNtClose(hProcess);
    system("pause");
}

#else // Linux implementation

static bool read_process_memory(const DWORD pid, const uintptr_t address, void* buffer, const size_t size, size_t* bytesRead) {
    if (size == 0) return false;

    struct iovec local_iov;
    struct iovec remote_iov;
    local_iov.iov_base = buffer;
    local_iov.iov_len = size;
    remote_iov.iov_base = reinterpret_cast<void*>(address);
    remote_iov.iov_len = size;

    ssize_t n = process_vm_readv(static_cast<pid_t>(pid), &local_iov, 1, &remote_iov, 1, 0);
    if (n > 0) {
        if (bytesRead) *bytesRead = static_cast<size_t>(n);
        return true;
    }

    // Fallback: /proc/<pid>/mem
    std::string memPath = "/proc/" + std::to_string(pid) + "/mem";
    int fd = open(memPath.c_str(), O_RDONLY);
    if (fd >= 0) {
        ssize_t pn = pread(fd, buffer, size, static_cast<off_t>(address));
        close(fd);
        if (pn > 0) {
            if (bytesRead) *bytesRead = static_cast<size_t>(pn);
            return true;
        }
    }

    return false;
}

static bool is_attach_listener_present(const DWORD pid) {
    std::string taskPath = "/proc/" + std::to_string(pid) + "/task";
    DIR* dir = opendir(taskPath.c_str());
    if (dir) {
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
            std::string commPath = taskPath + "/" + entry->d_name + "/comm";
            std::ifstream f(commPath);
            std::string comm;
            if (std::getline(f, comm)) {
                if (comm == "Attach Listener") {
                    closedir(dir);
                    return true;
                }
            }
        }
        closedir(dir);
    }

    // Also check for dynamic attach domain socket
    std::string sockPath = "/tmp/.java_pid" + std::to_string(pid);
    if (access(sockPath.c_str(), F_OK) == 0) {
        return true;
    }

    return false;
}

static bool check_agent_modules(const DWORD pid) {
    std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream mapsFile(mapsPath);
    if (!mapsFile.is_open()) return false;

    std::string line;
    while (std::getline(mapsFile, line)) {
        if (line.find("libinstrument.so") != std::string::npos ||
            line.find("instrument.dll") != std::string::npos) {
            return true;
        }
    }
    return false;
}

static __forceinline bool __fastcall __vld_pattern(const DWORD pid) {
    inst_set cpu_instruction = __intr();
    __mem_scan_ptr memory_scan = nullptr;
    __simd__mem_scan_ptr simd_scan = nullptr;

    switch (cpu_instruction) {
    case INSTRUCTION_SET_AVX512:
        simd_scan = avx512_mem_scn;
        break;
    case INSTRUCTION_SET_AVX:
        simd_scan = avx_mem_scn;
        break;
    case INSTRUCTION_SET_SSE:
        simd_scan = sse_mem_scn;
        break;
    default:
        memory_scan = generic_memory_scan;
        break;
    }

    std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream mapsFile(mapsPath);
    if (mapsFile.is_open()) {
        std::string line;
        std::vector<unsigned char> buffer;

        while (std::getline(mapsFile, line)) {
            if (line.empty()) continue;

            // Check if mapped jar files indicate tested game clients
            std::string lineLower = line;
            std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(), ::tolower);
            if (lineLower.find("com/lunarclient") != std::string::npos ||
                lineLower.find("lunar") != std::string::npos ||
                lineLower.find("feather") != std::string::npos ||
                lineLower.find("fabric") != std::string::npos ||
                lineLower.find("forge") != std::string::npos ||
                lineLower.find("minecraft") != std::string::npos) {
                return true;
            }

            size_t dash = line.find('-');
            size_t space1 = line.find(' ');
            if (dash == std::string::npos || space1 == std::string::npos || dash >= space1) continue;

            // Permissions: readable and writable (rw)
            if (space1 + 2 >= line.size() || line[space1 + 1] != 'r' || line[space1 + 2] != 'w') {
                continue;
            }

            // Skip non-heap kernel mappings
            if (line.find('[') != std::string::npos) {
                continue;
            }

            try {
                uintptr_t start = std::stoull(line.substr(0, dash), nullptr, 16);
                uintptr_t end = std::stoull(line.substr(dash + 1, space1 - (dash + 1)), nullptr, 16);
                size_t regionSize = end - start;

                size_t bufferSize = std::min(regionSize / 2, static_cast<size_t>(2 * 1024 * 1024));
                if (bufferSize < MIN_STRING_LENGTH) {
                    bufferSize = std::min(regionSize, static_cast<size_t>(2 * 1024 * 1024));
                }
                if (bufferSize < MIN_STRING_LENGTH) continue;

                if (buffer.size() < bufferSize) {
                    buffer.resize(bufferSize);
                }

                size_t bytesRead = 0;
                if (read_process_memory(pid, start, buffer.data(), bufferSize, &bytesRead) && bytesRead >= MIN_STRING_LENGTH) {
                    std::vector<std::string> memory_dump;
                    if (simd_scan)
                        memory_dump = simd_scan(buffer.data(), bytesRead);
                    else
                        memory_dump = memory_scan(buffer.data(), bytesRead);

                    for (const auto& build : memory_dump) {
                        if (build.find("com/lunarclient") != std::string::npos ||
                            build.find("net/minecraft") != std::string::npos ||
                            build.find("net/fabricmc") != std::string::npos ||
                            build.find("feather") != std::string::npos) {
                            return true;
                        }
                    }
                }
            } catch (...) {
                continue;
            }
        }
    }

    // Also check cmdline
    std::string cmdPath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream cmdFile(cmdPath, std::ios::binary);
    if (cmdFile.is_open()) {
        std::string cmd;
        while (std::getline(cmdFile, cmd, '\0')) {
            std::string cmdLower = cmd;
            std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
            if (cmdLower.find("lunar") != std::string::npos ||
                cmdLower.find("feather") != std::string::npos ||
                cmdLower.find("fabric") != std::string::npos ||
                cmdLower.find("minecraft") != std::string::npos ||
                cmdLower.find("forge") != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

static __forceinline bool __fastcall trace(const DWORD pid, const DWORD_PTR baseAddress, const DWORD_PTR endAddress, const DWORD x /* expected */) {
    std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream mapsFile(mapsPath);
    if (!mapsFile.is_open()) return false;

    std::string line;
    std::vector<uint8_t> buffer;

    while (std::getline(mapsFile, line)) {
        if (line.empty()) continue;

        size_t dash = line.find('-');
        size_t space1 = line.find(' ');
        if (dash == std::string::npos || space1 == std::string::npos || dash >= space1) continue;

        try {
            uintptr_t start = std::stoull(line.substr(0, dash), nullptr, 16);
            uintptr_t end = std::stoull(line.substr(dash + 1, space1 - (dash + 1)), nullptr, 16);

            // Check if region intersects [baseAddress, endAddress)
            if (end <= baseAddress || start >= endAddress) continue;

            // Check permissions: read and write
            if (space1 + 2 < line.size()) {
                if (line[space1 + 1] != 'r' || line[space1 + 2] != 'w') {
                    continue;
                }
            } else {
                continue;
            }

            uintptr_t scanStart = std::max(start, static_cast<uintptr_t>(baseAddress));
            uintptr_t scanEnd = std::min(end, static_cast<uintptr_t>(endAddress));
            size_t scanSize = scanEnd - scanStart;

            if (buffer.size() < scanSize) {
                buffer.resize(scanSize);
            }

            size_t bytesRead = 0;
            if (read_process_memory(pid, scanStart, buffer.data(), scanSize, &bytesRead)) {
                for (size_t i = 0; i + sizeof(DWORD) <= bytesRead; i += sizeof(DWORD)) {
                    const DWORD value = *reinterpret_cast<const DWORD*>(buffer.data() + i);
                    if (value == x) {
                        return true;
                    }
                }
            }
        } catch (...) {
            continue;
        }
    }

    return false;
}

static __forceinline void __fastcall __trigger_err() {
    fprintf(stderr, "Minecraft Java not detected. Closing program in 10s...\n");
    sleep(10);
    exit(0);
}

void __fastcall start_memory_scan(const DWORD pid) {
    auto [baseAddress, endAddress] = __get_mem_range(pid);
    if (baseAddress == 0 || endAddress == 0) __trigger_err();

    bool flag = false;

    // 1. Dynamic JVMTI / Java agent detection (Attach Listener thread & domain socket)
    if (is_attach_listener_present(pid)) {
        flag = true;
    }

    // 2. Injected agent native modules (libinstrument.so / instrument.dll)
    if (!flag && check_agent_modules(pid)) {
        flag = true;
    }

    // 3. Memory trace signatures
    if (!flag) {
        bool mem_flag = true;
        mem_flag &= trace(pid, baseAddress, endAddress, 524294);
        mem_flag &= trace(pid, baseAddress, endAddress, 4242546329);
        if (mem_flag) {
            flag = true;
        }
    }

    if (flag) {
        std::cout << "Validating detection...\n";
        if (__vld_pattern(pid)) {
            std::cout << "[!] Generic injection detected.\n";
        }
        else {
            std::cout << "[-] Injection detected in untested game client.\n";
        }
    }
    else {
        std::cout << "[+] No suspicious java agents were loaded in the game instance.\n";
    }

    std::cout << "Press Enter to continue...\n";
    std::cin.get();
}

#endif
