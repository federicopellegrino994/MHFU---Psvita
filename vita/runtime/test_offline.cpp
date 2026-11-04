#include "adhoc_link_aemu.hpp"
#include "adhoc_sockets.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mhf::psp::adhoc;
static void require(bool result) { if (!result) throw std::runtime_error("offline contract failed"); }
int main() {
    AemuLink link({});
    require(!link.online());
    GroupName group{};
    link.request_connect(group);
    link.request_scan();
    const auto events = link.take_events();
    require(events.size() == 2 && events[0].kind == LinkEvent::Kind::Error && events[1].kind == LinkEvent::Kind::Error);
    require(link.take_events().empty());
    Sockets sockets({});
    Mac mac{};
    std::uint16_t port = 1234;
    std::uint32_t length = 4;
    std::uint8_t buffer[]{1,2,3,4};
    require(sockets.pdp_create(port) == as_status(sockerr::kNotConnected));
    require(sockets.ptp_open(port, mac, port) < 0 && sockets.ptp_listen(port) < 0);
    require(sockets.pdp_recv(1, mac, port, buffer, length) == sockerr::kInvalidSocketId);
    require(length == 4 && buffer[0] == 1 && port == 1234);
    require(sockets.ptp_accept(1, mac, port) < 0);
    require(sockets.ptp_stats().empty() && sockets.empty());
    sockets.close_all();
    require(sockets.empty() && !link.online());
    std::puts("PASS: offline transport rejects connections and preserves receive buffers");
}
