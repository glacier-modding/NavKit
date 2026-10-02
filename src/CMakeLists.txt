configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")
configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_BINARY_DIR}/NavKit/NavKitConfig.h")
set(RECAST_DEMO_SOURCES
        extern/RecastDemo/ChunkyTriMesh.cpp
        extern/RecastDemo/InputGeom.cpp
        extern/RecastDemo/MeshLoaderObj.cpp
        extern/RecastDemo/PerfTimer.cpp
        extern/RecastDemo/Sample.cpp
        extern/RecastDemo/SampleInterfaces.cpp
        extern/RecastDemo/Sample_TileMesh.cpp
        extern/RecastDemo/imgui.cpp
        extern/RecastDemo/imguiRenderGL.cpp
)
set(MODEL_SOURCES
        model/ReasoningGrid.cpp
        model/VisionData.cpp
        model/Json.cpp
)
set(NAVWEAKNESS_SOURCES
        extern/NavWeakness/NavPower.cpp
        extern/NavWeakness/NavWeakness.cpp
)
set(MODULE_SOURCES
        module/Airg.cpp
        module/GameConnection.cpp
        module/Grid.cpp
        module/Gui.cpp
        module/InputHandler.cpp
        module/Logger.cpp
        module/Menu.cpp
        module/NavKitSettings.cpp
        module/Navp.cpp
        module/PersistedSettings.cpp
        module/Renderer.cpp
        module/Rpkg.cpp
        module/Scene.cpp
        module/SceneExtract.cpp
        module/SceneMesh.cpp
        module/WxApplication.cpp
)
set(RENDER_SOURCES
        render/CoreProfileDraw.cpp
        render/Mesh.cpp
        render/Model.cpp
        render/Shader.cpp
        render/stb_image_impl.cpp
)
set(UTIL_SOURCES
        util/CommandRunner.cpp
        util/ErrorHandler.cpp
        util/FileUtil.cpp
        util/GridGenerator.cpp
        util/Math.cpp
        util/Pathfinding.cpp
        util/UpdateChecker.cpp
)
add_library(NavKitLib STATIC
        adapter/RecastAdapter.cpp
        extern/easywsclient/easywsclient.cpp
        extern/QuickDigest/quickdigest5.cpp
        ${RECAST_DEMO_SOURCES}
        ${NAVWEAKNESS_SOURCES}
        ${MODEL_SOURCES}
        ${MODULE_SOURCES}
        ${RENDER_SOURCES}
        ${UTIL_SOURCES}
)
target_compile_definitions(NavKitLib PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if (APPLE)
    # Silence platform deprecation notices for OpenGL calls exposed by system headers.
    target_compile_definitions(NavKitLib PUBLIC GL_SILENCE_DEPRECATION)
endif ()

set_property(TARGET NavKitLib PROPERTY CXX_STANDARD 20)

if (WIN32)
    add_library(navkit-rpkg-lib SHARED IMPORTED GLOBAL
            include/navkit-rpkg-lib/navkit-rpkg-lib.h)
    set_target_properties(navkit-rpkg-lib PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/navkit_rpkg_lib.dll.lib"
            IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/navkit_rpkg_lib.dll.lib"
            IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/navkit_rpkg_lib.dll"
            IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/navkit_rpkg_lib.dll"
    )
elseif (APPLE)
    add_library(navkit-rpkg-lib SHARED IMPORTED GLOBAL
            include/navkit-rpkg-lib/navkit-rpkg-lib.h)
    set_target_properties(navkit-rpkg-lib PROPERTIES
            IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/libnavkit_rpkg_lib.dylib"
            IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/libnavkit_rpkg_lib.dylib"
    )
else ()
    message(FATAL_ERROR "navkit-rpkg-lib has no native library for this platform.")
endif ()
target_include_directories(NavKitLib
        PUBLIC
        "${PROJECT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/extern"
        SYSTEM
        ${OPENGL_INCLUDE_DIR}
        ${SIMPLEINI_INCLUDE_DIRS}
)
target_link_libraries(NavKitLib PUBLIC
        GLEW::GLEW
        navkit-rpkg-lib
        OpenGL::GL
        RecastNavigation::DebugUtils
        RecastNavigation::Detour
        RecastNavigation::DetourCrowd
        RecastNavigation::DetourTileCache
        RecastNavigation::Recast
        SDL2::SDL2
        SDL2::SDL2main
        assimp::assimp
        cpptrace::cpptrace
        ftgl
        glm::glm
        httplib::httplib
        nfd::nfd
        simdjson::simdjson
        simpleini
        wx::core
        wx::gl
        wx::base
)

add_executable(NavKit
        NavKit.cpp
)
if (WIN32)
    target_sources(NavKit PRIVATE NavKit.rc)
elseif (APPLE)
    set(NAVKIT_BUNDLE_RESOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/DroidSans.ttf"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/Glacier2Obj.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/hash_list_filtered.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/res.blend"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/vertex.glsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/fragment.glsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/tile.bmp"
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/NavKitLogo.png"
    )
    set_source_files_properties(
            ${NAVKIT_BUNDLE_RESOURCES}
            resource/NavKitLogo.icns
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(NavKit PRIVATE ${NAVKIT_BUNDLE_RESOURCES} resource/NavKitLogo.icns)
    set_target_properties(NavKit PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_ICON_FILE NavKitLogo.icns
            MACOSX_BUNDLE_BUNDLE_NAME NavKit
            MACOSX_BUNDLE_GUI_IDENTIFIER org.glaciermodding.navkit
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            BUILD_RPATH_USE_ORIGIN TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif ()
target_compile_definitions(NavKit PRIVATE NOMINMAX)
target_link_libraries(NavKit PRIVATE NavKitLib)
set(RUNTIME_TARGET_DEPENDENCIES
        navkit-rpkg-lib
)
set(RUNTIME_TARGET_DEPENDENCIES ${RUNTIME_TARGET_DEPENDENCIES} PARENT_SCOPE)

set(RUNTIME_FILE_DEPENDENCIES
        "${NavKit_SOURCE_DIR}/src/resource/DroidSans.ttf"
        "${NavKit_SOURCE_DIR}/src/resource/NavKit.ini"
        "${NavKit_SOURCE_DIR}/src/resource/Glacier2Obj.py"
        "${NavKit_SOURCE_DIR}/src/resource/hash_list_filtered.txt"
        "${NavKit_SOURCE_DIR}/src/resource/res.blend"
        "${NavKit_SOURCE_DIR}/src/resource/vertex.glsl"
        "${NavKit_SOURCE_DIR}/src/resource/fragment.glsl"
        "${NavKit_SOURCE_DIR}/src/resource/tile.bmp"
        "${NavKit_SOURCE_DIR}/src/resource/NavKitLogo.png"
)
set(RUNTIME_FILE_DEPENDENCIES ${RUNTIME_FILE_DEPENDENCIES} PARENT_SCOPE)

set(STAGED_DLL_PATHS "")
if (WIN32)
    foreach(dep ${RUNTIME_TARGET_DEPENDENCIES})
        list(APPEND STAGED_DLL_PATHS "$<TARGET_FILE:${dep}>")
    endforeach()
endif ()
set(STAGED_DLL_PATHS ${STAGED_DLL_PATHS} PARENT_SCOPE)

if (WIN32)
    add_executable(updater
            WIN32
            updater.cpp
    )
    target_link_libraries(updater PRIVATE wx::core wx::base)

    set(RUNTIME_FILE_DEPENDENCIES ${RUNTIME_FILE_DEPENDENCIES}
            "$<TARGET_FILE:updater>"
            PARENT_SCOPE
    )
    add_dependencies(NavKit updater)
endif ()
if (NOT APPLE)
    add_custom_command(TARGET NavKit POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:NavKit>
            ${STAGED_DLL_PATHS}
            ${RUNTIME_FILE_DEPENDENCIES}
            $<TARGET_FILE_DIR:NavKit>
            COMMENT "Copying runtime files to build output directory..."
            VERBATIM
    )
endif ()

if (WIN32)
    set_target_properties(NavKit PROPERTIES WIN32_EXECUTABLE TRUE)
elseif (APPLE)
    install(TARGETS NavKit BUNDLE DESTINATION .)

    set(NAVKIT_BUNDLE_SEARCH_DIRS)
    foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND NAVKIT_BUNDLE_SEARCH_DIRS "${prefix}/lib" "${prefix}/bin" "${prefix}/Frameworks")
    endforeach()
    if (VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        list(APPEND NAVKIT_BUNDLE_SEARCH_DIRS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Frameworks"
        )
    endif()
    list(APPEND NAVKIT_BUNDLE_SEARCH_DIRS
            "${NavKit_SOURCE_DIR}/lib/Debug"
            "${NavKit_SOURCE_DIR}/lib/Release"
    )
    list(REMOVE_DUPLICATES NAVKIT_BUNDLE_SEARCH_DIRS)
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(
            \"\${CMAKE_INSTALL_PREFIX}/NavKit.app/Contents/MacOS/NavKit\"
            \"\"
            \"${NAVKIT_BUNDLE_SEARCH_DIRS}\"
        )
    ")
endif ()

set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
#add_definitions(-DDT_POLYREF64) # TODO: Add Recast Detour 64 bit support