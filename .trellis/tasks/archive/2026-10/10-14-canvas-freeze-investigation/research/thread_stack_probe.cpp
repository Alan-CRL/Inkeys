// 独立诊断产物；不接入产品工程。用 ARM64 原生 MSVC、/std:c++17 /EHsc /utf-8 编译。
// 用法：thread_stack_probe.exe <Inkeys PID> [本地 Inkeys.pdb 或符号目录的绝对路径]
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")
#if !defined(_M_ARM64) || defined(_M_ARM64EC)
#error Build this diagnostic with the native ARM64 compiler (not x64/ARM64EC).
#endif

namespace {
class Handle final {
public:
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

std::string Utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<unicode-conversion-failed>";
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) throw std::runtime_error("Absolute local path required.");
    return path.substr(0, slash + 1);
}

// 不接受 UNC、网络盘、SRV*、搜索路径列表或 CodeView 中隐含的远程 PDB 路径。
bool IsLocalPath(const std::wstring& path) {
    if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
        path.find_first_of(L"*;\r\n") != std::wstring::npos) return false;
    wchar_t volume[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volume, MAX_PATH)) return false;
    const UINT type = GetDriveTypeW(volume);
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK;
}

std::wstring SymbolDirectory(const std::wstring& path) {
    if (!IsLocalPath(path)) throw std::runtime_error("Symbol path must be one absolute local file/directory.");
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Symbol path does not exist.");
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return path;
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || _wcsicmp(path.c_str() + dot, L".pdb") != 0)
        throw std::runtime_error("Symbol file must be a .pdb file.");
    return ParentDirectory(path);
}

bool IsInkeysPid(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw std::runtime_error("Process snapshot failed.");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL found = Process32FirstW(snapshot.Get(), &entry); found;
         found = Process32NextW(snapshot.Get(), &entry)) {
        if (entry.th32ProcessID == pid) return _wcsicmp(entry.szExeFile, L"Inkeys.exe") == 0;
    }
    return false;
}

std::vector<DWORD> ThreadIds(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snapshot) throw std::runtime_error("Thread snapshot failed.");
    std::vector<DWORD> ids;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL found = Thread32First(snapshot.Get(), &entry); found;
         found = Thread32Next(snapshot.Get(), &entry)) {
        if (entry.th32OwnerProcessID == pid) ids.push_back(entry.th32ThreadID);
    }
    return ids;
}

void LoadModules(HANDLE process, DWORD pid) {
    HANDLE rawSnapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3; ++attempt) {
        rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (rawSnapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    Handle snapshot(rawSnapshot);
    if (!snapshot) throw std::runtime_error("Module snapshot failed.");
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot.Get(), &module)) throw std::runtime_error("Module enumeration failed.");
    do {
        if (!IsLocalPath(module.szExePath)) {
            std::cout << "MODULE skipped_nonlocal=" << Utf8(module.szExePath) << '\n';
            continue;
        }
        SetLastError(ERROR_SUCCESS);
        const DWORD64 base = SymLoadModuleExW(process, nullptr, module.szExePath, nullptr,
            reinterpret_cast<DWORD64>(module.modBaseAddr), module.modBaseSize, nullptr, 0);
        const DWORD error = GetLastError();
        std::cout << "MODULE name=" << Utf8(module.szModule) << " base=0x" << std::hex
                  << reinterpret_cast<DWORD64>(module.modBaseAddr) << std::dec
                  << " load=" << (base != 0 || error == ERROR_SUCCESS) << " error=" << error << '\n';
    } while (Module32NextW(snapshot.Get(), &module));
}

struct Address { DWORD64 pc = 0; DWORD64 sp = 0; };
struct Capture {
    std::array<Address, 64> frames{};
    size_t count = 0;
    DWORD suspendPrevious = MAXDWORD;
    DWORD suspendError = 0;
    DWORD contextError = 0;
    DWORD resumePrevious = MAXDWORD;
    DWORD resumeError = 0;
    bool truncated = false;
};

// 本采样器只增加一次 suspend count；所有 C++ 正常/异常退出均恰好归还一次。
class Suspension final {
public:
    Suspension(HANDLE thread, Capture& capture) noexcept : thread_(thread), capture_(capture) {
        capture_.suspendPrevious = SuspendThread(thread_);
        active_ = capture_.suspendPrevious != MAXDWORD;
        if (!active_) capture_.suspendError = GetLastError();
    }
    ~Suspension() {
        if (!active_) return;
        capture_.resumePrevious = ResumeThread(thread_);
        if (capture_.resumePrevious == MAXDWORD) capture_.resumeError = GetLastError();
    }
    Suspension(const Suspension&) = delete;
    Suspension& operator=(const Suspension&) = delete;
    explicit operator bool() const noexcept { return active_; }
private:
    HANDLE thread_;
    Capture& capture_;
    bool active_ = false;
};

void CaptureStack(HANDLE process, HANDLE thread, Capture& capture) {
    Suspension suspension(thread, capture);
    if (!suspension) return;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread, &context)) {
        capture.contextError = GetLastError();
        return;
    }
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Pc, 0, AddrModeFlat};
    frame.AddrFrame = {context.Fp, 0, AddrModeFlat};
    frame.AddrStack = {context.Sp, 0, AddrModeFlat};
    capture.frames[capture.count++] = {context.Pc, context.Sp};
    bool skippedInitialDuplicate = false;
    while (capture.count < capture.frames.size() &&
           StackWalk64(IMAGE_FILE_MACHINE_ARM64, process, thread, &frame, &context, nullptr,
               SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
        if (!frame.AddrPC.Offset) break;
        const auto& previous = capture.frames[capture.count - 1];
        if (previous.pc == frame.AddrPC.Offset && previous.sp == frame.AddrStack.Offset) {
            if (skippedInitialDuplicate || capture.count != 1) break;
            skippedInitialDuplicate = true;
            continue;
        }
        capture.frames[capture.count++] = {frame.AddrPC.Offset, frame.AddrStack.Offset};
    }
    capture.truncated = capture.count == capture.frames.size();
    // 此作用域结束先恢复线程；符号输出和 stdout 阻塞均发生在恢复之后。
}

bool LoadedModuleForAddress(HANDLE process, DWORD64 pc, IMAGEHLP_MODULE64& module) {
    module = {};
    module.SizeOfStruct = sizeof(module);
    if (!SymGetModuleInfo64(process, pc, &module)) return false;
    return module.BaseOfImage != 0 && pc >= module.BaseOfImage &&
        pc - module.BaseOfImage < module.ImageSize;
}

bool PrintAddressDetails(HANDLE process, DWORD64 pc) {
    bool resolved = false;
    IMAGEHLP_MODULE64 module{};
    if (LoadedModuleForAddress(process, pc, module)) {
        std::cout << " " << module.ModuleName << "+0x" << std::hex << (pc - module.BaseOfImage) << std::dec;
        resolved = true;
    }
    alignas(SYMBOL_INFO) std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, pc, &displacement, symbol)) {
        std::cout << " " << symbol->Name << "+0x" << std::hex << displacement << std::dec;
        resolved = true;
    }
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(process, pc, &lineDisplacement, &line)) {
        std::cout << " [" << line.FileName << ':' << line.LineNumber << "+" << lineDisplacement << ']';
        resolved = true;
    }
    return resolved;
}

void PrintStack(HANDLE process, const Capture& capture) {
    for (size_t i = 0; i < capture.count; ++i) {
        const DWORD64 pc = capture.frames[i].pc;
        std::cout << "  #" << std::setw(2) << std::setfill('0') << i << std::setfill(' ')
                  << " pc=0x" << std::hex << pc << " sp=0x" << capture.frames[i].sp << std::dec;
        if (!PrintAddressDetails(process, pc)) {
            // 只为已捕获的原始地址给解析候选；不改 CONTEXT、不继续 unwind、不新增帧。
            const DWORD64 candidate = pc & 0x0000FFFFFFFFFFFFULL;
            IMAGEHLP_MODULE64 module{};
            if (candidate != pc && LoadedModuleForAddress(process, candidate, module)) {
                std::cout << " normalized-candidate=0x" << std::hex << candidate << std::dec
                          << " candidate-only=1";
                PrintAddressDetails(process, candidate);
            }
        }
        std::cout << '\n';
    }
}

class Symbols final {
public:
    Symbols(HANDLE process, const std::wstring& path) : process_(process) {
        // 不继承环境符号路径，不读嵌入式远程 PDB 路径，也不启用 symbol server。
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
            SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH | SYMOPT_IGNORE_CVREC |
            SYMOPT_DISABLE_SYMSRV_AUTODETECT | SYMOPT_NO_IMAGE_SEARCH);
        if (!SymInitializeW(process_, path.c_str(), FALSE))
            throw std::runtime_error("SymInitializeW failed.");
    }
    ~Symbols() { SymCleanup(process_); }
    Symbols(const Symbols&) = delete;
    Symbols& operator=(const Symbols&) = delete;
private:
    HANDLE process_;
};
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2 || argc > 3) {
            std::cerr << "Usage: thread_stack_probe.exe <Inkeys PID> [absolute local Inkeys.pdb or symbol directory]\n";
            return 2;
        }
        wchar_t* end = nullptr;
        errno = 0;
        const auto parsed = std::wcstoull(argv[1], &end, 10);
        if (errno || end == argv[1] || *end || parsed == 0 || parsed > std::numeric_limits<DWORD>::max())
            throw std::runtime_error("Invalid PID.");
        const DWORD pid = static_cast<DWORD>(parsed);
        if (pid == GetCurrentProcessId() || !IsInkeysPid(pid))
            throw std::runtime_error("Refusing: PID is not an existing Inkeys.exe process.");
        // 只打开用户给定的目标，不申请调试特权，不注入、不写进程内存。
        Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        if (!process) throw std::runtime_error("OpenProcess failed.");
        std::wstring image(32768, L'\0');
        DWORD imageLength = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(process.Get(), 0, image.data(), &imageLength))
            throw std::runtime_error("QueryFullProcessImageNameW failed.");
        image.resize(imageLength);
        const auto slash = image.find_last_of(L"\\/");
        if (slash == std::wstring::npos || _wcsicmp(image.c_str() + slash + 1, L"Inkeys.exe") != 0)
            throw std::runtime_error("Refusing: opened process identity changed.");
        USHORT machine = 0, nativeMachine = 0;
        if (!IsWow64Process2(process.Get(), &machine, &nativeMachine) ||
            nativeMachine != IMAGE_FILE_MACHINE_ARM64 || machine != IMAGE_FILE_MACHINE_UNKNOWN)
            throw std::runtime_error("Refusing: target must be native ARM64 Inkeys.exe.");
        const std::wstring imageDirectory = SymbolDirectory(ParentDirectory(image));
        const std::wstring suppliedDirectory = argc == 3 ? SymbolDirectory(argv[2]) : imageDirectory;
        const std::wstring symbolPath = imageDirectory == suppliedDirectory ? imageDirectory :
            suppliedDirectory + L";" + imageDirectory;
        const auto ids = ThreadIds(pid);
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        std::cout << "PROCESS pid=" << pid << " image=" << Utf8(image.c_str())
                  << " architecture=ARM64 threads=" << ids.size() << " utc=" << utc.wYear << '-'
                  << utc.wMonth << '-' << utc.wDay << 'T' << utc.wHour << ':' << utc.wMinute << ':'
                  << utc.wSecond << '.' << utc.wMilliseconds << "Z\nSYMBOL_PATH " << Utf8(symbolPath.c_str()) << '\n';
        Symbols symbols(process.Get(), symbolPath);
        // 预先加载本地模块/PDB，避免目标线程暂停期间才开始大量符号 I/O。
        LoadModules(process.Get(), pid);
        size_t sampled = 0, failures = 0;
        for (const DWORD tid : ids) {
            Handle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid));
            if (!thread) {
                const DWORD error = GetLastError();
                std::cout << "THREAD tid=" << tid << " open_error=" << error << '\n';
                ++failures;
                continue;
            }
            // 线程可能在枚举之后退出且 TID 被复用；只采样仍属于该 PID 的线程。
            if (GetProcessIdOfThread(thread.Get()) != pid) {
                std::cout << "THREAD tid=" << tid << " skipped_owner_changed=1\n";
                ++failures;
                continue;
            }
            PWSTR rawDescription = nullptr;
            const HRESULT descriptionResult = GetThreadDescription(thread.Get(), &rawDescription);
            std::string description;
            try { description = Utf8(rawDescription); }
            catch (...) { if (rawDescription) LocalFree(rawDescription); throw; }
            if (rawDescription) LocalFree(rawDescription);
            Capture capture;
            CaptureStack(process.Get(), thread.Get(), capture);
            std::cout << "THREAD tid=" << tid << " name=" << std::quoted(description)
                      << " description_hr=0x" << std::hex << static_cast<unsigned long>(descriptionResult) << std::dec
                      << " frames=" << capture.count << " suspend_previous=" << capture.suspendPrevious
                      << " suspend_error=" << capture.suspendError << " context_error=" << capture.contextError
                      << " resume_previous=" << capture.resumePrevious << " resume_error=" << capture.resumeError
                      << " truncated=" << capture.truncated << '\n';
            PrintStack(process.Get(), capture);
            if (!capture.count || capture.resumeError) ++failures; else ++sampled;
        }
        std::cout << "DONE sampled=" << sampled << " failed=" << failures << " single_pass=1\n";
        return failures == 0 ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << " win32_last_error=" << GetLastError() << '\n';
        return 1;
    }
}
