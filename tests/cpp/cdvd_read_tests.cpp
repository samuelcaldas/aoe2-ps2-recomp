#include <aoe2/cdvd/cdvd_model.hpp>
#include "cdvd_test_memory.hpp"
#include "synthetic_cdvd_disc.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>

namespace {
using aoe2::cdvd::CdvdModel;
constexpr uint32_t DESTINATION = 0x200000, STATUS = 0x4f16c0;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Action> void expect_failure(Action action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected request failure was not raised");
}
std::array<uint32_t, 3> status(CdvdTestMemory& memory) {
    return {memory.Load32(STATUS), memory.Load32(STATUS + 4), memory.Load32(STATUS + 8)};
}
void reads_exact_single_and_multiple_sectors() {
    SyntheticCdvdDisc disc;
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize(disc.root.string());
    for (uint32_t count : {1u, 2u, 3u}) {
        check(model.QueueRead(1, count, DESTINATION) == 0, "valid queue result");
        model.ProcessPendingRequests();
        check(std::equal(disc.bytes.begin() + 2048, disc.bytes.begin() + 2048 * (1 + count),
                         memory.ram.begin() + DESTINATION), "wrong sector bytes");
        check(!model.IsActive() && model.RemainingBytes() == 0, "read not completed");
        check(status(memory) == std::array<uint32_t, 3>{0, 0, 0}, "guest completion not published");
    }
}
void reads_last_sector_without_crossing_eof() {
    SyntheticCdvdDisc disc;
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize(disc.root.string());
    model.QueueRead(3, 1, 0); model.ProcessPendingRequests();
    check(std::equal(disc.bytes.begin() + 6144, disc.bytes.end(), memory.ram.begin()), "last sector or address zero unread");
    check(memory.ram[2048] == 0xa5 && !model.IsActive(), "last sector overrun");
}
void failed_read_is_transactional(const std::function<void(SyntheticCdvdDisc&)>& change, uint32_t lba, uint32_t count) {
    SyntheticCdvdDisc disc;
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize(disc.root.string());
    model.QueueRead(lba, count, DESTINATION);
    const auto before_status = status(memory);
    const auto before_bytes = memory.ram;
    change(disc);
    expect_failure<std::ios_base::failure>([&] { model.ProcessPendingRequests(); });
    check(memory.ram == before_bytes && status(memory) == before_status, "failed read modified guest memory");
    check(model.IsActive() && model.RemainingBytes() == int32_t(count * 2048), "failed read published completion");
    expect_failure<std::logic_error>([&] { model.QueueRead(0, 1, DESTINATION); });
    check(model.Reset() == 0 && !model.IsActive(), "explicit reset failed");
}
void invalid_reads_leave_destination_unchanged() {
    failed_read_is_transactional([](auto& disc) { std::filesystem::remove(disc.root / "aoe2.iso"); }, 0, 1);
    failed_read_is_transactional([](auto& disc) {
        std::filesystem::remove(disc.root / "aoe2.iso");
        std::filesystem::create_directory(disc.root / "aoe2.iso");
    }, 0, 1);
    failed_read_is_transactional([](auto& disc) {
        std::filesystem::permissions(disc.root / "aoe2.iso", std::filesystem::perms::none);
    }, 0, 1);
    failed_read_is_transactional([](auto&) {}, 4, 1);
    failed_read_is_transactional([](auto&) {}, 3, 2);
    failed_read_is_transactional([](auto& disc) { std::filesystem::resize_file(disc.root / "aoe2.iso", 8191); }, 3, 1);
    failed_read_is_transactional([](auto& disc) { std::filesystem::resize_file(disc.root / "aoe2.iso", 2048); }, 1, 2);
    failed_read_is_transactional([](auto&) {}, std::numeric_limits<uint32_t>::max(), 2);
}
void invalid_guest_ranges_are_rejected_before_io() {
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize("");
    const auto before = memory.ram;
    for (uint32_t destination : {0x1fffff0u, 0x2000000u, 0xfffffff0u})
        expect_failure<std::out_of_range>([&] { model.QueueRead(0, 1, destination); });
    for (uint32_t count : {1048576u, 2097152u, 0xffffffffu})
        expect_failure<std::length_error>([&] { model.QueueRead(0, count, DESTINATION); });
    check(memory.ram == before && !model.IsActive(), "invalid queue changed state");
}
void failed_request_can_retry_without_requeue() {
    SyntheticCdvdDisc disc;
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize(disc.root.string());
    model.QueueRead(3, 1, 0x1fff800);
    std::filesystem::rename(disc.root / "aoe2.iso", disc.root / "saved.iso");
    expect_failure<std::ios_base::failure>([&] { model.ProcessPendingRequests(); });
    std::filesystem::rename(disc.root / "saved.iso", disc.root / "aoe2.iso");
    model.ProcessPendingRequests();
    check(std::equal(disc.bytes.begin() + 6144, disc.bytes.end(), memory.ram.begin() + 0x1fff800), "retry lost pending request");
    check(!model.IsActive(), "retry did not complete");
    const auto completed = memory.ram;
    model.ProcessPendingRequests();
    check(memory.ram == completed, "repeated processing changed completed state");
}
void zero_and_busy_requests_preserve_pending_read() {
    SyntheticCdvdDisc disc;
    CdvdTestMemory memory; CdvdModel model(memory); model.Initialize(disc.root.string());
    check(model.QueueRead(0xffffffff, 0, 0xffffffff) == 0 && !model.IsActive(), "zero count is not a no-op");
    model.QueueRead(2, 1, DESTINATION);
    const auto before = memory.ram;
    check(model.QueueRead(0, 0, 0) == 0, "zero count failed while pending");
    expect_failure<std::logic_error>([&] { model.QueueRead(0, 1, DESTINATION + 2048); });
    check(memory.ram == before && model.RemainingBytes() == 2048, "new request clobbered pending read");
    model.ProcessPendingRequests();
    check(std::equal(disc.bytes.begin() + 4096, disc.bytes.begin() + 6144,
                     memory.ram.begin() + DESTINATION), "pending read was replaced");
    check(memory.ram[DESTINATION + 2048] == 0xa5, "rejected second read wrote bytes");
}
}
void run_cdvd_read_tests() {
    unsigned failures = 0;
    for (auto test : {reads_exact_single_and_multiple_sectors, reads_last_sector_without_crossing_eof,
                      invalid_reads_leave_destination_unchanged, invalid_guest_ranges_are_rejected_before_io,
                      zero_and_busy_requests_preserve_pending_read, failed_request_can_retry_without_requeue}) {
        try { test(); } catch (const std::runtime_error& error) {
            ++failures; std::cerr << "FAIL cdvd read: " << error.what() << '\n';
        }
    }
    check(failures == 0, "CDVD read regressions failed");
}
