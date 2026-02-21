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
)
set(RENDER_SOURCES
        render/Mesh.cpp
        render/Model.cpp
        render/Shader.cpp
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
        ${RECAST_DEMO_SOURCES}
        ${MODEL_SOURCES}
        ${MODULE_SOURCES}
        ${RENDER_SOURCES}
        ${UTIL_SOURCES}
)
target_compile_definitions(NavKitLib PRIVATE NOMINMAX)
set_property(TARGET NavKitLib PROPERTY CXX_STANDARD 20)

add_library(navkit-rpkg-lib SHARED IMPORTED GLOBAL
        include/navkit-rpkg-lib/navkit-rpkg-lib.h)
set_target_properties(navkit-rpkg-lib PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/navkit_rpkg_lib.dll.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/navkit_rpkg_lib.dll.lib"
        IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/navkit_rpkg_lib.dll"
        IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/navkit_rpkg_lib.dll"
)

add_library(NavWeakness SHARED IMPORTED GLOBAL)
set_target_properties(NavWeakness PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.lib"
        IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.dll"
        IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.dll"
)

add_library(ResourceLib_HM3 SHARED IMPORTED GLOBAL)
set_target_properties(ResourceLib_HM3 PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/ResourceLib_HM3.lib"
        IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll"
        IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/ResourceLib_HM3.dll"
)
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
        NavWeakness
        OpenGL::GL
        RecastNavigation::DebugUtils
        RecastNavigation::Detour
        RecastNavigation::DetourCrowd
        RecastNavigation::DetourTileCache
        RecastNavigation::Recast
        ResourceLib_HM3
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
)

add_executable(NavKit
        NavKit.cpp
        NavKit.rc
)
target_compile_definitions(NavKit PRIVATE NOMINMAX)
target_link_libraries(NavKit PRIVATE NavKitLib)
set(RUNTIME_TARGET_DEPENDENCIES
        navkit-rpkg-lib
        NavWeakness
        ResourceLib_HM3
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
)
set(RUNTIME_FILE_DEPENDENCIES ${RUNTIME_FILE_DEPENDENCIES} PARENT_SCOPE)

set(STAGED_DLL_PATHS "")
foreach(dep ${RUNTIME_TARGET_DEPENDENCIES})
    list(APPEND STAGED_DLL_PATHS "$<TARGET_FILE:${dep}>")
endforeach()
set(STAGED_DLL_PATHS ${STAGED_DLL_PATHS} PARENT_SCOPE)

add_executable(updater
        WIN32
        updater.cpp
)
target_link_libraries(updater PRIVATE shell32)

set(RUNTIME_FILE_DEPENDENCIES ${RUNTIME_FILE_DEPENDENCIES}
        "$<TARGET_FILE:updater>"
        PARENT_SCOPE
)
add_dependencies(NavKit updater)
add_custom_command(TARGET NavKit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:NavKit>
        ${STAGED_DLL_PATHS}
        ${RUNTIME_FILE_DEPENDENCIES}
        $<TARGET_FILE_DIR:NavKit>
        COMMENT "Copying runtime files to build output directory..."
        VERBATIM
)

set_target_properties(NavKit PROPERTIES WIN32_EXECUTABLE TRUE)
set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
#add_definitions(-DDT_POLYREF64) # TODO: Add Recast Detour 64 bit support