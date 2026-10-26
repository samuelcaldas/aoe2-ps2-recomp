#include <aoe2/cdvd/cdvd_model.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aoe2::cdvd {
namespace {
constexpr uint64_t GUEST_RAM_SIZE = 32 * 1024 * 1024;

void validate_request(uint64_t bytes, uint32_t destination) {
    if (bytes > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        throw std::length_error("CDVD read exceeds signed stream status capacity");
    if (uint64_t(destination) + bytes > GUEST_RAM_SIZE)
        throw std::out_of_range("CDVD destination exceeds physical EE RAM");
}

std::ifstream open_image(std::string_view root) {
    if (root.empty()) throw std::ios_base::failure("CDVD disc root is empty");
    const auto path = std::filesystem::path(root) / "aoe2.iso";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        throw std::ios_base::failure("CDVD image is not a regular file: " + path.string());
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::ios_base::failure("Cannot open CDVD image: " + path.string());
    return file;
}

std::vector<uint8_t> read_exact(std::string_view root, uint64_t offset, std::size_t bytes) {
    auto file = open_image(root);
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (!file || size < 0 || offset + bytes > static_cast<uint64_t>(size))
        throw std::ios_base::failure("CDVD read exceeds image size");
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) throw std::ios_base::failure("CDVD image seek failed");
    std::vector<uint8_t> buffer(bytes);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bytes));
    if (!file || file.gcount() != static_cast<std::streamsize>(bytes))
        throw std::ios_base::failure("CDVD image read was incomplete");
    return buffer;
}
}

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
    if (sector_count == 0) return 0;
    if (active_) throw std::logic_error("CDVD read already pending; process or reset it first");
    const uint64_t bytes = uint64_t(sector_count) * kSectorSize;
    validate_request(bytes, dest_guest_addr);
    pending_lba_ = lba;
    pending_sectors_ = sector_count;
    pending_dest_ = dest_guest_addr;
    bytes_remaining_ = static_cast<int32_t>(bytes);
    active_ = true;
    UpdateGuestStatus();
    return 0;
}

void CdvdModel::ProcessPendingRequests() {
    if (!active_) return;
    const auto buffer = read_exact(disc_root_, uint64_t(pending_lba_) * kSectorSize,
                                   static_cast<std::size_t>(bytes_remaining_));
    memory_.WriteBytes(pending_dest_, buffer.data(), buffer.size());
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
