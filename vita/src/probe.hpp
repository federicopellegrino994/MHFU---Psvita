#pragma once
#include <string>
#include <vector>

namespace pocket {
struct Report {
    bool fibers_ok = false;
    bool scheduler_ok = false;
    bool memory_ok = false;
    bool disc_recognized = false;
    std::vector<std::string> lines;
};
// Read-only inspection. Recognition is not executable/hash validation or game support.
Report inspect(const std::string &iso_path);
}
