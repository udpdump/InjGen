# InjGen (Linux & Windows)

> **InjGen Linux Port:** This fork brings full native Linux support to InjGen while preserving complete Windows compatibility. It replaces platform-locked Windows syscalls with native Linux mechanisms (`/proc` virtual filesystem inspection, `process_vm_readv`, dynamic HotSpot Attach & JVMTI tracking, and SIMD string scanning), providing seamless cross-platform injection detection without requiring elevated privileges for core detection.

InjGen is a program focused on detecting any external **JNI/JVMTI injection** into a Java program. As a PoC, it's designed to detect ghost clients in the Minecraft game.

### Tested detections

> Vape Lite Client

> Vape V4 Client

> DoomsDay Client

> Slinky Client

> Sunset Client

> Karma Client


### Tested game clients
*This means that InjGen won't give a false positive in the following game clients:*

> Lunar Client (all versions)

> Feather Client (all versions)

> Minecraft Vanilla (1.7.10 - 1.21.4)

> LabyMod (not fully tested)

*InjGen won't false flag despite you using any of the above with OptiFine or Sodium (or any other non-JNI mod) with Forge or Fabric.*

### Known flags
1. Badlion Client will flag InjGen due to them performing JNI injections
2. Some other hacked clients like Entropy, Dream or Drip may be aswell detected if they perform blatant modifications to the JVM 

### Notes
JNI and JVMTI injections are not the only two injections in the world, if a client injects by using the Forge API (Vape Lite can do it), it won't be detected as using Forge doesn't imply memory changes in the JVM

### Building & Running

#### Linux
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/InjGen
```
*Note for Linux: Core JVMTI attach and agent detection works out-of-the-box as a regular user. If deep virtual memory scanning is desired on systems with strict Yama `ptrace_scope > 0`, run with `sudo` or grant `cap_sys_ptrace`:*
```bash
sudo ./build/InjGen
# or
sudo setcap cap_sys_ptrace=eip ./build/InjGen
./build/InjGen
```

#### Windows
Build via Visual Studio (`Client Detector.sln`) or via CMake.
