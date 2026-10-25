#include <aoe2/file/kt_file_model.hpp>

namespace aoe2::file {

KtFileModel::KtFileModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

uint32_t KtFileModel::SetRecordTable(uint32_t table_addr) {
    uint32_t old_table = 0;
    int32_t count = 0;
    int32_t min_val = 0x7FFFFFFF;

    if (table_addr != 0) {
        old_table = memory_.Load32(kGlobalStateAddr);
        if (memory_.Load16(table_addr) != 0) {
            do {
                count++;
            } while (memory_.Load16(table_addr + (count << 3)) != 0);
        }
        if (count > 0) {
            for (int32_t i = 0; i < count; ++i) {
                const uint32_t record_addr = table_addr + i * 8;
                const uint32_t raw_word = memory_.Load32(record_addr);
                const int32_t val = static_cast<int32_t>((raw_word >> 16) & 0xFFFFFF);
                if (val < min_val) {
                    min_val = val;
                }
            }
        }
    }

    memory_.Store32(kGlobalStateAddr + 0, table_addr);
    memory_.Store32(kGlobalStateAddr + 4, static_cast<uint32_t>(count));
    memory_.Store32(kGlobalStateAddr + 8, static_cast<uint32_t>(min_val));
    return old_table;
}

uint32_t KtFileModel::SetEntryTable(uint32_t table_addr) {
    uint32_t old_table = 0;
    int32_t count = 0;
    uint32_t offset = 0;

    if (table_addr != 0) {
        old_table = memory_.Load32(kGlobalStateAddr + 12);
        while (memory_.Load8(table_addr + offset) != 0) {
            count++;
            const uint8_t entry_len = memory_.Load8(table_addr + offset);
            offset += (entry_len > 0) ? entry_len : 1;
        }
    }

    memory_.Store32(kGlobalStateAddr + 12, table_addr);
    memory_.Store32(kGlobalStateAddr + 16, static_cast<uint32_t>(count));
    return old_table;
}

int32_t KtFileModel::ClassifyKind(uint32_t arg_addr) {
    if (arg_addr == 0) {
        return 3;
    }
    const uint16_t val = memory_.Load16(arg_addr);
    if ((val & 0x0007) != 0) {
        return 0;
    }
    if (val != 0x0040) {
        return 2;
    }
    return 1;
}

uint32_t KtFileModel::GetHandlerByKind(int32_t kind) {
    if (kind < 0 || kind >= 4) {
        memory_.ReportAssertion("kt_file.c", 0x16A, "kind < 4");
        return 0;
    }
    // D_38C7F8 dispatch table in guest RDRAM
    constexpr uint32_t kHandlerTable = 0x0038C7F8;
    return memory_.Load32(kHandlerTable + kind * 4);
}

uint32_t KtFileModel::BinarySearch(int32_t key, uint32_t table_addr, int32_t count) {
    if (table_addr == 0 || count <= 0) {
        return 0;
    }
    int32_t low = 0;
    int32_t high = count;
    const int32_t target_key = key & 0x7FFF;

    while (low < high) {
        const int32_t mid = (low + high) / 2;
        const uint32_t entry_addr = table_addr + mid * 8;
        const int32_t entry_id = static_cast<int32_t>(memory_.Load16(entry_addr) & 0x7FFF);

        if (target_key < entry_id) {
            high = mid;
        } else if (entry_id >= target_key) {
            return entry_addr;
        } else {
            low = mid + 1;
        }
    }
    return 0;
}

uint32_t KtFileModel::RecordTableAddress() const {
    return memory_.Load32(kGlobalStateAddr + 0);
}

int32_t KtFileModel::RecordCount() const {
    return static_cast<int32_t>(memory_.Load32(kGlobalStateAddr + 4));
}

int32_t KtFileModel::MinVal() const {
    return static_cast<int32_t>(memory_.Load32(kGlobalStateAddr + 8));
}

uint32_t KtFileModel::EntryTableAddress() const {
    return memory_.Load32(kGlobalStateAddr + 12);
}

int32_t KtFileModel::EntryCount() const {
    return static_cast<int32_t>(memory_.Load32(kGlobalStateAddr + 16));
}

}  // namespace aoe2::file
