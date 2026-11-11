// ============================================================================
//  Setup.exe - the one-file download: NASCAR-GVR-<version>-Setup.exe.
//
//  Tools\Make-Release.ps1 appends the release zip to this program, followed by a 16-byte trailer:
//  the zip's offset in the file (8 bytes, little endian) and the text "NGVRSFX1". When started:
//    1. Windows asks for administrator rights (manifest: requireAdministrator) - once.
//    2. The zip is written to a FRESH %TEMP%\NASCAR_GVR_Setup folder (an old one is deleted first,
//       nothing from an earlier run is reused) and unpacked there.
//    3. Install-NASCAR.ps1 from the unpacked release runs exactly as Install.bat would run it:
//       the game picker, an existing install offered for update, the game's own installer.
//       Command-line options are passed on (Setup.exe -Game 1.1 ...).
//    4. When it has finished, the temporary folder is deleted.
//  A console window shows the progress; the installer's own windows do the talking.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

static const char kMagic[8] = { 'N', 'G', 'V', 'R', 'S', 'F', 'X', '1' };

static void fail(const char* fmt, ...)
{
    char msg[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    printf("\n%s\n", msg);
    MessageBoxA(NULL, msg, "NASCAR GVR setup", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

static void remove_tree(const std::string& dir)
{
    std::string from = dir; from.push_back('\0');      // SHFileOperation wants a double-null list
    SHFILEOPSTRUCTA op = { 0 };
    op.wFunc = FO_DELETE; op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationA(&op);
}

static DWORD run_wait(std::string cmd)
{
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}

// the command line after our own program name, passed on to the installer
static std::string passed_args(void)
{
    const char* c = GetCommandLineA();
    if (*c == '"') { ++c; while (*c && *c != '"') ++c; if (*c) ++c; }
    else while (*c && *c != ' ' && *c != '\t') ++c;
    while (*c == ' ' || *c == '\t') ++c;
    return c;
}

int main(void)
{
    SetConsoleTitleA("NASCAR GVR setup");
    // A click into the console starts "select text" mode, and Windows then PAUSES every program
    // writing to it until the selection ends - the install looked hung (seen 2026-11-11). Off.
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(in, &mode)) SetConsoleMode(in, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    printf("NASCAR GVR setup\n\n");

    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    HANDLE f = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) fail("Setup cannot read itself (%lu):\n%s", GetLastError(), self);
    LARGE_INTEGER size; GetFileSizeEx(f, &size);
    unsigned char tail[16]; DWORD got = 0;
    LARGE_INTEGER pos; pos.QuadPart = size.QuadPart - 16;
    SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
    if (size.QuadPart < 16 || !ReadFile(f, tail, 16, &got, NULL) || got != 16 || memcmp(tail + 8, kMagic, 8) != 0)
        fail("This Setup.exe is damaged or incomplete (no game package inside).\nDownload it again.");
    unsigned long long off = 0; memcpy(&off, tail, 8);
    unsigned long long zipLen = (unsigned long long)size.QuadPart - 16 - off;
    if (off == 0 || off >= (unsigned long long)size.QuadPart - 16)
        fail("This Setup.exe is damaged (bad package position).\nDownload it again.");

    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string work = std::string(tmp) + "NASCAR_GVR_Setup";
    remove_tree(work);                                  // never reuse what an earlier run left
    if (GetFileAttributesA(work.c_str()) != INVALID_FILE_ATTRIBUTES)
        fail("Cannot clear the old temporary folder:\n%s\n\nClose any window using it and run Setup again.", work.c_str());
    CreateDirectoryA(work.c_str(), NULL);
    std::string zip = work + "\\package.zip";

    printf("Unpacking the game package (%llu MB) ...\n", zipLen >> 20);
    HANDLE z = CreateFileA(zip.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (z == INVALID_HANDLE_VALUE) fail("Cannot write to the temporary folder (%lu):\n%s", GetLastError(), zip.c_str());
    pos.QuadPart = (LONGLONG)off; SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
    static char buf[1 << 20];
    for (unsigned long long left = zipLen; left; ) {
        DWORD want = (DWORD)(left < sizeof buf ? left : sizeof buf), n = 0, w = 0;
        if (!ReadFile(f, buf, want, &n, NULL) || n != want) fail("Setup could not read its package (%lu).", GetLastError());
        if (!WriteFile(z, buf, n, &w, NULL) || w != n) fail("Could not write the package to %s (%lu) - is the disk full?", zip.c_str(), GetLastError());
        left -= n;
    }
    CloseHandle(z); CloseHandle(f);

    // The 64-bit PowerShell on 64-bit Windows, exactly as Install.bat gets it: this program is 32-bit,
    // and its "System32" would be redirected to the 32-bit PowerShell, which writes registry entries
    // (Programs and Features) into the 32-bit view - seen 2026-11-11. Sysnative reaches the real one.
    std::string ps = "powershell.exe";
    char win[MAX_PATH];
    if (GetWindowsDirectoryA(win, MAX_PATH)) {
        std::string native = std::string(win) + "\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe";
        ps = GetFileAttributesA(native.c_str()) != INVALID_FILE_ATTRIBUTES ? native
           : std::string(win) + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    std::string out = work + "\\package";
    DWORD rc = run_wait("\"" + ps + "\" -NoProfile -ExecutionPolicy Bypass -Command \"Add-Type -AssemblyName System.IO.Compression.FileSystem; "
                        "[IO.Compression.ZipFile]::ExtractToDirectory('" + zip + "', '" + out + "')\"");
    DeleteFileA(zip.c_str());

    // the zip holds one folder, NASCAR-GVR-<version>
    std::string root;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((out + "\\NASCAR-GVR-*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { root = out + "\\" + fd.cFileName; break; } } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::string picker = root + "\\Install-NASCAR.ps1";
    if (rc != 0 || root.empty() || GetFileAttributesA(picker.c_str()) == INVALID_FILE_ATTRIBUTES) {
        remove_tree(work);
        fail("The game package could not be unpacked (code %lu).\n\nIf an antivirus is active, it may have blocked it: "
             "allow Setup in Windows Security > Protection history, or use the zip download instead.", rc);
    }

    printf("Starting the installer ...\n\n");
    std::string args = passed_args();
    rc = run_wait("\"" + ps + "\" -NoProfile -STA -ExecutionPolicy Bypass -File \"" + picker + "\"" + (args.empty() ? "" : " " + args));

    remove_tree(work);
    return (int)rc;
}
