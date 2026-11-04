#pragma once
#include "mhf/psp/runtime.hpp"
// Install before the first GE command. The context belongs exclusively to the GE worker.
void pocket_install_gpu(mhf::psp::Runtime &runtime);
