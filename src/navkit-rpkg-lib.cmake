if (NOT NAVKIT_RPKG_LIB_VERSION STREQUAL "v1.3.0")
    message(FATAL_ERROR "Only v1.3.0 SHA-256 values are configured; update the release hashes in src/navkit-rpkg-lib.cmake before selecting another version.")
endif ()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" NAVKIT_RPKG_LIB_PROCESSOR)
if (NOT NAVKIT_RPKG_LIB_PROCESSOR)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" NAVKIT_RPKG_LIB_PROCESSOR)
endif ()

if (WIN32)
    if (NOT NAVKIT_RPKG_LIB_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
        message(FATAL_ERROR "navkit-rpkg-lib release binaries are only available for 64-bit x86 Windows.")
    endif ()
    set(NAVKIT_RPKG_LIB_PLATFORM windows)
    set(NAVKIT_RPKG_LIB_ARCHIVE_EXTENSION zip)
elseif (APPLE)
    if (CMAKE_OSX_ARCHITECTURES)
        foreach (arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if (NOT arch MATCHES "^(arm64|x86_64)$")
                message(FATAL_ERROR "navkit-rpkg-lib macOS binaries support arm64 and x86_64, not ${arch}.")
            endif ()
        endforeach ()
    elseif (NOT NAVKIT_RPKG_LIB_PROCESSOR MATCHES "^(arm64|aarch64|x86_64|amd64)$")
        message(FATAL_ERROR "navkit-rpkg-lib macOS binaries support arm64 and x86_64, not ${NAVKIT_RPKG_LIB_PROCESSOR}.")
    endif ()
    set(NAVKIT_RPKG_LIB_PLATFORM macos)
    set(NAVKIT_RPKG_LIB_ARCHIVE_EXTENSION tar.gz)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NAVKIT_RPKG_LIB_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(NAVKIT_RPKG_LIB_ARCH aarch64)
    elseif (NAVKIT_RPKG_LIB_PROCESSOR MATCHES "^(x86_64|amd64|x64)$")
        set(NAVKIT_RPKG_LIB_ARCH x86_64)
    else ()
        message(FATAL_ERROR "navkit-rpkg-lib has no Linux binary for processor ${NAVKIT_RPKG_LIB_PROCESSOR}.")
    endif ()
    set(NAVKIT_RPKG_LIB_PLATFORM linux)
    set(NAVKIT_RPKG_LIB_ARCHIVE_EXTENSION tar.gz)
else ()
    message(FATAL_ERROR "navkit-rpkg-lib has no binary for ${CMAKE_SYSTEM_NAME}.")
endif ()

message(STATUS "Using navkit-rpkg-lib ${NAVKIT_RPKG_LIB_VERSION} for ${CMAKE_SYSTEM_NAME} (${NAVKIT_RPKG_LIB_PROCESSOR})")

if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "linux")
    set(NAVKIT_RPKG_LIB_ASSET_PREFIX "navkit-rpkg-lib-linux-${NAVKIT_RPKG_LIB_ARCH}")
elseif (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
    set(NAVKIT_RPKG_LIB_ASSET_PREFIX navkit-rpkg-lib-macos)
else ()
    set(NAVKIT_RPKG_LIB_ASSET_PREFIX navkit-rpkg-lib-windows)
endif ()

set(NAVKIT_RPKG_LIB_DIR "${CMAKE_BINARY_DIR}/lib/navkit-rpkg-lib")
set(NAVKIT_RPKG_LIB_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/navkit-rpkg-lib/${NAVKIT_RPKG_LIB_VERSION}")
set(NAVKIT_RPKG_LIB_FIXED_FILES)
foreach (config Debug Release)
    string(TOLOWER "${config}" config_lower)
    set(asset "${NAVKIT_RPKG_LIB_ASSET_PREFIX}-${config_lower}")
    if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "linux")
        if (NAVKIT_RPKG_LIB_ARCH STREQUAL "aarch64")
            if (config STREQUAL "Debug")
                set(sha256 8c803a88a56f9c82595893afc9da5e54c80e818a51c2761cea9124f78577338e)
            else ()
                set(sha256 43362ec0a62cf624e586cbbae3cda4e7137b4ef238b7530184a01fbc993ab940)
            endif ()
        else ()
            if (config STREQUAL "Debug")
                set(sha256 249e955ddac4eaa7c47e06472e53ef9b0e9a0aa9bb481a1d542b43790488f7fb)
            else ()
                set(sha256 62c95ac263b993fcdf94ddab77c85bb5f07294bf7605dc6fb17b6b8a34cc35c0)
            endif ()
        endif ()
    elseif (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
        if (config STREQUAL "Debug")
            set(sha256 81c8bfee6c302a0bc661878c95b13dbada298b8b8a66c974753375f15f4308d3)
        else ()
            set(sha256 be340db3ed453b1ba2d8d4e0b5591c867af8eac17febb466e3784740ca6a955d)
        endif ()
    else ()
        if (config STREQUAL "Debug")
            set(sha256 33b7b4412b1b38b5042ca823be27717bab951374ae58246184704c0285d53093)
        else ()
            set(sha256 888a44c2d94ac18fd2d7507b2993816f82f95fcfeef9961a908e733f7a7463fa)
        endif ()
    endif ()

    set(archive "${NAVKIT_RPKG_LIB_DOWNLOAD_DIR}/${asset}.${NAVKIT_RPKG_LIB_ARCHIVE_EXTENSION}")
    set(extract_dir "${NAVKIT_RPKG_LIB_DOWNLOAD_DIR}/${config}")
    if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "windows")
        set(library "${extract_dir}/navkit_rpkg_lib.dll")
        set(import_library "${extract_dir}/navkit_rpkg_lib.dll.lib")
    elseif (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
        set(library "${extract_dir}/libnavkit_rpkg_lib.dylib")
    else ()
        set(library "${extract_dir}/libnavkit_rpkg_lib.so")
    endif ()

    if (NOT EXISTS "${library}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps" "${extract_dir}")
        set(url "https://github.com/glacier-modding/navkit-rpkg-lib/releases/download/${NAVKIT_RPKG_LIB_VERSION}/${asset}.${NAVKIT_RPKG_LIB_ARCHIVE_EXTENSION}")
        set(archive_is_valid FALSE)
        if (EXISTS "${archive}")
            file(SHA256 "${archive}" archive_sha256)
            if (archive_sha256 STREQUAL sha256)
                set(archive_is_valid TRUE)
                message(STATUS "Using cached navkit-rpkg-lib ${config} archive: ${archive}")
            endif ()
        endif ()
        if (NOT archive_is_valid)
            message(STATUS "Downloading navkit-rpkg-lib ${config} archive for ${NAVKIT_RPKG_LIB_VERSION}: ${url}")
            file(DOWNLOAD "${url}" "${archive}"
                    EXPECTED_HASH "SHA256=${sha256}"
                    TLS_VERIFY ON
                    STATUS download_status
                    SHOW_PROGRESS
            )
            list(GET download_status 0 download_code)
            list(GET download_status 1 download_message)
            if (NOT download_code EQUAL 0)
                message(FATAL_ERROR "Failed to download ${url}: ${download_message}")
            endif ()
        endif ()
        file(SHA256 "${archive}" downloaded_sha256)
        if (NOT downloaded_sha256 STREQUAL sha256)
            message(FATAL_ERROR "SHA-256 mismatch for ${url}: expected ${sha256}, got ${downloaded_sha256}.")
        endif ()
        message(STATUS "Extracting navkit-rpkg-lib ${config} binary to ${extract_dir}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${extract_dir}")
    else ()
        message(STATUS "Using existing navkit-rpkg-lib ${config} binary: ${library}")
    endif ()
    if (NOT EXISTS "${library}")
        message(FATAL_ERROR "Expected navkit-rpkg-lib binary was not found after extracting ${archive}.")
    endif ()
    if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "windows" AND NOT EXISTS "${import_library}")
        message(FATAL_ERROR "Expected navkit-rpkg-lib import library was not found after extracting ${archive}.")
    endif ()

    if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
        set(fixed_library "${NAVKIT_RPKG_LIB_DIR}/${config}/libnavkit_rpkg_lib.dylib")
        add_custom_command(OUTPUT "${fixed_library}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${NAVKIT_RPKG_LIB_DIR}/${config}"
                COMMAND ${CMAKE_COMMAND} -E copy "${library}" "${fixed_library}"
                COMMAND install_name_tool -id "@rpath/libnavkit_rpkg_lib.dylib" "${fixed_library}"
                COMMAND codesign --force --sign - "${fixed_library}"
                DEPENDS "${library}"
                COMMENT "Setting an @rpath install name on the ${config} libnavkit_rpkg_lib.dylib"
                VERBATIM
        )
        list(APPEND NAVKIT_RPKG_LIB_FIXED_FILES "${fixed_library}")
        set("NAVKIT_RPKG_LIB_LOCATION_${config}" "${fixed_library}")
    else ()
        set("NAVKIT_RPKG_LIB_LOCATION_${config}" "${library}")
    endif ()
    set("NAVKIT_RPKG_LIB_IMPLIB_${config}" "${import_library}")
endforeach ()

if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
    add_custom_target(navkit-rpkg-lib-fixup DEPENDS ${NAVKIT_RPKG_LIB_FIXED_FILES})
endif ()

add_library(navkit-rpkg-lib SHARED IMPORTED GLOBAL
        include/navkit-rpkg-lib/navkit-rpkg-lib.h)
set_target_properties(navkit-rpkg-lib PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${NAVKIT_RPKG_LIB_LOCATION_Debug}"
        IMPORTED_LOCATION_RELEASE "${NAVKIT_RPKG_LIB_LOCATION_Release}"
        MAP_IMPORTED_CONFIG_NOCONFIG Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
)
if (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "windows")
    set_target_properties(navkit-rpkg-lib PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${NAVKIT_RPKG_LIB_IMPLIB_Debug}"
            IMPORTED_IMPLIB_RELEASE "${NAVKIT_RPKG_LIB_IMPLIB_Release}"
    )
elseif (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
    set_target_properties(navkit-rpkg-lib PROPERTIES
            IMPORTED_SONAME_DEBUG "@rpath/libnavkit_rpkg_lib.dylib"
            IMPORTED_SONAME_RELEASE "@rpath/libnavkit_rpkg_lib.dylib"
    )
    add_dependencies(navkit-rpkg-lib navkit-rpkg-lib-fixup)
else ()
    # cargo cdylibs have no SONAME, so link by name to avoid an absolute DT_NEEDED path
    set_target_properties(navkit-rpkg-lib PROPERTIES IMPORTED_NO_SONAME TRUE)
endif ()
