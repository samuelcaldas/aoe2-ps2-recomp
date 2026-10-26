#include <aoe2/gpu/kt_gpu_model.hpp>
#include <array>
#include <stdexcept>

namespace aoe2::gpu {
uint64_t KtGpuModel::Load64(uint32_t p) {
    const auto low = m.Load32(p);
    return low | (uint64_t(m.Load32(p + 4)) << 32);
}
void KtGpuModel::Store64(uint32_t p, uint64_t value) {
    m.Store32(p, uint32_t(value));
    m.Store32(p + 4, uint32_t(value >> 32));
}
void KtGpuModel::Tag(uint32_t p) {
    std::array<uint8_t, 16> tag{};
    m.ReadBytes(0x38c730, tag.data(), tag.size());
    const auto cursor = m.Load32(p);
    m.WriteBytes(cursor, tag.data(), tag.size());
    m.Store32(p, cursor + 16);
    m.Store32(p + 12, cursor);
}
void KtGpuModel::Register(uint32_t p, uint32_t reg, uint64_t value) {
    const auto cursor = m.Load32(p);
    Store64(cursor, value);
    Store64(cursor + 8, reg);
    m.Store32(p, cursor + 16);
}
void KtGpuModel::Set(uint32_t p, uint32_t data, uint32_t base, bool selector) {
    Tag(p);
    const auto reg = base + (selector ? ((m.Load32(data) >> 13) & 1) : 0);
    const auto value = Load64(data + 8);
    Register(p, reg, value);
}
void KtGpuModel::SetDrawRegisters(uint32_t p, uint32_t data) {
    Tag(p);
    const auto value = m.Load32(data + 4);
    Register(p, 0x1c, value);
}
void KtGpuModel::ResetTestRegister(uint32_t p) {
    Tag(p);
    Register(p, 0x3f, 0);
}
uint8_t KtGpuModel::ValidateCommand(uint32_t command) {
    const auto kind = m.Load8(command);
    if (kind >= 29) {
        m.ReportAssertion("kt_gpu.c", 630, "kind < 29");
        throw std::runtime_error("native GPU: invalid command kind");
    }
    return kind;
}
uint32_t KtGpuModel::Handler(uint8_t kind) {
    if (kind >= 29) throw std::runtime_error("native GPU: invalid handler index");
    return m.Load32(0x38c740 + uint32_t(kind) * 4);
}
void KtGpuModel::Begin(uint32_t p) {
    auto cursor = m.Load32(p);
    const auto previous = m.Load32(p + 8);
    while (cursor & 12) {
        m.Store32(cursor, 0);
        cursor += 4;
    }
    if (previous) {
        const auto count = m.Load32(previous);
        const auto delta = uint32_t((int32_t(cursor - previous) >> 4) - 1);
        m.Store32(previous, count + delta);
    }
    m.Store32(p + 8, 0);
    m.Store32(p, cursor);
    const auto start = m.Load32(p);
    m.Store32(p + 8, cursor);
    m.Store32(start, 0x10000000);
    m.Store32(start + 8, 0);
    m.Store32(p, start + 16);
    m.Store32(start + 12, 0);
    m.Store32(start + 4, 0);
}
void KtGpuModel::Finalize(uint32_t p) {
    const auto tag = m.Load32(p + 12);
    auto cursor = m.Load32(p);
    const auto value = Load64(tag);
    uint32_t count = uint32_t((int32_t(cursor - tag) >> 3) - 2);
    const auto format = (value >> 58) & 3;
    if (format != 1) count >>= 1;
    if (format != 2) {
        auto registers = uint32_t(value >> 60);
        if (!registers) registers = 16;
        count = (count + registers - 1) / registers;
    }
    m.Store32(p + 12, 0);
    Store64(tag, value + count);
    while (cursor & 12) {
        m.Store32(cursor, 0);
        cursor += 4;
    }
    m.Store32(p, cursor);
}
}
