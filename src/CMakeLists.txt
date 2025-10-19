file(GLOB INCLUDES include/*.h)
file(GLOB INCLUDES extern/*.h)
include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
include_directories(SYSTEM extern)
include_directories(SYSTEM include)

configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")
add_library(NavKitLib STATIC
        adapter/RecastAdapter.cpp
        extern/RecastDemo/ChunkyTriMesh.cpp
        extern/RecastDemo/InputGeom.cpp
        extern/RecastDemo/MeshLoaderObj.cpp
        extern/RecastDemo/PerfTimer.cpp
        extern/RecastDemo/Sample.cpp
        extern/RecastDemo/SampleInterfaces.cpp
        extern/RecastDemo/Sample_SoloMesh.cpp
        extern/RecastDemo/Sample_TileMesh.cpp
        extern/RecastDemo/imgui.cpp
        extern/RecastDemo/imguiRenderGL.cpp
        extern/easywsclient/easywsclient.cpp
        model/ReasoningGrid.cpp
        model/VisionData.cpp
        model/ZPathfinding.cpp
        module/Airg.cpp
        module/GameConnection.cpp
        module/Grid.cpp
        module/Gui.cpp
        module/InputHandler.cpp
        module/Logger.cpp
        module/Navp.cpp
        module/Obj.cpp
        module/Renderer.cpp
        module/Scene.cpp
        module/SceneExtract.cpp
        module/Settings.cpp
        util/CommandRunner.cpp
        util/ErrorHandler.cpp
        util/FileUtil.cpp
        util/GridGenerator.cpp
        util/Math.cpp
        util/Pathfinding.cpp
)
set_property(TARGET NavKitLib PROPERTY CXX_STANDARD 20)

add_library(nfd::nfd SHARED IMPORTED)
add_library(NavWeakness SHARED IMPORTED)
add_library(ResourceLib_HM3 SHARED IMPORTED)

target_include_directories(NavKitLib PUBLIC
        "${PROJECT_BINARY_DIR}"
        ${SIMPLEINI_INCLUDE_DIRS}
)
target_link_libraries(NavKitLib PUBLIC
        ${OPENGL_LIBRARIES}
        ${GLUT_LIBRARY}
        OpenGL::GLU
        OpenGL::GL
        GLUT::GLUT
        GLEW::GLEW
        cpptrace::cpptrace
        simdjson::simdjson
        RecastNavigation::Recast
        RecastNavigation::DebugUtils
        RecastNavigation::Detour
        RecastNavigation::DetourCrowd
        RecastNavigation::DetourTileCache
        ResourceLib_HM3
        ftgl
        nfd::nfd
        NavWeakness
        ResourceLib_HM3
        SDL2::SDL2main
        SDL2::SDL2
)

add_executable(NavKit
        NavKit.cpp
        NavKit.rc
)
target_link_libraries(NavKit PRIVATE NavKitLib)
set(OUTPUT_DIR_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug")
set(OUTPUT_DIR_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release")

add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NavKit_SOURCE_DIR}/src/resource/DroidSans.ttf"
        "$<TARGET_FILE_DIR:NavKit>/" # Use generator expression for destination dir
    COMMENT "Copying DroidSans.ttf to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)
set(NAV_WEAKNESS_DLL_PATH_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.dll")
set(NAV_WEAKNESS_DLL_PATH_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.dll")
set(NAV_WEAKNESS_DLL_GENEX "$<IF:$<CONFIG:Debug>,${NAV_WEAKNESS_DLL_PATH_DEBUG},${NAV_WEAKNESS_DLL_PATH_RELEASE}>")
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NAV_WEAKNESS_DLL_GENEX}"
        "$<TARGET_FILE_DIR:NavKit>/" # Use generator expression for destination dir
    COMMENT "Copying NavWeakness.dll to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)

set(RESOURCELIB_DLL_PATH_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll")
set(RESOURCELIB_DLL_PATH_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/ResourceLib_HM3.dll")
set(RESOURCELIB_DLL_GENEX "$<IF:$<CONFIG:Debug>,${RESOURCELIB_DLL_PATH_DEBUG},${RESOURCELIB_DLL_PATH_RELEASE}>")
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RESOURCELIB_DLL_GENEX}"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying ResourceLib_HM3.dll to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)
# Copy Glacier2Obj.exe
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NavKit_SOURCE_DIR}/bin/Glacier2Obj.exe"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying Glacier2Obj.exe to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)

# Copy Glacier2Obj.py
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NavKit_SOURCE_DIR}/src/resource/Glacier2Obj.py"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying Glacier2Obj.py to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)

# Copy Editor.dll
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NavKit_SOURCE_DIR}/bin/Editor.dll"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying Editor.dll to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)
# Copy nfd.dll (Adjust source path if Release version exists)
set(NFD_DLL_PATH_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/nfd.dll")
set(NFD_DLL_GENEX "$<IF:$<CONFIG:Debug>,${NFD_DLL_PATH_DEBUG},${NFD_DLL_PATH_DEBUG}>") # Assuming Debug only for now
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NFD_DLL_GENEX}"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying nfd.dll to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)

# Copy NavKit.ini
add_custom_command(
    TARGET NavKit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NavKit_SOURCE_DIR}/src/resource/NavKit.ini"
        "$<TARGET_FILE_DIR:NavKit>/"
    COMMENT "Copying NavKit.ini to $<TARGET_FILE_DIR:NavKit>"
    VERBATIM
)

set_target_properties(NavWeakness PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/NavWeakness.dll"
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.lib"
        IMPORTED_LOCATION_DEBUG "${OUTPUT_DIR_DEBUG}/NavWeakness.dll"
        IMPORTED_LOCATION_RELEASE "${OUTPUT_DIR_RELEASE}/NavWeakness.dll"
)
set_target_properties(nfd::nfd PROPERTIES
#        NFD_OVERRIDE_RECENT_WITH_DEFAULT ON
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/nfd.dll"
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/nfd.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Debug/nfd.lib"
        IMPORTED_LOCATION_DEBUG "${OUTPUT_DIR_DEBUG}/nfd.dll"
        IMPORTED_LOCATION_RELEASE "${OUTPUT_DIR_RELEASE}/nfd.dll"
)
set_target_properties(ResourceLib_HM3 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/ResourceLib_HM3.dll"
        IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.lib"
        IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/ResourceLib_HM3.lib"
        IMPORTED_LOCATION_DEBUG "${OUTPUT_DIR_DEBUG}/ResourceLib_HM3.dll"
        IMPORTED_LOCATION_RELEASE "${OUTPUT_DIR_RELEASE}/ResourceLib_HM3.dll"
)
set_target_properties(NavKit PROPERTIES WIN32_EXECUTABLE TRUE)

set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
#add_definitions(-DDT_POLYREF64) # TODO: Add Recast Detour 64 bit support