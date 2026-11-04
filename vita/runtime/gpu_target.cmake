# Optional hardware experiment. Default remains the known software configuration.
option(POCKET_VITA_GPU "Experimental vitaGL GE backend" OFF)
if(POCKET_VITA_GPU)
    set(_gpu_libs vitaGL mathneon vitashark SceShaccCgExt zip z)
    foreach(_lib IN LISTS _gpu_libs)
        find_library(POCKET_GPU_${_lib} NAMES ${_lib}
            PATHS "$ENV{VITASDK}/arm-vita-eabi/lib" NO_DEFAULT_PATH)
        if(NOT POCKET_GPU_${_lib})
            message(FATAL_ERROR "GPU prerequisite missing: lib${_lib}.a. Run python3 pocket-gpu-addon/check_gpu_deps.py")
        endif()
    endforeach()
    if(NOT EXISTS "$ENV{VITASDK}/arm-vita-eabi/include/vitaGL.h")
        message(FATAL_ERROR "GPU prerequisite missing: vitaGL.h")
    endif()
    target_include_directories(pocket-game-boot PRIVATE "${R}/platforms/psp/gpu")
    target_sources(pocket-game-boot PRIVATE "${CMAKE_CURRENT_LIST_DIR}/vita_gpu.cpp")
    target_compile_definitions(pocket-game-boot PRIVATE POCKET_VITA_GPU=1)
    # Order follows the official vitaGL immediate-mode sample. No suppressed ABI warnings.
    target_link_libraries(pocket-game-boot PRIVATE
        "-Wl,--start-group"
        ${POCKET_GPU_vitaGL} ${POCKET_GPU_mathneon}
        SceLibKernel_stub SceAppMgr_stub SceAppUtil_stub SceCommonDialog_stub
        SceGxm_stub SceDisplay_stub SceSysmodule_stub
        ${POCKET_GPU_zip} ${POCKET_GPU_z} ${POCKET_GPU_vitashark}
        ${POCKET_GPU_SceShaccCgExt} taihen_stub SceShaccCg_stub
        SceKernelDmacMgr_stub SceCtrl_stub m
        "-Wl,--end-group")
endif()
