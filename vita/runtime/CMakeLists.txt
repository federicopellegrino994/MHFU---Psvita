cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK")
endif()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE FILEPATH "Vita toolchain")
project(PocketFullRuntime LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(R "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MHF_BUILD_TESTS OFF)
set(POCKET_FFMPEG_PREFIX "" CACHE PATH "Vita-built FFmpeg installation, not host libraries")
if(NOT EXISTS "${POCKET_FFMPEG_PREFIX}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "Set POCKET_FFMPEG_PREFIX to the Vita FFmpeg installation")
endif()
function(mhf_target_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -ffp-contract=off -fno-fast-math)
endfunction()
function(mhf_use_ffmpeg target)
    # FFmpeg's enum-containing public structs require the same enum ABI on both sides.
    target_compile_options(${target} PRIVATE -fno-short-enums)
    target_include_directories(${target} PRIVATE "${POCKET_FFMPEG_PREFIX}/include")
    target_link_libraries(${target} PRIVATE "${POCKET_FFMPEG_PREFIX}/lib/libavcodec.a"
        "${POCKET_FFMPEG_PREFIX}/lib/libavutil.a" m pthread)
endfunction()
add_subdirectory("${R}/third_party/psprecomp" psprecomp)
add_subdirectory("${R}/third_party/stb" stb)
add_subdirectory("${R}/third_party/pokke_save_crypto" save_crypto)
set(MHF_PSP_OFFLINE ON CACHE BOOL "Initial Vita runtime has no network transport" FORCE)
add_library(mhf_psp_tools STATIC "${R}/platforms/psp/tools/iso9660.cpp" "${R}/platforms/psp/tools/param_sfo.cpp")
target_include_directories(mhf_psp_tools PUBLIC "${R}/platforms/psp/tools")
add_subdirectory("${R}/platforms/psp/runtime" runtime)
# Keep enum layout consistent in libraries sharing C/C++ interfaces.
foreach(dependency psprecomp_core mhf_psp_tools mhf_stb_truetype mhf_save_crypto)
    target_compile_options(${dependency} PRIVATE -fno-short-enums)
endforeach()
# Building the static archive checks compilation, not symbol resolution or boot.
# The game and the reduced test Runtime are deliberately absent from this graph.

set(POCKET_FIXTURE_GENERATOR "" CACHE FILEPATH "Host-built pocket-fixture-generator")
if(POCKET_FIXTURE_GENERATOR)
    set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(FIXTURE_FILES "${FIXTURE_DIR}/pocket_fixture.hpp" "${FIXTURE_DIR}/pocket_fixture_part0.cpp" "${FIXTURE_DIR}/pocket_fixture_table.cpp")
    add_custom_command(OUTPUT ${FIXTURE_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FIXTURE_DIR}"
        COMMAND "${POCKET_FIXTURE_GENERATOR}" "${FIXTURE_DIR}"
        DEPENDS "${POCKET_FIXTURE_GENERATOR}" "${R}/vita/src/make_fixture.cpp" VERBATIM)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    add_executable(pocket-full-runtime
        "${R}/vita/src/vita_main.cpp" "${R}/vita/src/probe.cpp"
        "${R}/vita/src/fiber_check.cpp" scheduler_check.cpp ${FIXTURE_FILES})
    target_include_directories(pocket-full-runtime PRIVATE "${R}/vita/src"
        "${R}/platforms/psp/runtime/src" "${FIXTURE_DIR}" "${R}/third_party/stb")
    target_compile_options(pocket-full-runtime PRIVATE -fno-short-enums)
    target_compile_definitions(pocket-full-runtime PRIVATE POCKET_FULL_RUNTIME=1)
    target_link_libraries(pocket-full-runtime PRIVATE mhf_psp_runtime mhf_stb_truetype
        SceDisplay_stub SceCtrl_stub SceSysmem_stub SceLibKernel_stub SceProcessmgr_stub
        SceFiber_stub SceSysmodule_stub pthread)
    vita_create_self(pocket-full-runtime.self pocket-full-runtime UNSAFE)
    vita_create_vpk(pocket-full-runtime.vpk PKPROBE01 pocket-full-runtime.self
        VERSION "00.50" NAME "Pocket Probe"
        FILE "${R}/third_party/fonts/liberation-sans/LiberationSans-Regular.ttf" "font.ttf"
             "${R}/third_party/fonts/liberation-sans/LICENSE" "font-license.txt")
endif()

# Player-generated sources stay outside version control and release archives.
set(POCKET_GAME_GENERATED "" CACHE PATH "Private mhf_recomp output for ARM compilation")
if(POCKET_GAME_GENERATED)
    if(NOT EXISTS "${POCKET_GAME_GENERATED}/mhfu.hpp")
        message(FATAL_ERROR "POCKET_GAME_GENERATED must contain mhfu.hpp")
    endif()
    file(GLOB game_sources CONFIGURE_DEPENDS "${POCKET_GAME_GENERATED}/*.cpp")
    add_library(pocket_game_generated STATIC ${game_sources})
    target_include_directories(pocket_game_generated PRIVATE "${POCKET_GAME_GENERATED}")
    target_link_libraries(pocket_game_generated PRIVATE mhf_psp_runtime)
    target_compile_options(pocket_game_generated PRIVATE -w -O1 -fno-short-enums)
    add_executable(pocket_game_link_check game_link_check.cpp)
    target_compile_options(pocket_game_link_check PRIVATE -fno-short-enums)
    target_link_libraries(pocket_game_link_check PRIVATE pocket_game_generated mhf_psp_runtime
        SceLibKernel_stub SceFiber_stub SceSysmodule_stub pthread)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/boot_target.cmake")
