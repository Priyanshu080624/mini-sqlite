# Session Summary: Setting up a Modern C++ Environment on Windows

This document summarizes the different approaches we took to fix the build environment for the `mini-sqlite` project, ultimately arriving at a native CMake solution.

## 1. The Initial Problem
You were trying to run a bash script (`build_and_run.sh`) that compiled a C++ project using C++20 features:
```sh
g++ -std=c++20 -Wall -Wextra -g -fsanitize=address,undefined src/main.cpp src/storage/pager.cpp -o minidb_demo && ./minidb_demo
```
This failed because:
- **Bash wasn't recognized**: Windows PowerShell doesn't support `bash` natively.
- **Outdated Compiler**: Your system's default `g++` was version 6.3.0, which is too old to support C++20 features like `std::byte`.
- **Sanitizer Issues**: The Address Sanitizer (`-fsanitize=address,undefined`) is notoriously difficult to link against on Windows MinGW/GCC ports.

## 2. Attempted Fixes (MSYS2 & WSL)
1. **MSYS2**: We attempted to install a modern GCC compiler via MSYS2. While it successfully compiled the C++20 code, it failed at the linker stage because the Address Sanitizer libraries (`libasan` / `libubsan`) were missing, which is a common limitation of MSYS2.
2. **WSL**: We considered installing Windows Subsystem for Linux (WSL) to get a true Linux environment where the bash script and sanitizers would work flawlessly. However, this felt heavy-handed.

## 3. The Final Solution: Native CMake & MSVC
Instead of relying on workarounds or virtualization, we realized that you already had **CMake** and **Visual Studio (MSVC)** installed on your Windows machine. MSVC fully supports C++20 and has excellent native support for Address Sanitizer on Windows!

We created a `CMakeLists.txt` file that:
- Strictly enforces C++20 standard compliance.
- Automatically handles the directory structure linking (`src/`).
- Dynamically enables Address Sanitizer natively (`/fsanitize=address`) when building with MSVC, ensuring you keep your bug-finding capabilities.

## Result & How to Run
Your project is now fully native to Windows and doesn't require any `.sh` scripts. 

You can build and run it manually in PowerShell:
```powershell
cmake -B build
cmake --build build
```
*(Tip: The easiest way to run and debug this project going forward is to use the **CMake Tools** extension in VS Code. It will automatically handle the build process and correctly inject the Address Sanitizer DLLs when you click the "Run" button at the bottom of your screen!)*
