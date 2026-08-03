// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_report.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

const char* kReportName = "animage-crash.txt";

#ifdef _WIN32

const char* describe(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation (bad pointer)";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow (runaway recursion)";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        default: return "unknown";
    }
}

// Addresses are written as module+offset so they survive relocation and can be
// turned back into file and line with:
//     addr2line -e build/src/app/animage.exe -f -C <offset>
void writeFrame(std::FILE* out, void* address) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto here = reinterpret_cast<std::uintptr_t>(address);
        std::fprintf(out, "  %s+0x%llx\n", path,
                     static_cast<unsigned long long>(here - base));
    } else {
        std::fprintf(out, "  0x%p (unknown module)\n", address);
    }
}

void writeReport(const char* what, DWORD code, void* address, EXCEPTION_POINTERS* pointers) {
    std::FILE* out = std::fopen(kReportName, "w");
    if (!out) out = stderr;

    std::fprintf(out, "Animage crashed.\n\n");
    std::fprintf(out, "reason   %s\n", what);
    if (code) {
        std::fprintf(out, "code     0x%08lx  (%s)\n", static_cast<unsigned long>(code),
                     describe(code));
    }
    if (address) std::fprintf(out, "address  %p\n", address);

    if (code == EXCEPTION_ACCESS_VIOLATION && pointers &&
        pointers->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR kind = pointers->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR at = pointers->ExceptionRecord->ExceptionInformation[1];
        std::fprintf(out, "tried to %s address 0x%llx\n",
                     (kind == 1) ? "write" : (kind == 8) ? "execute" : "read",
                     static_cast<unsigned long long>(at));
    }

    void* frames[62] = {};
    const USHORT captured = CaptureStackBackTrace(0, 62, frames, nullptr);
    std::fprintf(out, "\nstack (%u frames, innermost first):\n", captured);
    for (USHORT i = 0; i < captured; ++i) writeFrame(out, frames[i]);

    std::fflush(out);
    if (out != stderr) std::fclose(out);

    std::fprintf(stderr, "\nAnimage crashed: %s. Report written to %s\n", what, kReportName);
    std::fflush(stderr);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) {
    writeReport("unhandled exception", pointers->ExceptionRecord->ExceptionCode,
                pointers->ExceptionRecord->ExceptionAddress, pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif  // _WIN32

void onTerminate() {
    const char* what = "std::terminate";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            static char buffer[512];
            std::snprintf(buffer, sizeof buffer, "uncaught exception: %s", error.what());
            what = buffer;
        } catch (...) {
            what = "uncaught exception of unknown type";
        }
    }
#ifdef _WIN32
    writeReport(what, 0, nullptr, nullptr);
#else
    std::fprintf(stderr, "Animage crashed: %s\n", what);
#endif
    std::abort();
}

}  // namespace

void installCrashHandler() {
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onUnhandledException);
    // A stack overflow leaves too little room to run a handler on the same
    // stack, so ask Windows to keep a slice in reserve for it.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
#endif
}
