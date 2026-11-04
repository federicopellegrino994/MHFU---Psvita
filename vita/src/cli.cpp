#include "probe.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: pocket-probe path/to/game.iso\n"; return 2; }
    const auto report = pocket::inspect(argv[1]);
    for (const auto &line : report.lines) std::cout << line << '\n';
    return report.scheduler_ok && report.fibers_ok && report.memory_ok && report.disc_recognized ? 0 : 1;
}
