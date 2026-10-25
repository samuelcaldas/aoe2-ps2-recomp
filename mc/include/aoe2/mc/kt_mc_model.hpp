#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>

namespace aoe2::mc {

struct KtMcCard {
    int32_t port{0};
    int32_t slot{0};
    int32_t type{0};
    int32_t free_clusters{0};
    int32_t flags{0};
};

class KtMcModel {
public:
    explicit KtMcModel(aoe2::runtime::GuestAccess& memory);

    void InitWork();
    void TermWork();
    uint32_t TableLookup(int32_t key, uint32_t table_addr, int32_t count);
    int32_t Init2(uint32_t card_buf_addr, int32_t n_card);
    void SetActiveCard(int32_t card_index);

    int32_t GetCardPort(uint32_t card_ptr) const;
    int32_t GetCardSlot(uint32_t card_ptr) const;
    int32_t GetCardType(uint32_t card_ptr) const;

    void SetFileAttr(uint32_t file_ptr, int32_t attr);
    void SetFileCluster(uint32_t file_ptr, int32_t cluster);
    void ClearEntryFields(uint32_t entry_ptr);

private:
    aoe2::runtime::GuestAccess& memory_;
    int32_t active_card_{0};
    static constexpr int32_t kMaxCards = 2;
    static constexpr uint32_t kMcWorkAddr = 0x004E2CB8;
};

}  // namespace aoe2::mc
