#include "proc.hh"
#include "mem.hpp"

auto main() ->
#ifdef _WIN32
__int32
#else
int
#endif
{

    std::unordered_set<DWORD> x;

    detect_all_instances(x);

    if (x.empty()) {
#ifdef _WIN32
        printf("No 'javaw.exe' process detected.\n");
#else
        printf("No 'java' process detected.\n");
#endif
        return 0;
    }

    for (const DWORD pid : x) {
#ifdef _WIN32
        printf("Reading virtual memory in 'javaw.exe' process with PID %d\n", pid);
#else
        printf("Reading virtual memory in 'java' process with PID %d\n", pid);
#endif
        start_memory_scan(pid);
    }

    return 0;

}
