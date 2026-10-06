set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "arm64;x86_64")

# OpenSSL selects assembly for VCPKG_TARGET_ARCHITECTURE, which cannot be
# compiled for both slices of a universal binary. Use its portable C code.
if(PORT STREQUAL "openssl")
    list(APPEND VCPKG_CONFIGURE_MAKE_OPTIONS no-asm)
endif()
