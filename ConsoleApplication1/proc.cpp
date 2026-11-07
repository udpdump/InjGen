#include "proc.hh"

#ifdef _WIN32
#include "krnljmper.h"

static _inline bool __fastcall ends_with(std::wstring_view value, std::wstring_view ending) {
    return value.size() >= ending.size() &&
        value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
}

void __fastcall detect_all_instances(std::unordered_set<DWORD>& processes) {
    std::unordered_set<DWORD> validProcesses;

    static DWORD processIds[1024], bytesReturned;

    if (!K32EnumProcesses(processIds, sizeof(processIds), &bytesReturned)) {
        return;
    }

    DWORD numberOfProcesses = bytesReturned / sizeof(DWORD);

    for (DWORD i = 0; i < numberOfProcesses; ++i) {
        HANDLE handle;
        OBJECT_ATTRIBUTES objAttr{};
        CLIENT_ID clientId{};

        clientId.UniqueProcess = ULongToHandle(processIds[i]);

        (&objAttr)->Length = sizeof(OBJECT_ATTRIBUTES);
        (&objAttr)->RootDirectory = nullptr;
        (&objAttr)->Attributes = 0;
        (&objAttr)->ObjectName = nullptr;
        (&objAttr)->SecurityDescriptor = nullptr;
        (&objAttr)->SecurityQualityOfService = 0;

        NTSTATUS status = KeNtOpenProcess(&handle, (0x0400), &objAttr, &clientId);
        if (((NTSTATUS)(status) >= 0)) {
            BYTE buffer[sizeof(UNICODE_STRING) + MAX_PATH * sizeof(WCHAR)] = { 0 };
            PUNICODE_STRING pImageName = (PUNICODE_STRING)buffer;
            pImageName->MaximumLength = MAX_PATH * sizeof(WCHAR);
            pImageName->Buffer = (PWSTR)(pImageName + 1);

            KeNtQueryInformationProcess(
                handle,
                ProcessImageFileName,
                pImageName,
                sizeof(buffer),
                0
            );

            std::wstring_view imageName(pImageName->Buffer, pImageName->Length / sizeof(WCHAR));
            if (ends_with(imageName, L"javaw.exe")) {
                validProcesses.insert(processIds[i]);
            }

            KeNtClose(handle);
        }
    }

    processes.swap(validProcesses);
}

std::pair<DWORD_PTR, DWORD_PTR> __fastcall __get_mem_range(const DWORD processID) {
    DWORD_PTR baseAddress = 0;
    DWORD_PTR endAddress = 0;
    HANDLE hProcess;
    OBJECT_ATTRIBUTES objAttr = { 0 };
    CLIENT_ID clientId = { 0 };
    clientId.UniqueProcess = UlongToHandle(processID);

    InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);
    KeNtOpenProcess(&hProcess, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, &objAttr, &clientId);

    if (hProcess) {
        HMODULE hModules[1024];
        DWORD cbNeeded;

        if (K32EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
            for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                wchar_t moduleName[MAX_PATH];
                if (K32GetModuleFileNameExW(hProcess, hModules[i], moduleName, sizeof(moduleName) / sizeof(WCHAR))) {
                    if (wcsstr(moduleName, L"jvm.dll") != NULL) {
                        baseAddress = (DWORD_PTR)hModules[i];

                        IMAGE_DOS_HEADER dosHeader{};
                        SIZE_T bytesRead = 0;
                        const NTSTATUS readStatus = KeNtReadVirtualMemory(hProcess, reinterpret_cast<PVOID>(baseAddress), &dosHeader, sizeof(dosHeader), &bytesRead);

                        if (readStatus >= 0 && bytesRead == sizeof(dosHeader) && dosHeader.e_magic == 0x5A4D) {
                            IMAGE_NT_HEADERS ntHeaders{};
                            const NTSTATUS ntReadStatus = KeNtReadVirtualMemory(hProcess, reinterpret_cast<PVOID>(baseAddress + dosHeader.e_lfanew), &ntHeaders, sizeof(ntHeaders), &bytesRead);

                            if (ntReadStatus >= 0 && bytesRead == sizeof(ntHeaders)) {
                                endAddress = baseAddress + ntHeaders.OptionalHeader.SizeOfImage;
                            }
                        }
                        break;
                    }
                }
            }
        }
        KeNtClose(hProcess);
    }

    return std::make_pair(baseAddress, endAddress);
}

#else // Linux implementation

#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <climits>
#include <iostream>

static inline bool str_ends_with(std::string_view value, std::string_view ending) {
    return value.size() >= ending.size() &&
        value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
}

void __fastcall detect_all_instances(std::unordered_set<DWORD>& processes) {
    std::unordered_set<DWORD> validProcesses;

    DIR* procDir = opendir("/proc");
    if (!procDir) {
        return;
    }

    pid_t myPid = getpid();
    struct dirent* entry = nullptr;

    while ((entry = readdir(procDir)) != nullptr) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }

        char* endPtr = nullptr;
        long pidLong = strtol(entry->d_name, &endPtr, 10);
        if (*endPtr != '\0' || pidLong <= 0 || pidLong == myPid) {
            continue;
        }

        DWORD pid = static_cast<DWORD>(pidLong);
        bool isJava = false;

        // 1. Check /proc/<pid>/exe target
        char exePath[PATH_MAX] = { 0 };
        std::string procExePath = std::string("/proc/") + entry->d_name + "/exe";
        ssize_t len = readlink(procExePath.c_str(), exePath, sizeof(exePath) - 1);
        if (len > 0) {
            exePath[len] = '\0';
            std::string_view target(exePath, len);
            if (str_ends_with(target, "/java") || str_ends_with(target, "/javaw") ||
                target == "java" || target == "javaw") {
                isJava = true;
            }
        }

        // 2. Check /proc/<pid>/comm
        if (!isJava) {
            std::string commPath = std::string("/proc/") + entry->d_name + "/comm";
            std::ifstream commFile(commPath);
            std::string comm;
            if (commFile >> comm) {
                if (comm == "java" || comm == "javaw") {
                    isJava = true;
                }
            }
        }

        // 3. Check /proc/<pid>/cmdline
        if (!isJava) {
            std::string cmdlinePath = std::string("/proc/") + entry->d_name + "/cmdline";
            std::ifstream cmdlineFile(cmdlinePath, std::ios::binary);
            std::string cmdArg;
            if (std::getline(cmdlineFile, cmdArg, '\0')) {
                if (str_ends_with(cmdArg, "/java") || str_ends_with(cmdArg, "/javaw") ||
                    cmdArg == "java" || cmdArg == "javaw") {
                    isJava = true;
                }
            }
        }

        if (isJava) {
            validProcesses.insert(pid);
        }
    }

    closedir(procDir);
    processes.swap(validProcesses);
}

std::pair<DWORD_PTR, DWORD_PTR> __fastcall __get_mem_range(const DWORD processID) {
    DWORD_PTR baseAddress = 0;
    DWORD_PTR endAddress = 0;

    std::string mapsPath = "/proc/" + std::to_string(processID) + "/maps";
    std::ifstream mapsFile(mapsPath);
    if (!mapsFile.is_open()) {
        return std::make_pair(baseAddress, endAddress);
    }

    std::string line;
    while (std::getline(mapsFile, line)) {
        if (line.empty()) continue;

        if (line.find("libjvm.so") != std::string::npos || line.find("jvm.dll") != std::string::npos) {
            size_t dash = line.find('-');
            size_t space = line.find(' ');
            if (dash != std::string::npos && space != std::string::npos && dash < space) {
                try {
                    DWORD_PTR start = std::stoull(line.substr(0, dash), nullptr, 16);
                    DWORD_PTR end = std::stoull(line.substr(dash + 1, space - (dash + 1)), nullptr, 16);

                    if (baseAddress == 0 || start < baseAddress) {
                        baseAddress = start;
                    }
                    if (end > endAddress) {
                        endAddress = end;
                    }
                } catch (...) {
                    continue;
                }
            }
        }
    }

    return std::make_pair(baseAddress, endAddress);
}

#endif
