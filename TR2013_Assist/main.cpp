#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kProcessName[] = L"TombRaider.exe";
constexpr char kApplicationVersion[] = "0.1.0-rc1";
constexpr wchar_t kWriteModeMutexName[] =
    L"Local\\TR2013_Assist_WriteMode_0_1_0";
constexpr std::uintptr_t kRootOffset = 0x01F7B6D4;
constexpr std::uintptr_t kFinalOffset = 0x08;
constexpr std::uint32_t kExpectedPeTimestamp = 0x632CE0FA;
constexpr std::uint32_t kExpectedImageSize = 0x024C2000;
constexpr std::uint64_t kExpectedFileSize = 19198000;
constexpr std::int32_t kMinimumTestValue = 0;
constexpr std::int32_t kMaximumTestValue = 999;
constexpr ULONGLONG kAimWaitMilliseconds = 500;
constexpr ULONGLONG kFireHoldMilliseconds = 700;
constexpr ULONGLONG kShotGapMilliseconds = 400;

enum class RunMode {
    ReadOnly,
    WriteOnce,
    InfiniteAmmo,
    Assist,
};

enum class AutoFirePhase {
    Off,
    AimWait,
    FireHold,
    ShotGap,
};

struct AutoFireState {
    AutoFirePhase phase = AutoFirePhase::Off;
    ULONGLONG deadline = 0;
    bool leftButtonHeld = false;
    bool rightButtonHeld = false;
};

struct AmmoDefinition {
    std::string_view name;
    std::uintptr_t weaponOffset;
    std::int32_t infiniteValue;
};

constexpr std::array kAmmoDefinitions{
    AmmoDefinition{"Arrow",   0x34, 30},
    AmmoDefinition{"Pistol",  0x04, 60},
    AmmoDefinition{"Rifle",   0x24, 120},
    AmmoDefinition{"Shotgun", 0x28, 40},
};

struct AmmoSnapshot {
    const AmmoDefinition* definition;
    std::uintptr_t address;
    std::int32_t value;
};

struct GameSignature {
    std::uint32_t peTimestamp;
    std::uint32_t imageSize;
    std::uint64_t fileSize;
};

std::optional<RunMode> ParseRunMode(int argc, char* argv[]) {
    if (argc == 1) {
        return RunMode::Assist;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--diagnostic") {
        return RunMode::ReadOnly;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--write-once") {
        return RunMode::WriteOnce;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--infinite-ammo") {
        return RunMode::InfiniteAmmo;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--assist") {
        return RunMode::Assist;
    }

    return std::nullopt;
}

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() noexcept {
        HANDLE result = handle_;
        handle_ = nullptr;
        return result;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

std::optional<DWORD> FindProcessId(std::wstring_view processName) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return std::nullopt;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot.get(), &entry)) {
        return std::nullopt;
    }

    do {
        if (processName == entry.szExeFile) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &entry));

    return std::nullopt;
}

std::optional<std::uintptr_t> FindModuleBase(HANDLE process,
                                             std::wstring_view moduleName) {
    std::vector<HMODULE> modules(256);

    for (;;) {
        DWORD bytesNeeded = 0;
        if (!K32EnumProcessModulesEx(
                process,
                modules.data(),
                static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                &bytesNeeded,
                LIST_MODULES_ALL)) {
            return std::nullopt;
        }

        if (bytesNeeded <= modules.size() * sizeof(HMODULE)) {
            modules.resize(bytesNeeded / sizeof(HMODULE));
            break;
        }

        modules.resize(bytesNeeded / sizeof(HMODULE));
    }

    for (const HMODULE module : modules) {
        wchar_t baseName[MAX_PATH]{};
        const DWORD length = K32GetModuleBaseNameW(
            process, module, baseName, static_cast<DWORD>(std::size(baseName)));

        if (length != 0 && moduleName == std::wstring_view(baseName, length)) {
            return reinterpret_cast<std::uintptr_t>(module);
        }
    }

    return std::nullopt;
}

template <typename T>
std::optional<T> ReadRemote(HANDLE process, std::uintptr_t address) {
    T value{};
    SIZE_T bytesRead = 0;

    const BOOL ok = ReadProcessMemory(
        process,
        reinterpret_cast<LPCVOID>(address),
        &value,
        sizeof(value),
        &bytesRead);

    if (!ok || bytesRead != sizeof(value)) {
        return std::nullopt;
    }

    return value;
}

std::optional<GameSignature> ReadGameSignature(
    HANDLE process,
    std::uintptr_t moduleBase) {
    const auto dosHeader = ReadRemote<IMAGE_DOS_HEADER>(process, moduleBase);
    if (!dosHeader ||
        dosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
        dosHeader->e_lfanew <= 0 ||
        dosHeader->e_lfanew > 0x1000) {
        return std::nullopt;
    }

    const auto ntHeaders = ReadRemote<IMAGE_NT_HEADERS32>(
        process,
        moduleBase + static_cast<std::uintptr_t>(dosHeader->e_lfanew));
    if (!ntHeaders ||
        ntHeaders->Signature != IMAGE_NT_SIGNATURE ||
        ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return std::nullopt;
    }

    std::vector<wchar_t> imagePath(32768);
    DWORD imagePathLength = static_cast<DWORD>(imagePath.size());
    if (!QueryFullProcessImageNameW(
            process, 0, imagePath.data(), &imagePathLength)) {
        return std::nullopt;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileData{};
    if (!GetFileAttributesExW(
            imagePath.data(),
            GetFileExInfoStandard,
            &fileData)) {
        return std::nullopt;
    }

    const std::uint64_t fileSize =
        (static_cast<std::uint64_t>(fileData.nFileSizeHigh) << 32) |
        fileData.nFileSizeLow;

    return GameSignature{
        ntHeaders->FileHeader.TimeDateStamp,
        ntHeaders->OptionalHeader.SizeOfImage,
        fileSize,
    };
}

bool IsSupportedGameSignature(const GameSignature& signature) {
    return signature.peTimestamp == kExpectedPeTimestamp &&
           signature.imageSize == kExpectedImageSize &&
           signature.fileSize == kExpectedFileSize;
}

bool IsWritableRemoteAddress(HANDLE process, std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQueryEx(
            process,
            reinterpret_cast<LPCVOID>(address),
            &information,
            sizeof(information)) == 0) {
        return false;
    }

    if (information.State != MEM_COMMIT ||
        (information.Protect & PAGE_GUARD) != 0 ||
        (information.Protect & PAGE_NOACCESS) != 0) {
        return false;
    }

    const DWORD baseProtection = information.Protect & 0xFF;
    switch (baseProtection) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            break;
        default:
            return false;
    }

    const auto regionStart =
        reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    const auto regionEnd = regionStart + information.RegionSize;
    return address >= regionStart &&
           address <= regionEnd - sizeof(std::int32_t);
}

bool WriteRemoteInt32(
    HANDLE process,
    std::uintptr_t address,
    std::int32_t value,
    DWORD& error) {
    SIZE_T bytesWritten = 0;
    const BOOL ok = WriteProcessMemory(
        process,
        reinterpret_cast<LPVOID>(address),
        &value,
        sizeof(value),
        &bytesWritten);

    if (!ok || bytesWritten != sizeof(value)) {
        error = GetLastError();
        return false;
    }

    error = ERROR_SUCCESS;
    return true;
}

std::optional<std::uintptr_t> ResolveAmmoAddress(
    HANDLE process,
    std::uint32_t rootPointer,
    std::uintptr_t weaponOffset) {
    const auto weaponPointer = ReadRemote<std::uint32_t>(
        process,
        static_cast<std::uintptr_t>(rootPointer) + weaponOffset);

    if (!weaponPointer || *weaponPointer == 0) {
        return std::nullopt;
    }

    return static_cast<std::uintptr_t>(*weaponPointer) + kFinalOffset;
}

void PrintAddress(std::uintptr_t address) {
    std::cout << "0x" << std::uppercase << std::hex
              << std::setw(sizeof(std::uintptr_t) * 2)
              << std::setfill('0') << address
              << std::dec << std::nouppercase << std::setfill(' ');
}

bool RunWriteOnceTest(
    HANDLE process,
    std::uintptr_t rootAddress,
    const std::vector<AmmoSnapshot>& snapshots) {
    std::cout << "\nCONTROLLED WRITE-ONCE TEST\n"
              << "This mode performs exactly one 4-byte write.\n"
              << "Choose 0 to cancel without writing.\n\n";

    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        std::cout << (index + 1) << ". "
                  << std::left << std::setw(10)
                  << snapshots[index].definition->name
                  << std::right << " current="
                  << snapshots[index].value << '\n';
    }

    std::cout << "\nSelection [0-" << snapshots.size() << "]: ";
    int selection = 0;
    if (!(std::cin >> selection) ||
        selection < 0 ||
        selection > static_cast<int>(snapshots.size())) {
        std::cout << "WRITE TEST: CANCELLED (invalid selection)\n";
        return false;
    }

    if (selection == 0) {
        std::cout << "WRITE TEST: CANCELLED (no memory was changed)\n";
        return true;
    }

    const AmmoSnapshot& selected = snapshots[selection - 1];

    std::cout << "New value [" << kMinimumTestValue << '-'
              << kMaximumTestValue << "]: ";
    long long requestedValue = 0;
    if (!(std::cin >> requestedValue) ||
        requestedValue < kMinimumTestValue ||
        requestedValue > kMaximumTestValue) {
        std::cout << "WRITE TEST: CANCELLED (value outside test range)\n";
        return false;
    }

    if (requestedValue == selected.value) {
        std::cout << "WRITE TEST: CANCELLED (new value equals current value)\n";
        return false;
    }

    std::cout << "\nTarget: " << selected.definition->name
              << "  " << selected.value << " -> " << requestedValue
              << "\nType WRITE to perform one 4-byte write: ";

    std::string confirmation;
    std::cin >> confirmation;
    if (confirmation != "WRITE") {
        std::cout << "WRITE TEST: CANCELLED (confirmation did not match)\n";
        return true;
    }

    // Re-resolve immediately before writing. Abort if the game changed state.
    const auto currentRoot = ReadRemote<std::uint32_t>(process, rootAddress);
    if (!currentRoot || *currentRoot == 0) {
        std::cout << "WRITE TEST: ABORTED (root pointer is no longer valid)\n";
        return false;
    }

    const auto currentAddress = ResolveAmmoAddress(
        process, *currentRoot, selected.definition->weaponOffset);
    if (!currentAddress || *currentAddress != selected.address) {
        std::cout << "WRITE TEST: ABORTED (target address changed)\n";
        return false;
    }

    const auto currentValue = ReadRemote<std::int32_t>(process, *currentAddress);
    if (!currentValue ||
        *currentValue < kMinimumTestValue ||
        *currentValue > kMaximumTestValue) {
        std::cout << "WRITE TEST: ABORTED (current value failed validation)\n";
        return false;
    }

    if (!IsWritableRemoteAddress(process, *currentAddress)) {
        std::cout << "WRITE TEST: ABORTED (target memory is not writable)\n";
        return false;
    }

    DWORD writeError = ERROR_SUCCESS;
    if (!WriteRemoteInt32(
            process,
            *currentAddress,
            static_cast<std::int32_t>(requestedValue),
            writeError)) {
        std::cout << "WRITE TEST: FAILED (Windows error "
                  << writeError << ")\n";
        return false;
    }

    const auto readBack = ReadRemote<std::int32_t>(process, *currentAddress);
    if (!readBack || *readBack != requestedValue) {
        std::cout << "WRITE TEST: FAILED (read-back did not match)\n";
        return false;
    }

    std::cout << "WRITE TEST: PASS\n"
              << selected.definition->name << " is now " << *readBack
              << " at ";
    PrintAddress(*currentAddress);
    std::cout << '\n';
    return true;
}

bool IsForegroundProcess(DWORD processId) {
    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == nullptr) {
        return false;
    }

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    return foregroundProcessId == processId;
}

bool MaintainInfiniteAmmo(HANDLE process, std::uintptr_t rootAddress) {
    const auto rootPointer = ReadRemote<std::uint32_t>(process, rootAddress);
    if (!rootPointer || *rootPointer == 0) {
        return false;
    }

    for (const auto& ammo : kAmmoDefinitions) {
        const auto address = ResolveAmmoAddress(
            process, *rootPointer, ammo.weaponOffset);
        if (!address) {
            return false;
        }

        const auto currentValue = ReadRemote<std::int32_t>(process, *address);
        if (!currentValue ||
            *currentValue < kMinimumTestValue ||
            *currentValue > kMaximumTestValue) {
            return false;
        }

        if (*currentValue == ammo.infiniteValue) {
            continue;
        }

        if (!IsWritableRemoteAddress(process, *address)) {
            return false;
        }

        DWORD writeError = ERROR_SUCCESS;
        if (!WriteRemoteInt32(
                process,
                *address,
                ammo.infiniteValue,
                writeError)) {
            return false;
        }

        const auto readBack = ReadRemote<std::int32_t>(process, *address);
        if (!readBack || *readBack != ammo.infiniteValue) {
            return false;
        }
    }

    return true;
}

bool SendMouseButton(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof(input)) == 1;
}

std::optional<DWORD> ParseProcessId(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    unsigned long long value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }

        value = value * 10 + static_cast<unsigned>(character - '0');
        if (value == 0 || value > MAXDWORD) {
            return std::nullopt;
        }
    }

    return static_cast<DWORD>(value);
}

std::optional<DWORD> StartWatchdog(DWORD gameProcessId) {
    std::vector<wchar_t> executablePath(32768);
    const DWORD pathLength = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size()));

    if (pathLength == 0 || pathLength >= executablePath.size()) {
        return std::nullopt;
    }

    executablePath.resize(pathLength);
    const std::wstring executable(executablePath.begin(), executablePath.end());
    const std::wstring commandLine =
        L"\"" + executable + L"\" --watchdog " +
        std::to_wstring(GetCurrentProcessId()) + L" " +
        std::to_wstring(gameProcessId);

    std::vector<wchar_t> commandBuffer(
        commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};

    const BOOL created = CreateProcessW(
        executable.c_str(),
        commandBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInformation);

    if (!created) {
        return std::nullopt;
    }

    const DWORD watchdogProcessId = processInformation.dwProcessId;
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return watchdogProcessId;
}

int RunWatchdog(DWORD mainProcessId, DWORD gameProcessId) {
    UniqueHandle mainProcess(OpenProcess(
        SYNCHRONIZE, FALSE, mainProcessId));
    UniqueHandle gameProcess(OpenProcess(
        SYNCHRONIZE, FALSE, gameProcessId));

    if (!mainProcess) {
        return 1;
    }

    const DWORD waitResult = WaitForSingleObject(mainProcess.get(), INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        return 1;
    }

    // If the game is still alive, release both buttons once. Duplicate mouse-up
    // events after a normal main-process exit are harmless and keep this path
    // reliable even when the main process was terminated without cleanup.
    if (gameProcess &&
        WaitForSingleObject(gameProcess.get(), 0) == WAIT_TIMEOUT) {
        const bool leftReleased = SendMouseButton(MOUSEEVENTF_LEFTUP);
        const bool rightReleased = SendMouseButton(MOUSEEVENTF_RIGHTUP);
        return (leftReleased && rightReleased) ? 0 : 1;
    }

    return 0;
}

void StopAutoFire(AutoFireState& state) {
    if (state.leftButtonHeld) {
        SendMouseButton(MOUSEEVENTF_LEFTUP);
    }
    if (state.rightButtonHeld) {
        SendMouseButton(MOUSEEVENTF_RIGHTUP);
    }

    state = AutoFireState{};
}

bool StartAutoFire(AutoFireState& state) {
    if (state.phase != AutoFirePhase::Off) {
        return true;
    }

    if (!SendMouseButton(MOUSEEVENTF_RIGHTDOWN)) {
        return false;
    }

    state.rightButtonHeld = true;
    state.phase = AutoFirePhase::AimWait;
    state.deadline = GetTickCount64() + kAimWaitMilliseconds;
    return true;
}

bool AdvanceAutoFire(AutoFireState& state, ULONGLONG now) {
    if (state.phase == AutoFirePhase::Off || now < state.deadline) {
        return true;
    }

    switch (state.phase) {
        case AutoFirePhase::AimWait:
        case AutoFirePhase::ShotGap:
            if (!SendMouseButton(MOUSEEVENTF_LEFTDOWN)) {
                return false;
            }
            state.leftButtonHeld = true;
            state.phase = AutoFirePhase::FireHold;
            state.deadline = now + kFireHoldMilliseconds;
            return true;

        case AutoFirePhase::FireHold:
            if (!SendMouseButton(MOUSEEVENTF_LEFTUP)) {
                return false;
            }
            state.leftButtonHeld = false;
            state.phase = AutoFirePhase::ShotGap;
            state.deadline = now + kShotGapMilliseconds;
            return true;

        case AutoFirePhase::Off:
            return true;
    }

    return false;
}

bool RunInfiniteAmmo(HANDLE process, DWORD processId, std::uintptr_t rootAddress) {
    std::cout << "\nINFINITE AMMO MODE: READY\n"
              << "P          Toggle ON/OFF (game must be foreground)\n"
              << "F12        Emergency OFF\n"
              << "Shift+F12  Exit this program\n\n"
              << "Infinite Ammo: OFF\n";

    bool enabled = false;
    bool running = true;
    bool previousPDown = (GetAsyncKeyState('P') & 0x8000) != 0;
    bool previousF12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    ULONGLONG nextMaintenance = GetTickCount64();

    while (running) {
        const DWORD waitResult = WaitForSingleObject(process, 0);
        if (waitResult == WAIT_OBJECT_0) {
            enabled = false;
            std::cout << "Game process exited. Infinite Ammo: OFF\n";
            break;
        }

        if (waitResult == WAIT_FAILED) {
            enabled = false;
            std::cout << "Safety stop: game process monitoring failed.\n"
                      << "Infinite Ammo: OFF\n";
            return false;
        }

        const bool pDown = (GetAsyncKeyState('P') & 0x8000) != 0;
        const bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        const bool shiftDown =
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (pDown && !previousPDown) {
            if (!IsForegroundProcess(processId)) {
                std::cout << "P ignored: Tomb Raider is not foreground.\n";
            } else if (enabled) {
                enabled = false;
                std::cout << "Infinite Ammo: OFF\n";
            } else if (MaintainInfiniteAmmo(process, rootAddress)) {
                enabled = true;
                nextMaintenance = GetTickCount64() + 50;
                std::cout << "Infinite Ammo: ON\n";
            } else {
                enabled = false;
                std::cout << "Infinite Ammo: NOT READY (validation failed)\n";
            }
        }

        if (f12Down && !previousF12Down) {
            if (shiftDown) {
                enabled = false;
                running = false;
                std::cout << "Infinite Ammo: OFF. Exiting.\n";
            } else {
                enabled = false;
                std::cout << "Emergency stop. Infinite Ammo: OFF\n";
            }
        }

        previousPDown = pDown;
        previousF12Down = f12Down;

        const ULONGLONG now = GetTickCount64();
        if (enabled && now >= nextMaintenance) {
            nextMaintenance = now + 50;

            // Pause writes whenever the game is not the foreground process.
            if (IsForegroundProcess(processId) &&
                !MaintainInfiniteAmmo(process, rootAddress)) {
                enabled = false;
                std::cout << "Safety stop: pointer/value validation failed.\n"
                          << "Infinite Ammo: OFF\n";
            }
        }

        Sleep(10);
    }

    return true;
}

bool RunAssist(HANDLE process, DWORD processId, std::uintptr_t rootAddress) {
    const auto watchdogProcessId = StartWatchdog(processId);
    if (!watchdogProcessId) {
        std::cout << "\nINTEGRATED ASSIST MODE: NOT READY\n"
                  << "Reason: Watchdog could not be started.\n";
        return false;
    }

    std::cout << "\nINTEGRATED ASSIST MODE: READY\n"
              << "Main PID     " << GetCurrentProcessId() << '\n'
              << "Watchdog PID " << *watchdogProcessId << '\n'
              << "P          Infinite Ammo ON/OFF\n"
              << "O          AutoFire ON/OFF (requires P ON)\n"
              << "F12        Emergency stop all functions\n"
              << "Shift+F12  Stop and exit this program\n\n"
              << "Infinite Ammo: OFF\n"
              << "AutoFire: OFF\n";

    bool infiniteAmmoEnabled = false;
    bool running = true;
    AutoFireState autoFire;

    bool previousPDown = (GetAsyncKeyState('P') & 0x8000) != 0;
    bool previousODown = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool previousF12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    ULONGLONG nextMaintenance = GetTickCount64();

    while (running) {
        const DWORD waitResult = WaitForSingleObject(process, 0);
        if (waitResult == WAIT_OBJECT_0) {
            infiniteAmmoEnabled = false;
            StopAutoFire(autoFire);
            std::cout << "Game process exited. All functions: OFF\n";
            break;
        }

        if (waitResult == WAIT_FAILED) {
            infiniteAmmoEnabled = false;
            StopAutoFire(autoFire);
            std::cout << "Safety stop: game process monitoring failed.\n"
                      << "All functions: OFF\n";
            return false;
        }

        const bool gameForeground = IsForegroundProcess(processId);
        const bool pDown = (GetAsyncKeyState('P') & 0x8000) != 0;
        const bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
        const bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        const bool shiftDown =
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (autoFire.phase != AutoFirePhase::Off && !gameForeground) {
            StopAutoFire(autoFire);
            std::cout << "Foreground lost. AutoFire: OFF\n";
        }

        if (pDown && !previousPDown) {
            if (!gameForeground) {
                std::cout << "P ignored: Tomb Raider is not foreground.\n";
            } else if (infiniteAmmoEnabled) {
                infiniteAmmoEnabled = false;
                if (autoFire.phase != AutoFirePhase::Off) {
                    StopAutoFire(autoFire);
                    std::cout << "AutoFire: OFF\n";
                }
                std::cout << "Infinite Ammo: OFF\n";
            } else if (MaintainInfiniteAmmo(process, rootAddress)) {
                infiniteAmmoEnabled = true;
                nextMaintenance = GetTickCount64() + 50;
                std::cout << "Infinite Ammo: ON\n";
            } else {
                infiniteAmmoEnabled = false;
                StopAutoFire(autoFire);
                std::cout << "Infinite Ammo: NOT READY (validation failed)\n";
            }
        }

        if (oDown && !previousODown) {
            if (!gameForeground) {
                std::cout << "O ignored: Tomb Raider is not foreground.\n";
            } else if (!infiniteAmmoEnabled) {
                StopAutoFire(autoFire);
                std::cout << "O ignored: turn Infinite Ammo ON first.\n";
            } else if (autoFire.phase != AutoFirePhase::Off) {
                StopAutoFire(autoFire);
                std::cout << "AutoFire: OFF\n";
            } else if (StartAutoFire(autoFire)) {
                std::cout << "AutoFire: ON\n";
            } else {
                StopAutoFire(autoFire);
                std::cout << "AutoFire: NOT READY (SendInput failed)\n";
            }
        }

        if (f12Down && !previousF12Down) {
            infiniteAmmoEnabled = false;
            StopAutoFire(autoFire);

            if (shiftDown) {
                running = false;
                std::cout << "All functions: OFF. Exiting.\n";
            } else {
                std::cout << "Emergency stop. All functions: OFF\n";
            }
        }

        previousPDown = pDown;
        previousODown = oDown;
        previousF12Down = f12Down;

        const ULONGLONG now = GetTickCount64();
        if (infiniteAmmoEnabled && now >= nextMaintenance) {
            nextMaintenance = now + 50;

            if (gameForeground &&
                !MaintainInfiniteAmmo(process, rootAddress)) {
                infiniteAmmoEnabled = false;
                StopAutoFire(autoFire);
                std::cout << "Safety stop: pointer/value validation failed.\n"
                          << "All functions: OFF\n";
            }
        }

        if (autoFire.phase != AutoFirePhase::Off) {
            if (!infiniteAmmoEnabled || !gameForeground) {
                StopAutoFire(autoFire);
                std::cout << "Safety stop. AutoFire: OFF\n";
            } else if (!AdvanceAutoFire(autoFire, now)) {
                infiniteAmmoEnabled = false;
                StopAutoFire(autoFire);
                std::cout << "Safety stop: SendInput failed.\n"
                          << "All functions: OFF\n";
            }
        }

        Sleep(10);
    }

    StopAutoFire(autoFire);
    return true;
}

void WaitForEnter() {
    std::cout << "\nPress Enter to close...";
    std::cin.get();
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 4 && std::string_view(argv[1]) == "--watchdog") {
        const auto mainProcessId = ParseProcessId(argv[2]);
        const auto gameProcessId = ParseProcessId(argv[3]);
        if (!mainProcessId || !gameProcessId) {
            return 1;
        }

        return RunWatchdog(*mainProcessId, *gameProcessId);
    }

    const auto mode = ParseRunMode(argc, argv);
    if (!mode) {
        std::cout << "Usage:\n"
                  << "  TR2013_Assist.exe              (integrated assist)\n"
                  << "  TR2013_Assist.exe --diagnostic (read-only)\n"
                  << "  TR2013_Assist.exe --write-once (controlled test)\n"
                  << "  TR2013_Assist.exe --infinite-ammo (P toggle)\n"
                  << "  TR2013_Assist.exe --assist (P/O/F12 integration)\n";
        WaitForEnter();
        return 1;
    }

    const bool writeOnce = *mode == RunMode::WriteOnce;
    const bool infiniteAmmo = *mode == RunMode::InfiniteAmmo;
    const bool assist = *mode == RunMode::Assist;
    const bool writeMode = writeOnce || infiniteAmmo || assist;

    UniqueHandle writeModeMutex;
    if (writeMode) {
        writeModeMutex.reset(CreateMutexW(
            nullptr, FALSE, kWriteModeMutexName));
        const DWORD mutexError = GetLastError();
        if (!writeModeMutex) {
            std::cout << "TR2013 Assist: NOT READY\n"
                      << "Reason: single-instance lock could not be created.\n";
            return 1;
        }
        if (mutexError == ERROR_ALREADY_EXISTS) {
            std::cout << "TR2013 Assist: NOT READY\n"
                      << "Reason: another write-mode instance is already running.\n";
            return 1;
        }
    }

    std::cout << "TR2013 Assist v" << kApplicationVersion << " - "
              << (writeOnce
                      ? "Controlled Write-Once Test"
                      : (infiniteAmmo
                             ? "Infinite Ammo Test"
                             : (assist
                                    ? "Integrated Assist Test"
                                    : "Read-Only Diagnostic")))
              << "\n\n";

    const auto processId = FindProcessId(kProcessName);
    if (!processId) {
        std::cout << "STATUS: NOT READY\n"
                  << "Reason: TombRaider.exe was not found. Start the game first.\n";
        WaitForEnter();
        return 1;
    }

    const DWORD processAccess =
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ |
        ((infiniteAmmo || assist) ? SYNCHRONIZE : 0) |
        ((writeOnce || infiniteAmmo || assist)
             ? (PROCESS_VM_WRITE | PROCESS_VM_OPERATION)
             : 0);

    // The default mode still requests read/query access only.
    UniqueHandle process(OpenProcess(
        processAccess,
        FALSE,
        *processId));

    if (!process) {
        std::cout << "STATUS: NOT READY\n"
                  << "Reason: OpenProcess failed (Windows error "
                  << GetLastError() << ").\n";
        WaitForEnter();
        return 1;
    }

    const auto moduleBase = FindModuleBase(process.get(), kProcessName);
    if (!moduleBase) {
        std::cout << "STATUS: NOT READY\n"
                  << "Reason: TombRaider.exe module base was not found.\n";
        WaitForEnter();
        return 1;
    }

    const auto gameSignature = ReadGameSignature(process.get(), *moduleBase);
    if (!gameSignature) {
        std::cout << "STATUS: NOT READY\n"
                  << "Reason: TombRaider.exe version signature could not be read.\n";
        WaitForEnter();
        return 1;
    }

    std::cout << "Game Build    "
              << (IsSupportedGameSignature(*gameSignature)
                      ? "1.01 SUPPORTED"
                      : "UNSUPPORTED")
              << '\n';

    if (!IsSupportedGameSignature(*gameSignature)) {
        std::cout << "PE Timestamp  0x" << std::uppercase << std::hex
                  << gameSignature->peTimestamp << '\n'
                  << "Image Size    0x" << gameSignature->imageSize << '\n'
                  << std::dec << std::nouppercase
                  << "File Size     " << gameSignature->fileSize << " bytes\n"
                  << "\nSTATUS: NOT READY\n"
                  << "Reason: this TombRaider.exe build has not been validated.\n";
        WaitForEnter();
        return 1;
    }

    const std::uintptr_t rootAddress = *moduleBase + kRootOffset;
    const auto rootPointer = ReadRemote<std::uint32_t>(process.get(), rootAddress);
    if (!rootPointer || *rootPointer == 0) {
        std::cout << "STATUS: NOT READY\n"
                  << "Reason: The shared root pointer could not be read.\n";
        WaitForEnter();
        return 1;
    }

    std::cout << "Process       OK (PID " << *processId << ")\n";
    std::cout << "Module Base   ";
    PrintAddress(*moduleBase);
    std::cout << '\n';
    std::cout << "Root Address  ";
    PrintAddress(rootAddress);
    std::cout << '\n';
    std::cout << "Root Pointer  ";
    PrintAddress(*rootPointer);
    std::cout << "\n\n";

    bool allReadSuccessfully = true;
    std::vector<AmmoSnapshot> snapshots;
    snapshots.reserve(kAmmoDefinitions.size());

    for (const auto& ammo : kAmmoDefinitions) {
        const auto address = ResolveAmmoAddress(
            process.get(), *rootPointer, ammo.weaponOffset);

        std::cout << std::left << std::setw(14) << ammo.name << std::right;

        if (!address) {
            std::cout << "UNRESOLVED\n";
            allReadSuccessfully = false;
            continue;
        }

        const auto value = ReadRemote<std::int32_t>(process.get(), *address);
        if (!value) {
            std::cout << "READ FAILED at ";
            PrintAddress(*address);
            std::cout << '\n';
            allReadSuccessfully = false;
            continue;
        }

        std::cout << std::setw(6) << *value << "  [";
        PrintAddress(*address);
        std::cout << "]\n";

        if (*value < kMinimumTestValue || *value > kMaximumTestValue) {
            std::cout << "  Validation failed: value is outside the test range.\n";
            allReadSuccessfully = false;
            continue;
        }

        snapshots.push_back(AmmoSnapshot{&ammo, *address, *value});
    }

    std::cout << "\nSTATUS: "
              << (allReadSuccessfully ? "READY" : "NOT READY")
              << '\n';

    bool testSucceeded = allReadSuccessfully;
    if (allReadSuccessfully && writeOnce) {
        testSucceeded = RunWriteOnceTest(process.get(), rootAddress, snapshots);
    } else if (allReadSuccessfully && infiniteAmmo) {
        testSucceeded = RunInfiniteAmmo(
            process.get(), *processId, rootAddress);
    } else if (allReadSuccessfully && assist) {
        testSucceeded = RunAssist(
            process.get(), *processId, rootAddress);
    }

    if (!infiniteAmmo && !assist) {
        WaitForEnter();
    }
    return testSucceeded ? 0 : 1;
}
