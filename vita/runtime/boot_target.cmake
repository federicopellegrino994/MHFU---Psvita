# Separate opt-in boot experiment. Reuses the already compiled generated library.
if(NOT TARGET pocket_game_generated)
    message(FATAL_ERROR "Configure POCKET_GAME_GENERATED before adding the boot runner")
endif()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
add_executable(pocket-game-boot "${CMAKE_CURRENT_LIST_DIR}/game_boot.cpp")
target_compile_options(pocket-game-boot PRIVATE -fno-short-enums)
target_link_libraries(pocket-game-boot PRIVATE pocket_game_generated mhf_psp_runtime
    SceCtrl_stub SceLibKernel_stub SceProcessmgr_stub SceIofilemgr_stub
    SceSysmem_stub SceFiber_stub SceSysmodule_stub
    "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive")
vita_create_self(pocket-game-boot.self pocket-game-boot UNSAFE)
vita_create_vpk(pocket-game-boot.vpk PKBOOT001 pocket-game-boot.self
    VERSION "00.10" NAME "Pocket Boot Test")

# Pocket software display hook
 target_sources(pocket-game-boot PRIVATE "${CMAKE_CURRENT_LIST_DIR}/vita_present.cpp")
 target_link_libraries(pocket-game-boot PRIVATE SceDisplay_stub)

include("${CMAKE_CURRENT_LIST_DIR}/gpu_target.cmake")
