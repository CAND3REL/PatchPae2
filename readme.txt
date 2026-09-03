PatchPae2 — Standalone Build
Originally by wj32. Standalone build by Suki (via Claude Code).

Patches 32-bit Windows kernels to unlock PAE memory beyond the artificial
4GB limit, enabling up to 128GB of RAM on supported hardware.

Tested on: Windows Vista SP2, Windows 7 SP0, Windows 7 SP1, Windows 8,
Windows 8.1, Windows 10 (builds 10240, 10586, 14393, 23569, 23992)

== Download ==
A pre-compiled PatchPae2.exe (Win32 x86) is available from the Releases page:
  https://github.com/CAND3REL/PatchPae2/releases

The .exe has no external dependencies — it only requires standard Windows
system DLLs (KERNEL32, IMAGEHLP, VERSION, msvcrt) which are present on
every Windows installation.

== Installation ==
1.  Open an elevated Command Prompt window.

2.  cd C:\Windows\system32
    Make sure the current directory is in fact system32.

[[ For Windows 8, Windows 8.1 and Windows 10: ]]
3.  C:\WherePatchPaeIs\PatchPae2.exe -type kernel -o ntoskrnx.exe ntoskrnl.exe
    This will patch the kernel to enable a maximum of 128GB of RAM.
[[ For Windows Vista and Windows 7: ]]
3.  C:\WherePatchPaeIs\PatchPae2.exe -type kernel -o ntkrnlpx.exe ntkrnlpa.exe
    This will patch the kernel to enable a maximum of 128GB of RAM.

4.  C:\WherePatchPaeIs\PatchPae2.exe -type loader -o winloadp.exe winload.exe
    This will patch the loader to disable signature verification.

5.  bcdedit /copy {current} /d "Windows (PAE Patched)"
    This will create a new boot entry. A message should appear:
    The entry was successfully copied to {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}.

[[ For Windows 8, Windows 8.1 and Windows 10: ]]
6.  bcdedit /set {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} kernel ntoskrnx.exe
    This will set our boot entry to load our patched kernel.
[[ For Windows Vista and Windows 7: ]]
6.  bcdedit /set {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} kernel ntkrnlpx.exe
    This will set our boot entry to load our patched kernel.

7.  bcdedit /set {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} path \Windows\system32\winloadp.exe
    This will set our loader to be our patched loader.

8.  bcdedit /set {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} nointegritychecks 1
    This will disable verification of the loader.

9.  bcdedit /set {bootmgr} default {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    This will set our boot entry to be the default.

10. bcdedit /set {bootmgr} timeout 2
    This will set the timeout to be shorter.
    Note: you can change this timeout to whatever you like.

11. Restart the computer and enjoy.

== Removal ==
To remove the patch:
 * Run msconfig, click Boot, highlight the entry named "Windows (PAE Patched)",
   and click Delete.
 * Delete the files ntoskrnx.exe (or ntkrnlpx.exe) and winloadp.exe from
   C:\Windows\system32.

== Updates ==
When Windows Update installs new updates on your computer, you should run
Step 3 again to ensure that you have the latest version of the kernel.

== Supported Versions ==
Kernel patches:
 * Windows Vista / 7 (builds prior to 9200)
 * Windows 8 (build 9200)
 * Windows 8.1 (build 9600)
 * Windows 10 (build 10586)

Loader patches:
 * Windows Vista (builds prior to 7600)
 * Windows 7 SP0 (build 7600)
 * Windows 7 SP1 (build 7601)
 * Windows 7 SP1, revision 23569+ (build 7601, rev >= 23569)
 * Windows 8 (build 9200)
 * Windows 8.1 (build 9600)
 * Windows 10 (builds 10240+)

== Compiling ==
This standalone build has NO external dependencies — it does not require
Process Hacker 2 (phlib) or Visual Studio. It can be compiled with MinGW-w64
on Linux, or with any C compiler targeting Win32 on Windows.

Cross-compiling on Linux with MinGW-w64:
  i686-w64-mingw32-gcc -o PatchPae2.exe PatchPae2/main_standalone.c \
    -municode -limagehlp -lversion -O2 -static

Compiling on Windows with MinGW-w64:
  gcc -o PatchPae2.exe PatchPae2\main_standalone.c ^
    -municode -limagehlp -lversion -O2 -static

Compiling on Windows with MSVC (Developer Command Prompt):
  cl /O2 /MT PatchPae2\main_standalone.c /Fe:PatchPae2.exe ^
    imagehlp.lib version.lib /link /SUBSYSTEM:CONSOLE

The original main.c with Process Hacker 2 (phlib) dependency is still
included for reference. To compile it, you would need Process Hacker 2:
 * ...\ProcessHacker2\phnt\include\...
 * ...\ProcessHacker2\phlib\include\...
 * ...\ProcessHacker2\phlib\bin\...
 * ...\PatchPae2\PatchPae2.sln
