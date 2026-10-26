#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

struct SyntheticCdvdDisc {
    std::filesystem::path root;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(4 * 2048);

    SyntheticCdvdDisc() {
        root = std::filesystem::temp_directory_path() /
            ("aoe2-disc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("create synthetic disc directory");
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<uint8_t>(1 + (index / 2048) * 37 + index % 251);
        std::ofstream file(root / "aoe2.iso", std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    ~SyntheticCdvdDisc() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    SyntheticCdvdDisc(const SyntheticCdvdDisc&) = delete;
    SyntheticCdvdDisc& operator=(const SyntheticCdvdDisc&) = delete;
};
