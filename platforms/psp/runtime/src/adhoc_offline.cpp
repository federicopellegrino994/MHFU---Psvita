// Explicit offline transport. Never creates host sockets or reports a connection.
// Built only with MHF_PSP_OFFLINE; the normal desktop transport is unchanged.
#include "adhoc_link_aemu.hpp"
#include "adhoc_sockets.hpp"
#include <utility>

namespace mhf::psp::adhoc {
AemuLink::AemuLink(Config config) : config_(std::move(config)) {}
AemuLink::~AemuLink() = default;
bool AemuLink::resolve_ipv4(const std::string &, std::uint32_t &) { return false; }
bool AemuLink::split_address(const std::string &, std::string &, std::uint16_t &) { return false; }
void AemuLink::request_connect(const GroupName &) {
    std::lock_guard lock(mutex_);
    events_.push_back(LinkEvent{}); // Error, never Connect
}
void AemuLink::request_disconnect() {}
void AemuLink::request_scan() {
    std::lock_guard lock(mutex_);
    events_.push_back(LinkEvent{});
}
std::vector<LinkEvent> AemuLink::take_events() {
    std::lock_guard lock(mutex_);
    std::vector<LinkEvent> result;
    result.swap(events_);
    return result;
}
Sockets::Sockets(Config config) : config_(std::move(config)) {}
Sockets::~Sockets() = default;
std::int64_t Sockets::pdp_create(std::uint16_t) { return as_status(sockerr::kNotConnected); }
std::uint32_t Sockets::pdp_delete(int) { return sockerr::kInvalidSocketId; }
std::uint32_t Sockets::pdp_send(int, const Mac &, std::uint16_t, const std::uint8_t *, std::uint32_t) { return sockerr::kInvalidSocketId; }
std::uint32_t Sockets::pdp_recv(int, Mac &, std::uint16_t &, std::uint8_t *, std::uint32_t &) { return sockerr::kInvalidSocketId; }
std::int64_t Sockets::ptp_open(std::uint16_t, const Mac &, std::uint16_t) { return as_status(sockerr::kNotConnected); }
std::int64_t Sockets::ptp_listen(std::uint16_t) { return as_status(sockerr::kNotConnected); }
std::uint32_t Sockets::ptp_connect(int) { return sockerr::kInvalidSocketId; }
std::int64_t Sockets::ptp_accept(int, Mac &, std::uint16_t &) { return as_status(sockerr::kInvalidSocketId); }
std::uint32_t Sockets::ptp_send(int, const std::uint8_t *, std::uint32_t &) { return sockerr::kInvalidSocketId; }
std::uint32_t Sockets::ptp_recv(int, std::uint8_t *, std::uint32_t &) { return sockerr::kInvalidSocketId; }
std::uint32_t Sockets::ptp_flush(int) { return sockerr::kInvalidSocketId; }
std::uint32_t Sockets::ptp_close(int) { return sockerr::kInvalidSocketId; }
std::vector<PtpStat> Sockets::ptp_stats() { return {}; }
void Sockets::close_all() {}
bool Sockets::empty() const { return true; }
} // namespace mhf::psp::adhoc
