#include <aoe2/mc/kt_mc_model.hpp>

namespace aoe2::mc {

KtMcModel::KtMcModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

void KtMcModel::InitWork() {
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        memory_.Store32(kMcWorkAddr + offset, 0);
    }
}

void KtMcModel::TermWork() {
    InitWork();
}

uint32_t KtMcModel::TableLookup(int32_t key, uint32_t table_addr, int32_t count) {
    if (table_addr == 0 || count <= 0) return 0;
    for (int32_t i = 0; i < count; ++i) {
        const int32_t entry_key = static_cast<int32_t>(memory_.Load32(table_addr + i * 8));
        if (entry_key == key) {
            return memory_.Load32(table_addr + i * 8 + 4);
        }
    }
    return 0;
}

int32_t KtMcModel::Init2(uint32_t card_buf_addr, int32_t n_card) {
    if (n_card > kMaxCards) {
        memory_.ReportAssertion("kt_mc_2.c", 0x490078, "n_card <= KT_MC_CARD_MAX");
    }
    if (card_buf_addr != 0 && n_card > 0) {
        const int32_t limit = (n_card < kMaxCards) ? n_card : kMaxCards;
        for (int32_t i = 0; i < limit; ++i) {
            const uint32_t card_addr = card_buf_addr + i * 20;
            memory_.Store32(card_addr + 0, static_cast<uint32_t>(i));  // port
            memory_.Store32(card_addr + 4, 0);                         // slot
            memory_.Store32(card_addr + 8, 0);                         // type
            memory_.Store32(card_addr + 12, 0);                        // free_clusters
            memory_.Store32(card_addr + 16, 0);                        // flags
        }
    }
    return 0;
}

void KtMcModel::SetActiveCard(int32_t card_index) {
    active_card_ = card_index;
}

int32_t KtMcModel::GetCardPort(uint32_t card_ptr) const {
    if (card_ptr == 0) return 1;
    return static_cast<int32_t>(memory_.Load32(card_ptr + 0));
}

int32_t KtMcModel::GetCardSlot(uint32_t card_ptr) const {
    if (card_ptr == 0) return 0;
    return static_cast<int32_t>(memory_.Load32(card_ptr + 4));
}

int32_t KtMcModel::GetCardType(uint32_t card_ptr) const {
    if (card_ptr == 0) return 0;
    return static_cast<int32_t>(memory_.Load32(card_ptr + 8));
}

void KtMcModel::SetFileAttr(uint32_t file_ptr, int32_t attr) {
    if (file_ptr == 0) return;
    memory_.Store32(file_ptr + 0x18, static_cast<uint32_t>(attr));
}

void KtMcModel::SetFileCluster(uint32_t file_ptr, int32_t cluster) {
    if (file_ptr == 0) return;
    memory_.Store32(file_ptr + 0x1C, static_cast<uint32_t>(cluster));
}

void KtMcModel::ClearEntryFields(uint32_t entry_ptr) {
    if (entry_ptr == 0) return;
    memory_.Store32(entry_ptr + 0, 0);
    memory_.Store32(entry_ptr + 8, 0);
    memory_.Store32(entry_ptr + 12, 0);
}

}  // namespace aoe2::mc
