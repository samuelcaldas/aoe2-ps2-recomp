// Pure helpers of kt_libpad (origin: decomp src/kcet/kt_libpad.c).
#include <aoe2/pad/kt_libpad_model.hpp>

namespace aoe2::pad {

// func_00127D88
int ScaleValue(int value, int scale, int min_value, int max_value) {
    int difference = value - min_value;
    if (difference < 0) difference = 0;
    const int range = max_value - min_value;
    if (range == 0) return scale - 1;  // the original traps here
    int result = (difference * scale) / range;
    if (result >= scale) result = scale - 1;
    return result;
}

// func_00127E40
int ScaleClamp(int value, int min_value, int max_value) {
    return ScaleValue(value, 0x100, min_value, max_value) & 0xFF;
}

// func_00127DC8
int ScaleWithThreshold(int value, int low, int high, int center) {
    const int threshold = center < 0x80 ? center : 0x100 - center;
    if (high >= threshold) high = threshold - 1;
    const bool below = value < center;
    const int distance = below ? center - (value + 1) : value - center;
    const int scaled = ScaleValue(distance, 0x80, low, high);
    return (below ? -scaled : scaled) & 0xFF;
}

// func_00127D30
int ScaleByMode(int value, int mode, int a, int b, int c) {
    if (mode == 1) return ScaleWithThreshold(value, a, b, c);
    if (mode == 2) return ScaleClamp(value, a, b);
    return value & 0xFF;
}

// func_00128868
bool IsPressureModeId(int id) { return (id ^ 0x79) < 1; }

// func_00127C78
bool IsExtendedMode(int value) { return (value ^ 4) != 0; }

// func_001262E8: (port, slot) pairs for every port whose slot count exceeds `start`.
int EnumeratePorts(const int* slots_per_port, int start, uint16_t* out_pairs, int max_items, int port_count) {
    int count = 0;
    for (int port = 0; port < port_count; ++port) {
        for (int slot = start; slot < slots_per_port[port]; ++slot) {
            if (count >= max_items) return count;
            out_pairs[count * 2] = static_cast<uint16_t>(port);
            out_pairs[count * 2 + 1] = static_cast<uint16_t>(slot);
            ++count;
        }
    }
    return count;
}

}  // namespace aoe2::pad
