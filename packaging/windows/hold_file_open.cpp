// Test helper only: model a host holding a plugin file without write sharing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fputs("usage: hold_file_open.exe FILE STOP_FILE\n", stderr);
        return 2;
    }
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "open failed: %lu\n", GetLastError());
        return 3;
    }
    std::puts("locked");
    std::fflush(stdout);
    const ULONGLONG start = GetTickCount64();
    while (GetFileAttributesW(argv[2]) == INVALID_FILE_ATTRIBUTES
           && GetTickCount64() - start < 30000) {
        Sleep(50);
    }
    CloseHandle(file);
    return 0;
}
