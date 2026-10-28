#pragma once
// Identity of the SLP shapes the game draws: the loader names each shape object,
// and draws are counted by (shape identity, frame) for periodic reports.
#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace aoe2::game_hooks {

class ShapeTraceModel {
public:
    void Loaded(uint32_t shape, const std::string& name, int32_t resource_id) {
        names_[shape] = name + "#" + std::to_string(resource_id);
    }

    void Drawn(uint32_t shape, int32_t frame) {
        ++counts_[{Identity(shape), frame}];
        ++draws_;
    }

    // Busiest `limit` (identity, frame) pairs as "identity/frame:count", then starts a new window.
    std::string Report(std::size_t limit) {
        std::vector<std::pair<std::pair<std::string, int32_t>, uint64_t>> rows(counts_.begin(), counts_.end());
        std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::ostringstream out;
        for (std::size_t i = 0; i < rows.size() && i < limit; ++i)
            out << (i == 0 ? "" : " ") << rows[i].first.first << "/" << rows[i].first.second << ":" << rows[i].second;
        counts_.clear();
        return out.str();
    }

    uint64_t draws() const { return draws_; }

private:
    std::string Identity(uint32_t shape) const {
        const auto found = names_.find(shape);
        if (found != names_.end()) return found->second;
        std::ostringstream out;
        out << "@0x" << std::hex << shape;
        return out.str();
    }

    std::map<uint32_t, std::string> names_;
    std::map<std::pair<std::string, int32_t>, uint64_t> counts_;
    uint64_t draws_ = 0;
};

} // namespace aoe2::game_hooks
