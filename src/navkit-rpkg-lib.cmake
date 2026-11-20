if (NOT NAVKIT_RPKG_LIB_VERSION STREQUAL "v1.7.0")
    message(FATAL_ERROR "Only v1.7.0 SHA-256 values are configured; update the release hashes in src/navkit-rpkg-lib.cmake before selecting another version.")
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
                set(sha256 64e4dbafdadfc06f4e40e127574e5ef506b1236df65ac4b69bf5853c59c0aa6d)
            else ()
                set(sha256 e80d9808ea0b3a0aec5ae15a1e5503de0146e23c0987b7c3c623faff87b5fdc6)
            endif ()
        else ()
            if (config STREQUAL "Debug")
                set(sha256 6f66849702a21da64d21219cc887b8de29af8cae8edeebc4bda92028dc2f990b)
            else ()
                set(sha256 a26bf0b625a1fd357e8c3d3a6f1a7de5674722c83e4a8d0ecb01f779e67ff215)
            endif ()
        endif ()
    elseif (NAVKIT_RPKG_LIB_PLATFORM STREQUAL "macos")
        if (config STREQUAL "Debug")
            set(sha256 43386dfe3159885087dfcc7522817c2d3b8007d06b9b68925b20c7b61626d79d)
        else ()
            set(sha256 83e51e7d03e8c7fac11ec3b83c2fcbe39b9319ef60526547974ff4856e623318)
        endif ()
    else ()
        if (config STREQUAL "Debug")
            set(sha256 a713bd2cfaf13d94bf7aaf474658679d2ce805e08635ca29389ae58cf58e7087)
        else ()
            set(sha256 79a467dcb86a12acc5714d31c420ad97be6be59f13ffe0fab8ec45aa593e41e3)
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

add_library(navkit-rpkg-lib SHARED IMPORTED GLOBAL)
set_target_properties(navkit-rpkg-lib PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${NAVKIT_RPKG_LIB_LOCATION_Debug}"
        IMPORTED_LOCATION_RELEASE "${NAVKIT_RPKG_LIB_LOCATION_Release}"
        MAP_IMPORTED_CONFIG_NOCONFIG Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        INTERFACE_INCLUDE_DIRECTORIES "${NAVKIT_RPKG_LIB_DOWNLOAD_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>"
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
