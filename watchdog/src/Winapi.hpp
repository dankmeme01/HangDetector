#pragma once

#include <Windows.h>
#include <Dbghelp.h>
#include <tlhelp32.h>
#include "Util.hpp"

using FnTableAccess = PVOID(*)(HANDLE, DWORD64);

struct CachedModule {
    uintptr_t base;
    size_t size;
    std::string name;
};

static std::vector<CachedModule> g_cachedModules;

inline std::string wideToUtf8(std::wstring_view wstr) {
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.size(), NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.size(), &str[0], count, NULL, NULL);
    return str;
}

inline void initCachedModules(int pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);

    if (snap == INVALID_HANDLE_VALUE) {
        log("Failed to create module snapshot: {}", GetLastError());
        return;
    }

    MODULEENTRY32W me32;
    me32.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(snap, &me32)) {
        do {
            g_cachedModules.push_back({
                reinterpret_cast<uintptr_t>(me32.modBaseAddr),
                static_cast<size_t>(me32.modBaseSize),
                wideToUtf8(me32.szExePath),
            });
            log("Cached module: {} at {:p} ({} bytes)", g_cachedModules.back().name, (void*)g_cachedModules.back().base, g_cachedModules.back().size);
        } while (Module32NextW(snap, &me32));
    }

    CloseHandle(snap);

    log("Cached {} modules in total", g_cachedModules.size());
}

inline std::optional<CachedModule> findModuleForAddress(uintptr_t addr) {
    for (const auto& mod : g_cachedModules) {
        if (addr >= mod.base && addr < mod.base + mod.size) {
            return mod;
        }
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCTSTR)addr, &module
    )) {
        return std::nullopt;
    }

    char namebuf[512];
    auto len = GetModuleFileNameA(module, namebuf, sizeof(namebuf));
    if (len == 0) {
        return std::nullopt;
    }

    return CachedModule {
        reinterpret_cast<uintptr_t>(module),
        0,
        std::string(namebuf, len),
    };
}

inline DWORD findMainThread(int pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        log("Failed to create thread snapshot: {}", GetLastError());
        return 0;
    }

    THREADENTRY32 te;
    te.dwSize = sizeof(te);

    DWORD mtid = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                mtid = te.th32ThreadID;
                break;
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return mtid;
}

typedef union _UNWIND_CODE {
    struct {
        uint8_t CodeOffset;
        uint8_t UnwindOp : 4;
        uint8_t OpInfo   : 4;
    };
    uint16_t FrameOffset;
} UNWIND_CODE, *PUNWIND_CODE;

typedef struct _UNWIND_INFO {
    uint8_t Version       : 3;
    uint8_t Flags         : 5;
    uint8_t SizeOfProlog;
    uint8_t CountOfCodes;
    uint8_t FrameRegister : 4;
    uint8_t FrameOffset   : 4;
    UNWIND_CODE UnwindCode[1];
/*  UNWIND_CODE MoreUnwindCode[((CountOfCodes + 1) & ~1) - 1];
*   union {
*       OPTIONAL ULONG ExceptionHandler;
*       OPTIONAL ULONG FunctionEntry;
*   };
*   OPTIONAL ULONG ExceptionData[]; */
} UNWIND_INFO, *PUNWIND_INFO;

inline std::string describeFrame(HANDLE parent, void const* addr) {
    std::string out = fmt::format("{:p}", addr);

    auto moduleOpt = findModuleForAddress(reinterpret_cast<uintptr_t>(addr));
    if (!moduleOpt) {
        return out;
    }

    std::string_view name(moduleOpt->name);
    auto lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string_view::npos) {
        name = name.substr(lastSlash + 1);
    }

    auto const diff = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(moduleOpt->base);
    out = fmt::format("{} + 0x{:x}", name, diff);

    // log symbol if possible
    // https://docs.microsoft.com/en-us/windows/win32/debug/retrieving-symbol-information-by-address

    DWORD64 displacement;

    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbolInfo = reinterpret_cast<PSYMBOL_INFO>(buffer);

    symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbolInfo->MaxNameLen = MAX_SYM_NAME;

    if (SymFromAddr(
        parent, static_cast<DWORD64>(reinterpret_cast<uintptr_t>(addr)), &displacement,
        symbolInfo
    )) {
        // if (auto entry = SymFunctionTableAccess64(parent, static_cast<DWORD64>(reinterpret_cast<uintptr_t>(addr)))) {
        //     auto moduleBase = SymGetModuleBase64(parent, static_cast<DWORD64>(reinterpret_cast<uintptr_t>(addr)));
        //     auto runtimeFunction = static_cast<PRUNTIME_FUNCTION>(entry);
        //     auto unwindInfo = reinterpret_cast<PUNWIND_INFO>(moduleBase + runtimeFunction->UnwindInfoAddress);

        //     // This is a chain of unwind info structures, so we traverse back to the first one
        //     while (unwindInfo->Flags & UNW_FLAG_CHAININFO) {
        //         runtimeFunction = (PRUNTIME_FUNCTION)&(unwindInfo->UnwindCode[( unwindInfo->CountOfCodes + 1 ) & ~1]);
        //         unwindInfo = reinterpret_cast<PUNWIND_INFO>(moduleBase + runtimeFunction->UnwindInfoAddress);
        //     }

        //     if (moduleBase + runtimeFunction->BeginAddress != symbolInfo->Address) {
        //         // the symbol address is not the same as the function address
        //         return out;
        //     }
        // }

        auto symbol = std::string(symbolInfo->Name, symbolInfo->NameLen);
        auto offset = displacement;
        out += fmt::format(" ({} + 0x{:x})", symbol, offset);

        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        DWORD displacement2;

        if (SymGetLineFromAddr64(
            parent, static_cast<DWORD64>(reinterpret_cast<uintptr_t>(addr)),
            &displacement2, &line
        )) {
            out += fmt::format(" @ {}:{}", line.FileName, line.LineNumber);
        }
    }

    return out;
}

inline std::vector<std::string> dumpStackTrace(HANDLE parent, HANDLE thread) {
    SymInitialize(parent, NULL, TRUE);

    STACKFRAME64 stack{};

    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(thread, &ctx);

    stack.AddrPC.Offset = ctx.Rip;
    stack.AddrStack.Offset = ctx.Rsp;
    stack.AddrFrame.Offset = ctx.Rbp;

    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Mode = AddrModeFlat;

    std::vector<std::string> frames;

    while (true) {
        if (!StackWalk64(
            IMAGE_FILE_MACHINE_AMD64, parent, thread, &stack, &ctx, nullptr,
            SymFunctionTableAccess64, SymGetModuleBase64, nullptr
        )) {
            break;
        }

        void* addr = reinterpret_cast<void*>(stack.AddrPC.Offset);
        frames.push_back(describeFrame(parent, addr));
    }

    SymCleanup(parent);
    return frames;
}

inline void forceFault(HANDLE process, HANDLE thread) {
    SuspendThread(thread);

    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL;

    if (GetThreadContext(thread, &ctx)) {
        // before overwriting rip, push the current rip on the stack so the stacktrace is complete
        DWORD64 site = ctx.Rip;
        ctx.Rsp -= 8;

        WriteProcessMemory(process, reinterpret_cast<void*>(ctx.Rsp), &site, sizeof(site), nullptr);

        ctx.Rip = 0xdead;
        if (!SetThreadContext(thread, &ctx)) {
            log("Failed to set thread context: {}", GetLastError());
        }
    }

    ResumeThread(thread);
}
