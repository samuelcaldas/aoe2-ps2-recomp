#include <aoe2/cdvd/cdvd_model.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace aoe2::cdvd {

CdvdModel::CdvdModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

bool CdvdModel::Initialize(std::string_view disc_root) {
    disc_root_ = std::string(disc_root);
    Reset();
    return true;
}

int32_t CdvdModel::Reset() {
    active_ = false;
    pending_lba_ = 0;
    pending_sectors_ = 0;
    pending_dest_ = 0;
    bytes_remaining_ = 0;
    UpdateGuestStatus();
    return 0;
}

int32_t CdvdModel::QueueRead(uint32_t lba, uint32_t sector_count, uint32_t dest_guest_addr) {
    pending_lba_ = lba;
    pending_sectors_ = sector_count;
    pending_dest_ = dest_guest_addr;
    bytes_remaining_ = static_cast<int32_t>(sector_count * kSectorSize);
    active_ = (sector_count > 0);
    UpdateGuestStatus();
    return 0;
}

void CdvdModel::ProcessPendingRequests() {
    if (!active_ || pending_sectors_ == 0 || pending_dest_ == 0) {
        return;
    }

    const std::size_t total_bytes = pending_sectors_ * kSectorSize;
    std::vector<uint8_t> buffer(total_bytes, 0);

    // If disc image or files are available in disc_root, read from host file
    if (!disc_root_.empty()) {
        const std::filesystem::path iso_path = std::filesystem::path(disc_root_) / "aoe2.iso";
        if (std::filesystem::exists(iso_path)) {
            std::ifstream file(iso_path, std::ios::binary);
            if (file) {
                file.seekg(static_cast<std::streamoff>(pending_lba_) * kSectorSize);
                file.read(reinterpret_cast<char*>(buffer.data()), total_bytes);
            }
        }
    }

    memory_.WriteBytes(pending_dest_, buffer.data(), total_bytes);
    active_ = false;
    bytes_remaining_ = 0;
    pending_sectors_ = 0;
    UpdateGuestStatus();
}

bool CdvdModel::IsActive() const {
    return active_;
}

int32_t CdvdModel::RemainingBytes() const {
    return bytes_remaining_;
}

int32_t CdvdModel::RemainingSectors() const {
    return bytes_remaining_ >> 11;
}

void CdvdModel::UpdateGuestStatus() {
    memory_.Store32(kStreamStatusAddr + 0, 0);
    memory_.Store32(kStreamStatusAddr + 4, active_ ? 1 : 0);
    memory_.Store32(kStreamStatusAddr + 8, static_cast<uint32_t>(bytes_remaining_));
}

}  // namespace aoe2::cdvd
