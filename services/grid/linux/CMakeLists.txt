# Native Linux host for the original engine, not a replacement protocol/simulator.
# Host pointers may be 32 or 64 bits; protocol fields keep their legacy widths.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PORT "${PROJECT_SOURCE_DIR}/services/grid/linux")
set(BOOST "${PROJECT_SOURCE_DIR}/Client/boost_1_34_1")
set(G3D "${PROJECT_SOURCE_DIR}/Client/Rendering/g3d")
file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" root_cmake)
foreach(group APP NETWORK RAKNET)
  string(REGEX MATCH "set\\(OPENRBX_${group}_SOURCES[^)]*\\)" source_block "${root_cmake}")
  string(REGEX MATCHALL "Client/[A-Za-z0-9_./-]+\\.cpp" ${group}_SOURCES "${source_block}")
endforeach()
list(REMOVE_ITEM APP_SOURCES Client/App/util/Http.cpp Client/App/util/FileSystem.cpp)
list(REMOVE_ITEM NETWORK_SOURCES Client/Network/CrashReporter.cpp)
list(REMOVE_ITEM RAKNET_SOURCES Client/Network/raknet30/Source/GetTime.cpp)
add_compile_definitions(NDEBUG OPENRBX_NATIVE_SERVER BOOST_ALL_NO_LIB BOOST_THREAD_BUILD_LIB DATE_TIME_INLINE BOOST_DATE_TIME_STATIC_LINK)
add_compile_options(-pthread)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fms-extensions> $<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing> $<$<COMPILE_LANGUAGE:CXX>:-Wno-microsoft> -Wno-deprecated-declarations -Wno-register -Wno-invalid-constexpr -ferror-limit=6 -ffunction-sections -fdata-sections)
include_directories(BEFORE "${PORT}/include")
include_directories("${PROJECT_SOURCE_DIR}/util" "${BOOST}/src"
  "${PROJECT_SOURCE_DIR}/Client/App/include" "${PROJECT_SOURCE_DIR}/Client/Network/include"
  "${PROJECT_SOURCE_DIR}/Client/Network" "${PROJECT_SOURCE_DIR}/Client/Network/raknet30/Source"
  "${PROJECT_SOURCE_DIR}/Client/Rendering/AppDraw/include" "${PROJECT_SOURCE_DIR}/Client/RbxGraphics/include"
  "${PROJECT_SOURCE_DIR}/Client/Rendering/RenderLib/include" "${PROJECT_SOURCE_DIR}/Client/RbxView/include"
  "${PROJECT_SOURCE_DIR}/Client/RbxViewBase" "${PROJECT_SOURCE_DIR}/Client/App/lua-5.1.1/src"
  "${PROJECT_SOURCE_DIR}/Client/fmod/api/inc" "${G3D}/include" "${G3D}/png" "${G3D}/zlib" "${G3D}/ijg" /usr/include/SDL)
add_compile_options(-include "${PORT}/include/native_prefix.h")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_property(native_includes DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PORT}/prepare.py" "${CMAKE_CURRENT_BINARY_DIR}" ${native_includes}
  COMMAND_ERROR_IS_FATAL ANY)
include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}/case-includes")
file(GLOB lua_sources "${PROJECT_SOURCE_DIR}/Client/App/lua-5.1.1/src/*.c")
list(FILTER lua_sources EXCLUDE REGEX "/(lua|luac|print)\\.c$")
set_source_files_properties(${lua_sources} PROPERTIES LANGUAGE CXX)
file(GLOB g3d_sources "${G3D}/g3dcpp/*.cpp" "${G3D}/ijg/*.c" "${G3D}/png/*.c")
list(REMOVE_ITEM g3d_sources "${G3D}/g3dcpp/debugAssert.cpp")
file(GLOB boost_sources "${BOOST}/libs/signals/src/*.cpp" "${BOOST}/libs/thread/src/*.cpp" "${BOOST}/libs/date_time/src/gregorian/*.cpp")
list(FILTER boost_sources EXCLUDE REGEX "tss_(dll|pe)\\.cpp$")
add_library(native-engine OBJECT ${APP_SOURCES} ${NETWORK_SOURCES} ${RAKNET_SOURCES}
  Client/Rendering/g3d/glg3dcpp/LightingParameters.cpp
  util/decomp.cpp "${PORT}/Assertions.cpp"
  Client/Rendering/AppDraw/Draw.cpp Client/Rendering/AppDraw/DrawAdorn.cpp
  Client/Rendering/AppDraw/HitTest.cpp Client/Rendering/AppDraw/Textures.cpp
  ${lua_sources} ${g3d_sources} ${boost_sources}
  "${PORT}/Platform.cpp" "${PORT}/Http.cpp" "${PORT}/FileSystem.cpp" "${PORT}/NetworkClock.cpp" "${CMAKE_CURRENT_BINARY_DIR}/native-fmod.cpp")
add_executable(grid-server services/grid/worker.cpp $<TARGET_OBJECTS:native-engine>)
target_link_options(grid-server PRIVATE -Wl,--gc-sections)
target_link_libraries(grid-server PRIVATE curl ssl crypto z pthread dl)
option(OPENRBX_GRID_TESTS "Build native engine tests" ON)
if(OPENRBX_GRID_TESTS)
  enable_testing()
  add_executable(grid-clock-test "${PORT}/test_clock.cpp" "${PORT}/NetworkClock.cpp")
  target_link_libraries(grid-clock-test PRIVATE pthread)
  add_test(NAME native-network-clock COMMAND grid-clock-test)
  add_executable(grid-protocol-test "${PORT}/test_protocol.cpp" $<TARGET_OBJECTS:native-engine>)
  target_include_directories(grid-protocol-test PRIVATE "${PROJECT_SOURCE_DIR}/Client/Network")
  target_link_options(grid-protocol-test PRIVATE -Wl,--gc-sections)
  target_link_libraries(grid-protocol-test PRIVATE curl ssl crypto z pthread dl)
  add_test(NAME native-legacy-protocol COMMAND grid-protocol-test)
  add_executable(grid-simulation-test "${PORT}/test_simulation.cpp" $<TARGET_OBJECTS:native-engine>)
  target_link_options(grid-simulation-test PRIVATE -Wl,--gc-sections)
  target_link_libraries(grid-simulation-test PRIVATE curl ssl crypto z pthread dl)
endif()
