set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "arm64;x86_64")

# libjpeg-turbo's architecture-specific SIMD cannot serve both slices.
if(PORT STREQUAL "libjpeg-turbo")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DWITH_SIMD=OFF")
endif()

# ARM NEON intrinsics cannot compile for the x86_64 slice of a universal build.
if(PORT STREQUAL "libpng")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DPNG_ARM_NEON=off")
endif()

# OpenSSL selects assembly for VCPKG_TARGET_ARCHITECTURE, which cannot be
# compiled for both slices of a universal binary. Use its portable C code.
if(PORT STREQUAL "openssl")
    list(APPEND VCPKG_CONFIGURE_MAKE_OPTIONS no-asm)
endif()
