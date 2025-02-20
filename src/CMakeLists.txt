file(GLOB INCLUDES include/*.h)
file(GLOB INCLUDES extern/*.h)
include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
include_directories(SYSTEM extern)
include_directories(SYSTEM include)

configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")
set(RC_FILE "NavKit.rc")
add_executable (NavKit "NavKit.cpp" "RecastDemo/imgui.cpp" "RecastDemo/imguiRenderGL.cpp" "RecastDemo/MeshLoaderObj.cpp" "RecastDemo/InputGeom.cpp" "RecastDemo/ChunkyTriMesh.cpp" "RecastDemo/SampleInterfaces.cpp" "RecastDemo/PerfTimer.cpp" "ReasoningGrid.cpp"   "RecastDemo/Sample.cpp" "RecastDemo/Sample_SoloMesh.cpp" "GameConnection.cpp" "easywsclient/easywsclient.cpp" "easywsclient/easywsclient.cpp" "easywsclient/easywsclient.cpp" "Navp.cpp" "Obj.cpp" "FileUtil.cpp" "Airg.cpp" "SceneExtract.cpp" "Renderer.cpp" "InputHandler.cpp" "Gui.cpp" "PfBoxes.cpp" "VisionData.cpp" ${RC_FILE})
target_include_directories(NavKit PUBLIC "${PROJECT_BINARY_DIR}")
target_include_directories(NavKit PRIVATE ${SIMPLEINI_INCLUDE_DIRS})
target_link_libraries(NavKit PRIVATE ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )
target_link_libraries(NavKit PRIVATE OpenGL::GLU)
target_link_libraries(NavKit PRIVATE OpenGL::GL)
target_link_libraries(NavKit PRIVATE GLUT::GLUT)
target_link_libraries(NavKit PRIVATE GLEW::GLEW)
target_link_libraries(NavKit PRIVATE simdjson::simdjson)
target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)
target_link_libraries(NavKit PRIVATE RecastNavigation::DebugUtils)
target_link_libraries(NavKit PRIVATE RecastNavigation::Detour)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourCrowd)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourTileCache)
target_link_libraries(NavKit PRIVATE ResourceLib_HM3)
target_link_libraries(NavKit PRIVATE ftgl)

add_library(nfd::nfd SHARED IMPORTED)
add_library(NavWeakness SHARED IMPORTED)
add_library(ResourceLib_HM3 SHARED IMPORTED)
target_link_libraries(NavKit PRIVATE nfd::nfd)
target_link_libraries(NavKit PRIVATE NavWeakness)
target_link_libraries(NavKit PRIVATE ResourceLib_HM3)

file(COPY "${NavKit_SOURCE_DIR}/bin/DroidSans.ttf" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/NavWeakness.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/ResourceLib_HM3.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/Glacier2Obj.exe" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/Glacier2Obj.py" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/Editor.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/alocgen.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/nfd.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/NavKit.ini" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(NavWeakness PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/NavWeakness.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/include/NavWeakness/NavWeakness.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/bin/NavWeakness.dll"
)
set_target_properties(nfd::nfd PROPERTIES
  NFD_OVERRIDE_RECENT_WITH_DEFAULT ON
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/nfd.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/include/nativefiledialog-extended/nfd.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/bin/nfd.dll"
)
set_target_properties(ResourceLib_HM3 PROPERTIES
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/ResourceLib_HM3.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/include/ResourceLib_HM3/ResourceLib_HM3.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/bin/ResourceLib_HM3.dll"
)

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
set_target_properties(NavKit PROPERTIES WIN32_EXECUTABLE TRUE)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
endif()
