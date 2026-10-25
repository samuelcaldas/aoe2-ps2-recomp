#include <aoe2/hd/kt_hd_model.hpp>

namespace aoe2::hd {

KtHdModel::KtHdModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

int32_t KtHdModel::TableLookup(uint32_t table_addr, int32_t count, int32_t key) {
    if (table_addr == 0 || count <= 0) return 0;
    for (int32_t i = 0; i < count; ++i) {
        const int32_t entry_key = static_cast<int32_t>(memory_.Load32(table_addr + i * 8));
        if (entry_key == key) {
            return static_cast<int32_t>(memory_.Load32(table_addr + i * 8 + 4));
        }
    }
    return 0;
}

int32_t KtHdModel::SetFlag(int32_t flag) {
    if (flag == 0 || flag == 1) {
        memory_.Store32(kFlagAddr, static_cast<uint32_t>(flag));
    }
    return static_cast<int32_t>(memory_.Load32(kFlagAddr));
}

int32_t KtHdModel::GetFlag() const {
    return static_cast<int32_t>(memory_.Load32(kFlagAddr));
}

int32_t KtHdModel::ReadAndClearRegister() {
    const int32_t val = static_cast<int32_t>(memory_.Load32(kRegAddr));
    memory_.Store32(kRegAddr, 0);
    return val;
}

void KtHdModel::SetHostname(std::string_view name) {
    hostname_ = std::string(name);
}

int32_t KtHdModel::ExecuteCallback(uint32_t func_ptr, uint32_t /*arg_ptr*/) {
    if (func_ptr == 0) return 0;
    return 0;
}

}  // namespace aoe2::hd
