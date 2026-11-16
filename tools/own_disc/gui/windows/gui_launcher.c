/* "FivePocket Builder.exe": the window of the builder on Windows (a GUI program: no console window).

   It holds no logic. It finds the folder it lies in (the unpacked builder package) and runs the package's own Python
   without a console, with the window's server and the arguments it was given (disc images dropped onto it):

       <folder>\python\pythonw.exe -B <folder>\tools\own_disc\gui\web_app.py <arguments>

   with the environment builder.exe sets for the builder (BUILDER_PACKAGE_ROOT, BUILDER_PYTHON, BUILDER_HOST_TOOLS,
   PYTHONUTF8=1, PYTHONNOUSERSITE=1). web_app.py serves the window (tools\own_disc\gui\web) on 127.0.0.1 only and
   shows it in Microsoft Edge's app mode, which Windows 10 and 11 carry. Nothing on the machine is changed (no
   registry, no PATH, no installation).

   Every build step the window runs is builder.exe with the command line's own words (builder_core.py), so the window
   adds no second way of building.

   Build (llvm-mingw):  x86_64-w64-mingw32-clang -O2 -municode -mwindows -o "FivePocket Builder.exe" gui_launcher.c
   Nothing here comes from the game. */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define MAX_COMMAND 32768

static int append_argument(wchar_t *out, size_t room, size_t *at, const wchar_t *arg) {
    /* the quoting rules of the Microsoft C runtime (as builder_launcher.c) */
    size_t i = *at;
#define PUT(c) do { if (i + 1 >= room) return 0; out[i++] = (c); } while (0)
    if (i) PUT(L' ');
    PUT(L'"');
    for (const wchar_t *p = arg;; p++) {
        size_t slashes = 0;
        while (*p == L'\\') { p++; slashes++; }
        if (*p == 0) { for (size_t k = 0; k < slashes * 2; k++) PUT(L'\\'); break; }
        if (*p == L'"') { for (size_t k = 0; k < slashes * 2 + 1; k++) PUT(L'\\'); }
        else { for (size_t k = 0; k < slashes; k++) PUT(L'\\'); }
        PUT(*p);
    }
    PUT(L'"');
    out[i] = 0;
    *at = i;
    return 1;
#undef PUT
}

static int fail(const wchar_t *text) {
    MessageBoxW(NULL, text, L"FivePocket Builder", MB_OK | MB_ICONERROR);
    return 70;
}

static int set(const wchar_t *name, const wchar_t *format, const wchar_t *a, const wchar_t *b) {
    static wchar_t value[MAX_COMMAND];
    if (_snwprintf(value, MAX_COMMAND, format, a, b) < 0) return 0;
    return SetEnvironmentVariableW(name, value) != 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR line, int show) {
    (void)instance; (void)previous; (void)line; (void)show;
    static wchar_t folder[MAX_COMMAND], pythonw[MAX_COMMAND], python[MAX_COMMAND], script[MAX_COMMAND];
    static wchar_t web[MAX_COMMAND], command[MAX_COMMAND], message[MAX_COMMAND];
    DWORD n = GetModuleFileNameW(NULL, folder, MAX_COMMAND);
    if (n == 0 || n >= MAX_COMMAND) return fail(L"The builder cannot find its own folder.");
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (!slash) return fail(L"The builder's own path has no folder.");
    *slash = 0;
    if (_snwprintf(pythonw, MAX_COMMAND, L"%ls\\python\\pythonw.exe", folder) < 0 ||
        _snwprintf(python, MAX_COMMAND, L"%ls\\python\\python.exe", folder) < 0 ||
        _snwprintf(script, MAX_COMMAND, L"%ls\\tools\\own_disc\\gui\\web_app.py", folder) < 0 ||
        _snwprintf(web, MAX_COMMAND, L"%ls\\tools\\own_disc\\gui\\web\\index.html", folder) < 0)
        return fail(L"The builder's folder path is too long. Move the unpacked folder to C:\\FivePocket.");
    const wchar_t *needed[] = { pythonw, script, web };
    for (int k = 0; k < 3; k++) {
        if (GetFileAttributesW(needed[k]) == INVALID_FILE_ATTRIBUTES) {
            _snwprintf(message, MAX_COMMAND, L"The builder package is not complete. Missing beside this program:\n\n%ls\n\n"
                       L"Unpack the whole download again (keep every folder).", needed[k]);
            return fail(message);
        }
    }
    if (!set(L"BUILDER_PACKAGE_ROOT", L"%ls%ls", folder, L"") || !set(L"BUILDER_PYTHON", L"%ls%ls", python, L"") ||
        !set(L"BUILDER_HOST_TOOLS", L"%ls\\host-tools%ls", folder, L"") || !set(L"PYTHONUTF8", L"1%ls%ls", L"", L"") ||
        !set(L"PYTHONNOUSERSITE", L"1%ls%ls", L"", L""))
        return fail(L"The builder could not prepare its environment.");
    size_t at = 0;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!append_argument(command, MAX_COMMAND, &at, pythonw) || !append_argument(command, MAX_COMMAND, &at, L"-B") ||
        !append_argument(command, MAX_COMMAND, &at, script))
        return fail(L"The command line is too long.");
    for (int k = 1; argv && k < argc; k++)
        if (!append_argument(command, MAX_COMMAND, &at, argv[k])) return fail(L"Too many files were dropped onto the builder.");
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(pythonw, command, NULL, NULL, FALSE, 0, NULL, folder, &si, &pi)) {
        _snwprintf(message, MAX_COMMAND, L"The builder's Python could not be started (error %lu):\n\n%ls", GetLastError(), pythonw);
        return fail(message);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);   /* the window runs on its own; this program's work is done */
    return 0;
}
