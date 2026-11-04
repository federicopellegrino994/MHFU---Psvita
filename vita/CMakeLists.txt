cmake_minimum_required(VERSION 3.20)
option(POCKET_HOST "Build the diagnostic command-line tool on the development PC" OFF)
if(NOT POCKET_HOST)
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "Set VITASDK to the installed VitaSDK directory")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE FILEPATH "Vita toolchain")
endif()
project(PocketProbe VERSION 0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(POCKET_HOST)
    add_executable(pocket-fixture-generator src/make_fixture.cpp
        "${UPSTREAM}/platforms/psp/recomp/analysis.cpp"
        "${UPSTREAM}/platforms/psp/recomp/emitter.cpp"
        "${UPSTREAM}/platforms/psp/recomp/instruction_emitter.cpp"
        "${UPSTREAM}/third_party/psprecomp/src/decoder.cpp"
        "${UPSTREAM}/third_party/psprecomp/src/guest_memory.cpp")
    target_include_directories(pocket-fixture-generator PRIVATE
        "${UPSTREAM}/platforms/psp/recomp" "${UPSTREAM}/third_party/psprecomp/include")
    set(FIXTURE_GENERATOR pocket-fixture-generator)
else()
    set(POCKET_FIXTURE_GENERATOR "" CACHE FILEPATH "Host-built pocket-fixture-generator")
    if(NOT EXISTS "${POCKET_FIXTURE_GENERATOR}")
        message(FATAL_ERROR "Build pocket-fixture-generator on the host, then set POCKET_FIXTURE_GENERATOR")
    endif()
    set(FIXTURE_GENERATOR "${POCKET_FIXTURE_GENERATOR}")
endif()
set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FIXTURE_FILES "${FIXTURE_DIR}/pocket_fixture.hpp" "${FIXTURE_DIR}/pocket_fixture_part0.cpp" "${FIXTURE_DIR}/pocket_fixture_table.cpp")
add_custom_command(OUTPUT ${FIXTURE_FILES}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FIXTURE_DIR}"
    COMMAND ${FIXTURE_GENERATOR} "${FIXTURE_DIR}"
    DEPENDS ${FIXTURE_GENERATOR} src/make_fixture.cpp VERBATIM)
add_library(pocket_probe STATIC src/probe.cpp
    ${FIXTURE_FILES}
    src/fiber_check.cpp
    src/scheduler_check.cpp
    "${UPSTREAM}/platforms/psp/runtime/src/kernel.cpp"
    "${UPSTREAM}/platforms/psp/runtime/src/sysmem.cpp"
    "${UPSTREAM}/platforms/psp/runtime/src/fiber.cpp"
    "${UPSTREAM}/platforms/psp/tools/iso9660.cpp"
    "${UPSTREAM}/platforms/psp/tools/param_sfo.cpp"
    "${UPSTREAM}/third_party/psprecomp/src/guest_memory.cpp")
target_include_directories(pocket_probe PUBLIC src
    "${FIXTURE_DIR}"
    "${UPSTREAM}/platforms/psp/runtime/src"
    "${UPSTREAM}/platforms/psp/runtime/include"
    "${UPSTREAM}/platforms/psp/tools"
    "${UPSTREAM}/third_party/psprecomp/include")
target_compile_options(pocket_probe PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off -fno-fast-math)
if(POCKET_HOST)
    add_executable(pocket-probe src/cli.cpp)
    target_link_libraries(pocket-probe PRIVATE pocket_probe)
else()
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    add_executable(pocket-probe src/vita_main.cpp "${UPSTREAM}/third_party/stb/stb_truetype.cpp")
    target_include_directories(pocket-probe SYSTEM PRIVATE "${UPSTREAM}/third_party/stb")
    target_link_libraries(pocket-probe PRIVATE pocket_probe SceDisplay_stub SceCtrl_stub
        SceSysmem_stub SceLibKernel_stub SceProcessmgr_stub SceFiber_stub SceSysmodule_stub)
    vita_create_self(pocket-probe.self pocket-probe UNSAFE)
    vita_create_vpk(pocket-probe.vpk PKPROBE01 pocket-probe.self
        VERSION "00.40" NAME "Pocket Probe"
        FILE "${UPSTREAM}/third_party/fonts/liberation-sans/LiberationSans-Regular.ttf" "font.ttf"
             "${UPSTREAM}/third_party/fonts/liberation-sans/LICENSE" "font-license.txt")
endif()
