# Cross-compile glibc aarch64 code on Windows with the NDK's clang and the Debian trixie sysroot
# assembled by native/runtime/fetch_debs.py.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(NDK_LLVM "C:/Users/lucas/AppData/Local/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin" CACHE PATH "")
set(UT_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/runtime/out/root" CACHE PATH "")

set(CMAKE_C_COMPILER "${NDK_LLVM}/clang.exe")
set(CMAKE_CXX_COMPILER "${NDK_LLVM}/clang++.exe")
set(CMAKE_AR "${NDK_LLVM}/llvm-ar.exe" CACHE FILEPATH "")
set(CMAKE_RANLIB "${NDK_LLVM}/llvm-ranlib.exe" CACHE FILEPATH "")
set(CMAKE_STRIP "${NDK_LLVM}/llvm-strip.exe" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${UT_SYSROOT}")

set(CMAKE_C_FLAGS_INIT "--rtlib=libgcc")
set(CMAKE_CXX_FLAGS_INIT "--rtlib=libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld --unwindlib=libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld --unwindlib=libgcc")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld --unwindlib=libgcc")

set(CMAKE_FIND_ROOT_PATH "${UT_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${UT_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${UT_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${UT_SYSROOT}/usr/share/pkgconfig")
