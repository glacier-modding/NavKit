cmake_minimum_required(VERSION 3.30)
cmake_policy(SET CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>" CACHE STRING "MSVC debug information format")
project(
        NavKit
        VERSION 2.3.0
        DESCRIPTION "An app to create NAVP and AIRG files for use with Hitman: World of Assassination"
        LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build the tests" OFF)
find_package(BZip2 REQUIRED)
find_package(FTGL CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(cpptrace CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIR NAMES "SimpleIni.h" HINTS "${PROJECT_SOURCE_DIR}/extern" REQUIRED)

add_library(simpleini INTERFACE IMPORTED GLOBAL)
set_target_properties(simpleini PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SIMPLEINI_INCLUDE_DIR}"
)
add_subdirectory(src)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif ()

set(THIRD_PARTY_DLL_PATHS
        "$<TARGET_FILE:GLEW::glew>"
        "$<TARGET_FILE:SDL2::SDL2>"
        "$<TARGET_FILE:cpptrace::cpptrace>"
        "$<TARGET_FILE:freetype>"
        "$<TARGET_FILE:ftgl>"
        "$<TARGET_FILE:nfd::nfd>"
        "$<TARGET_FILE:simdjson::simdjson>"
        "$<TARGET_FILE:unofficial::brotli::brotlicommon>"
        "$<TARGET_FILE:unofficial::brotli::brotlidec>"
        "$<TARGET_FILE_DIR:BZip2::BZip2>/../bin/bz2.dll"
        "$<TARGET_FILE_DIR:OpenSSL::Crypto>/../bin/libcrypto-3-x64.dll"
        "$<TARGET_FILE_DIR:OpenSSL::SSL>/../bin/libssl-3-x64.dll"
        "$<TARGET_FILE_DIR:PNG::PNG>/../bin/libpng16.dll"
        "$<TARGET_FILE_DIR:ZLIB::ZLIB>/../bin/zlib1.dll"
)
option(BUILD_INSTALLER "Build the WiX MSI installer package" OFF)

if(BUILD_INSTALLER)
    if (PROJECT_IS_TOP_LEVEL)
        set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/installer_layout")

        set(COMPANY_NAME "Glacier Modding")
        set(CONTACT_INFO "dannybierek@gmail.com")
        set(WIX_UPGRADE_CODE "65a9e019-b66c-40f3-8ee0-f9d3d31c2554")
        set(WIX_LICENSE_FILE "${CMAKE_SOURCE_DIR}/License.rtf")
        set(WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/src/resource/NavKitLogo.ico")
        find_program(WIX_HEAT_EXECUTABLE heat.exe
                HINTS
                "C:/Program Files/WiX Toolset v6.0/bin/x64"
                "$ENV{WIX}/bin/x64"
                REQUIRED
        )
        find_program(WIX_EXECUTABLE wix.exe
                HINTS
                "C:/Program Files/WiX Toolset v6.0/bin"
                "$ENV{WIX}/bin"
                REQUIRED
        )

        message(STATUS "Using WiX Heat: ${WIX_HEAT_EXECUTABLE}")
        message(STATUS "Using WiX Build: ${WIX_EXECUTABLE}")

            install(TARGETS NavKit
                    RUNTIME DESTINATION .
            )
            install(FILES
                    $<TARGET_FILE:NavWeakness>
                    $<TARGET_FILE:nfd::nfd>
                    $<TARGET_FILE:ResourceLib_HM3>
                    DESTINATION .
            )
            install(FILES ${THIRD_PARTY_DLL_PATHS}
                    DESTINATION .
            )

        install(FILES ${RUNTIME_FILE_DEPENDENCIES}
                DESTINATION .
        )
        set(HARVESTED_COMPONENTS_FILE "${CMAKE_CURRENT_BINARY_DIR}/HarvestedComponents.wxs")
        add_custom_command(
                OUTPUT ${HARVESTED_COMPONENTS_FILE}
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install --config $<CONFIG>
                COMMAND ${WIX_HEAT_EXECUTABLE} dir "${CMAKE_BINARY_DIR}/installer_layout"
                -ag -srd -sreg -cg NavKit_Components -dr INSTALLFOLDER
                -t "${CMAKE_SOURCE_DIR}/wix/heat_exclude_navkit.xslt"
                -o ${HARVESTED_COMPONENTS_FILE}
                DEPENDS NavKit
                COMMENT "Harvesting staged files with WiX heat.exe..."
        )
        add_custom_target(HarvestWixComponents DEPENDS ${HARVESTED_COMPONENTS_FILE})

        set(WIX_INSTALLER_NAME "NavKit.msi")
        add_custom_command(
                OUTPUT "${CMAKE_BINARY_DIR}/${WIX_INSTALLER_NAME}"
                COMMAND ${WIX_EXECUTABLE} build
                -ext WixToolset.UI.wixext
                -b "${CMAKE_BINARY_DIR}/installer_layout"
                -d ProductName="${PROJECT_NAME}"
                -d ProductVersion="${PROJECT_VERSION}"
                -d ProductManufacturer="${COMPANY_NAME}"
                -d ProductContact="${CONTACT_INFO}"
                -d UpgradeCode="${WIX_UPGRADE_CODE}"
                -d LicenseFile="${WIX_LICENSE_FILE}"
                -d ProductIcon="${WIX_PRODUCT_ICON}"
                -d LayoutDir="${CMAKE_BINARY_DIR}/installer_layout"
                -d InstallScope="perUser"
                -d PerUserInstall="1"
                -o "${CMAKE_BINARY_DIR}/${WIX_INSTALLER_NAME}"
                "${CMAKE_SOURCE_DIR}/wix/Package.wxs"
                "${HARVESTED_COMPONENTS_FILE}"
                DEPENDS HarvestWixComponents
                COMMENT "Building installer from staged files..."
        )
        add_custom_target(installer DEPENDS "${CMAKE_BINARY_DIR}/${WIX_INSTALLER_NAME}")
    endif()
endif()
