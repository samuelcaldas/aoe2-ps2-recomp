#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoe2::cdvd {

// CdStreamStatus at guest address 0x004F16C0:
// 0x00: field0 (int32)
// 0x04: active (int32)
// 0x08: bytes_remaining (int32, >> 11 = sector count)
struct CdStreamStatus {
    int32_t field0{0};
    int32_t active{0};
    int32_t bytes_remaining{0};
};

class CdvdModel {
public:
    explicit CdvdModel(aoe2::runtime::GuestAccess& memory);

    /// Set the directory containing aoe2.iso and reset; returns true without opening it.
    bool Initialize(std::string_view disc_root);
    /// Discard any pending request and clear stream status; returns zero.
    int32_t Reset();

    /// Queue sectors at lba into physical EE RAM; zero sectors is a no-op. Returns zero.
    /// Throws logic_error if busy, length_error for signed byte overflow, out_of_range for invalid RAM.
    int32_t QueueRead(uint32_t lba, uint32_t sector_count, uint32_t dest_guest_addr);
    /// Copy a complete image read, then publish completion. Throws ios_base::failure on I/O errors;
    /// destination, pending request and completion status stay unchanged on those failures. Retry or Reset explicitly.
    void ProcessPendingRequests();

    // Stream status queries
    bool IsActive() const;
    int32_t RemainingBytes() const;
    int32_t RemainingSectors() const;
    void UpdateGuestStatus();

private:
    aoe2::runtime::GuestAccess& memory_;
    std::string disc_root_;
    bool active_{false};
    uint32_t pending_lba_{0};
    uint32_t pending_sectors_{0};
    uint32_t pending_dest_{0};
    int32_t bytes_remaining_{0};

    static constexpr uint32_t kStreamStatusAddr = 0x004F16C0;
    static constexpr uint32_t kSectorSize = 2048;
};

}  // namespace aoe2::cdvd
