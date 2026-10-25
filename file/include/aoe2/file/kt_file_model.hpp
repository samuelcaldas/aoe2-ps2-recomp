#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>
#include <string_view>

namespace aoe2::file {

// Global CdServerState located at guest 0x004F1508.
struct CdServerState {
    uint32_t record_table{0};     // 0x00: pointer/address of sorted FileRecord array
    int32_t record_count{0};      // 0x04: number of records
    int32_t min_val{0x7FFFFFFF};  // 0x08: minimum value in table
    uint32_t entry_table{0};     // 0x0C: pointer/address of named entry table
    int32_t entry_count{0};      // 0x10: number of named entries
};

// Pure model of the KCET file table subsystem over guest memory.
class KtFileModel {
public:
    explicit KtFileModel(aoe2::runtime::GuestAccess& memory);

    // 0x0012CCE8: ktFileSetRecordTable
    uint32_t SetRecordTable(uint32_t table_addr);

    // 0x0012CDE0: ktFileSetEntryTable
    uint32_t SetEntryTable(uint32_t table_addr);

    // 0x0012CEA0: ktFileClassifyKind
    int32_t ClassifyKind(uint32_t arg_addr);

    // 0x0012CF00: ktFileGetHandlerByKind
    uint32_t GetHandlerByKind(int32_t kind);

    // 0x0012D128: ktFileBinarySearchRecord
    uint32_t BinarySearch(int32_t key, uint32_t table_addr, int32_t count);

    // Accessors for state
    uint32_t RecordTableAddress() const;
    int32_t RecordCount() const;
    int32_t MinVal() const;
    uint32_t EntryTableAddress() const;
    int32_t EntryCount() const;

private:
    aoe2::runtime::GuestAccess& memory_;
    static constexpr uint32_t kGlobalStateAddr = 0x004F1508;
};

}  // namespace aoe2::file
