#include <aoe2/mc/native_host_mc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace aoe2::mc {

namespace {

// POSIX / PS2 file flags
constexpr uint32_t PS2_FIO_O_RDONLY = 0x0001;
constexpr uint32_t PS2_FIO_O_WRONLY = 0x0002;
constexpr uint32_t PS2_FIO_O_RDWR   = 0x0003;
constexpr uint32_t PS2_FIO_O_NBLOCK = 0x0010;
constexpr uint32_t PS2_FIO_O_APPEND = 0x0100;
constexpr uint32_t PS2_FIO_O_CREAT  = 0x0200;
constexpr uint32_t PS2_FIO_O_TRUNC  = 0x0400;
constexpr uint32_t PS2_FIO_O_EXCL   = 0x0800;

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    return parts;
}

std::string JoinPath(const std::vector<std::string>& parts) {
    if (parts.empty()) return "/";
    std::string res;
    for (const auto& p : parts) {
        res.push_back('/');
        res.append(p);
    }
    return res;
}

bool WildcardMatch(const char* pattern, const char* str) {
    if (!pattern || !str) return false;
    if (std::strcmp(pattern, "*") == 0 || std::strcmp(pattern, "*.*") == 0) return true;

    while (*pattern && *str) {
        if (*pattern == '*') {
            while (*pattern == '*') ++pattern;
            if (!*pattern) return true;
            while (*str) {
                if (WildcardMatch(pattern, str)) return true;
                ++str;
            }
            return false;
        }
        if (*pattern == '?' || *pattern == *str) {
            ++pattern;
            ++str;
        } else {
            return false;
        }
    }
    while (*pattern == '*') ++pattern;
    return !*pattern && !*str;
}

void SystemTimeToMcDateTime(std::filesystem::file_time_type ftime, SceMcStDateTime& out) {
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &cftime);
#else
    localtime_r(&cftime, &tm_buf);
#endif
    out.Sec = static_cast<uint8_t>(tm_buf.tm_sec);
    out.Min = static_cast<uint8_t>(tm_buf.tm_min);
    out.Hour = static_cast<uint8_t>(tm_buf.tm_hour);
    out.Day = static_cast<uint8_t>(tm_buf.tm_mday);
    out.Month = static_cast<uint8_t>(tm_buf.tm_mon + 1);
    out.Year = static_cast<uint16_t>(tm_buf.tm_year + 1900);
}

}  // namespace

NativeHostMemoryCard& NativeHostMemoryCard::Instance() {
    static NativeHostMemoryCard instance;
    return instance;
}

NativeHostMemoryCard::NativeHostMemoryCard() {
    Reset();
}

void NativeHostMemoryCard::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [fd, file] : files_) {
        if (file.handle) {
            std::fclose(file.handle);
        }
    }
    files_.clear();
    next_fd_ = 1;
    last_cmd_ = 0;
    last_result_ = 0;
    command_pending_ = false;

    for (size_t i = 0; i < ports_.size(); ++i) {
        ports_[i].current_dir = "/";
        ports_[i].formatted = true;
        ports_[i].root_path = std::filesystem::path("mc" + std::to_string(i));
    }
}

void NativeHostMemoryCard::SetRootDirectory(int32_t port, const std::filesystem::path& root_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port >= 0 && port < static_cast<int32_t>(ports_.size())) {
        ports_[port].root_path = root_path;
        std::error_code ec;
        std::filesystem::create_directories(root_path, ec);
    }
}

std::filesystem::path NativeHostMemoryCard::GetRootDirectory(int32_t port) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port >= 0 && port < static_cast<int32_t>(ports_.size())) {
        return ports_[port].root_path;
    }
    return {};
}

void NativeHostMemoryCard::SetCommandResult(int32_t cmd, int32_t result) {
    last_cmd_ = cmd;
    last_result_ = result;
    command_pending_ = true;
}

std::string NativeHostMemoryCard::NormalizeGuestPath(int32_t port, std::string path) const {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.rfind("mc0:", 0) == 0 || path.rfind("mc1:", 0) == 0) {
        path = path.substr(4);
    }
    const bool absolute = !path.empty() && path.front() == '/';
    std::vector<std::string> parts;
    if (!absolute && port >= 0 && port < static_cast<int32_t>(ports_.size())) {
        parts = SplitPath(ports_[port].current_dir);
    }
    for (const auto& part : SplitPath(path)) {
        if (part == "." || part.empty()) continue;
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(part);
    }
    return JoinPath(parts);
}

std::filesystem::path NativeHostMemoryCard::ResolveHostPath(int32_t port, const std::string& guest_path) const {
    if (port < 0 || port >= static_cast<int32_t>(ports_.size())) return {};
    std::filesystem::path root = ports_[port].root_path;
    const std::string normalized = NormalizeGuestPath(port, guest_path);
    if (normalized.size() > 1) {
        root /= normalized.substr(1);
    }
    return root.lexically_normal();
}

void NativeHostMemoryCard::SceMcGetInfo(int32_t port, int32_t slot, uint32_t type_ptr, uint32_t free_ptr,
                                        uint32_t formatted_ptr, aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdGetInfo, kMcResultNoEntry);
        return;
    }

    const auto& p = ports_[port];
    std::error_code ec;
    std::filesystem::create_directories(p.root_path, ec);

    if (type_ptr != 0) memory.Store32(type_ptr, kMcTypePs2);
    if (free_ptr != 0) memory.Store32(free_ptr, kMcFreeClusters);
    if (formatted_ptr != 0) memory.Store32(formatted_ptr, p.formatted ? kMcFormatted : 0);

    SetCommandResult(kMcCmdGetInfo, p.formatted ? kMcFormatted : 0);
}

void NativeHostMemoryCard::SceMcOpen(int32_t port, int32_t slot, const std::string& path, uint32_t flags,
                                    aoe2::runtime::GuestAccess& /*memory*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdOpen, kMcResultNoEntry);
        return;
    }

    const std::filesystem::path host_path = ResolveHostPath(port, path);
    std::error_code ec;
    const bool exists = std::filesystem::exists(host_path, ec) && !ec;
    const bool create = (flags & PS2_FIO_O_CREAT) != 0;

    if (!exists && !create) {
        SetCommandResult(kMcCmdOpen, kMcResultNoEntry);
        return;
    }

    const auto parent = host_path.parent_path();
    if (!std::filesystem::exists(parent, ec) || ec) {
        SetCommandResult(kMcCmdOpen, kMcResultNoEntry);
        return;
    }

    const char* mode = "rb";
    if (flags & PS2_FIO_O_WRONLY) {
        if (create) {
            mode = (flags & PS2_FIO_O_TRUNC) ? "wb" : "ab";
        } else {
            mode = (flags & PS2_FIO_O_TRUNC) ? "wb" : "r+b";
        }
    } else if (flags & PS2_FIO_O_RDWR) {
        if (create) {
            mode = (flags & PS2_FIO_O_TRUNC) ? "w+b" : "a+b";
        } else {
            mode = (flags & PS2_FIO_O_TRUNC) ? "w+b" : "r+b";
        }
    }

    FILE* fp = std::fopen(host_path.string().c_str(), mode);
    if (!fp && create) {
        fp = std::fopen(host_path.string().c_str(), "w+b");
    }

    if (!fp) {
        SetCommandResult(kMcCmdOpen, exists ? kMcResultDeniedPermit : kMcResultNoEntry);
        return;
    }

    const int32_t fd = next_fd_++;
    files_[fd] = OpenFile{fd, port, host_path, fp};
    SetCommandResult(kMcCmdOpen, fd);
}

void NativeHostMemoryCard::SceMcClose(int32_t fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end()) {
        SetCommandResult(kMcCmdClose, kMcResultNoEntry);
        return;
    }
    if (it->second.handle) {
        std::fclose(it->second.handle);
    }
    files_.erase(it);
    SetCommandResult(kMcCmdClose, kMcResultSucceed);
}

void NativeHostMemoryCard::SceMcSeek(int32_t fd, int32_t offset, int32_t whence) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end() || !it->second.handle) {
        SetCommandResult(kMcCmdSeek, kMcResultNoEntry);
        return;
    }

    int host_whence = SEEK_SET;
    if (whence == 1) host_whence = SEEK_CUR;
    else if (whence == 2) host_whence = SEEK_END;

    if (std::fseek(it->second.handle, offset, host_whence) != 0) {
        SetCommandResult(kMcCmdSeek, -1);
        return;
    }
    const long cur = std::ftell(it->second.handle);
    SetCommandResult(kMcCmdSeek, static_cast<int32_t>(cur));
}

void NativeHostMemoryCard::SceMcRead(int32_t fd, uint32_t buffer_guest_addr, int32_t size,
                                    aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end() || !it->second.handle || size < 0) {
        SetCommandResult(kMcCmdRead, kMcResultNoEntry);
        return;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    const size_t bytes_read = std::fread(buf.data(), 1, buf.size(), it->second.handle);
    if (bytes_read > 0) {
        memory.WriteBytes(buffer_guest_addr, buf.data(), bytes_read);
    }
    SetCommandResult(kMcCmdRead, static_cast<int32_t>(bytes_read));
}

void NativeHostMemoryCard::SceMcWrite(int32_t fd, uint32_t buffer_guest_addr, int32_t size,
                                     aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end() || !it->second.handle || size < 0) {
        SetCommandResult(kMcCmdWrite, kMcResultNoEntry);
        return;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    memory.ReadBytes(buffer_guest_addr, buf.data(), size);
    const size_t written = std::fwrite(buf.data(), 1, buf.size(), it->second.handle);
    std::fflush(it->second.handle);
    SetCommandResult(kMcCmdWrite, static_cast<int32_t>(written));
}

void NativeHostMemoryCard::SceMcFlush(int32_t fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end() || !it->second.handle) {
        SetCommandResult(kMcCmdFlush, kMcResultNoEntry);
        return;
    }
    std::fflush(it->second.handle);
    SetCommandResult(kMcCmdFlush, kMcResultSucceed);
}

void NativeHostMemoryCard::SceMcMkdir(int32_t port, int32_t slot, const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdMkdir, kMcResultNoEntry);
        return;
    }
    const std::filesystem::path host_path = ResolveHostPath(port, path);
    std::error_code ec;
    if (std::filesystem::exists(host_path, ec) && !ec) {
        SetCommandResult(kMcCmdMkdir, std::filesystem::is_directory(host_path, ec) ? kMcResultSucceed : kMcResultDeniedPermit);
        return;
    }
    if (std::filesystem::create_directory(host_path, ec) && !ec) {
        SetCommandResult(kMcCmdMkdir, kMcResultSucceed);
    } else {
        SetCommandResult(kMcCmdMkdir, kMcResultDeniedPermit);
    }
}

void NativeHostMemoryCard::SceMcChdir(int32_t port, int32_t slot, const std::string& path, uint32_t current_dir_ptr,
                                     aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdChdir, kMcResultNoEntry);
        return;
    }
    const std::string normalized = NormalizeGuestPath(port, path);
    const std::filesystem::path host_path = ResolveHostPath(port, normalized);
    std::error_code ec;
    if (std::filesystem::exists(host_path, ec) && !ec && std::filesystem::is_directory(host_path, ec)) {
        ports_[port].current_dir = normalized;
        if (current_dir_ptr != 0) {
            memory.WriteBytes(current_dir_ptr, reinterpret_cast<const uint8_t*>(normalized.c_str()), normalized.size() + 1);
        }
        SetCommandResult(kMcCmdChdir, kMcResultSucceed);
    } else {
        SetCommandResult(kMcCmdChdir, kMcResultNoEntry);
    }
}

void NativeHostMemoryCard::SceMcGetDir(int32_t port, int32_t slot, const std::string& path, uint32_t /*mode*/,
                                      int32_t max_entries, uint32_t table_guest_addr,
                                      aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0 || max_entries <= 0) {
        SetCommandResult(kMcCmdGetDir, kMcResultNoEntry);
        return;
    }

    const std::string normalized = NormalizeGuestPath(port, path);
    std::filesystem::path query_path = normalized.size() > 1 ? normalized.substr(1) : "";
    std::string pattern = "*";
    std::filesystem::path parent_rel;

    if (!query_path.empty()) {
        const std::string fname = query_path.filename().string();
        if (fname.find('*') != std::string::npos || fname.find('?') != std::string::npos) {
            pattern = fname;
            parent_rel = query_path.parent_path();
        } else {
            parent_rel = query_path;
        }
    }

    std::filesystem::path dir_to_scan = ports_[port].root_path;
    if (!parent_rel.empty()) {
        dir_to_scan /= parent_rel;
    }
    dir_to_scan = dir_to_scan.lexically_normal();

    std::error_code ec;
    if (!std::filesystem::exists(dir_to_scan, ec) || ec || !std::filesystem::is_directory(dir_to_scan, ec)) {
        SetCommandResult(kMcCmdGetDir, 0);
        return;
    }

    int32_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_to_scan, ec)) {
        if (count >= max_entries) break;
        const std::string filename = entry.path().filename().string();
        if (!WildcardMatch(pattern.c_str(), filename.c_str())) continue;

        SceMcTblGetDir row{};
        std::strncpy(row.EntryName, filename.c_str(), sizeof(row.EntryName) - 1);

        const bool is_dir = entry.is_directory(ec);
        row.AttrFile = kMcAttrReadable | kMcAttrWriteable | kMcAttrExists;
        row.AttrFile |= (is_dir ? kMcAttrSubdir : kMcAttrFile);

        if (!is_dir) {
            row.FileSizeByte = static_cast<uint32_t>(entry.file_size(ec));
        }
        SystemTimeToMcDateTime(entry.last_write_time(ec), row._Modify);
        row._Create = row._Modify;

        const uint32_t dest_addr = table_guest_addr + count * sizeof(SceMcTblGetDir);
        memory.WriteBytes(dest_addr, reinterpret_cast<const uint8_t*>(&row), sizeof(row));
        ++count;
    }

    SetCommandResult(kMcCmdGetDir, count);
}

void NativeHostMemoryCard::SceMcSetFileInfo(int32_t port, int32_t slot, const std::string& path,
                                           uint32_t /*info_guest_addr*/, uint32_t /*flags*/,
                                           aoe2::runtime::GuestAccess& /*memory*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdSetFileInfo, kMcResultNoEntry);
        return;
    }
    const std::filesystem::path host_path = ResolveHostPath(port, path);
    std::error_code ec;
    if (std::filesystem::exists(host_path, ec) && !ec) {
        SetCommandResult(kMcCmdSetFileInfo, kMcResultSucceed);
    } else {
        SetCommandResult(kMcCmdSetFileInfo, kMcResultNoEntry);
    }
}

void NativeHostMemoryCard::SceMcDelete(int32_t port, int32_t slot, const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdDelete, kMcResultNoEntry);
        return;
    }
    const std::filesystem::path host_path = ResolveHostPath(port, path);
    std::error_code ec;
    if (!std::filesystem::exists(host_path, ec) || ec) {
        SetCommandResult(kMcCmdDelete, kMcResultNoEntry);
        return;
    }
    if (std::filesystem::is_directory(host_path, ec) && !std::filesystem::is_empty(host_path, ec)) {
        SetCommandResult(kMcCmdDelete, kMcResultNotEmpty);
        return;
    }
    if (std::filesystem::remove(host_path, ec) && !ec) {
        SetCommandResult(kMcCmdDelete, kMcResultSucceed);
    } else {
        SetCommandResult(kMcCmdDelete, kMcResultDeniedPermit);
    }
}

void NativeHostMemoryCard::SceMcFormat(int32_t port, int32_t slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdFormat, kMcResultNoEntry);
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(ports_[port].root_path, ec);
    std::filesystem::create_directories(ports_[port].root_path, ec);
    ports_[port].current_dir = "/";
    ports_[port].formatted = true;
    SetCommandResult(kMcCmdFormat, kMcResultSucceed);
}

void NativeHostMemoryCard::SceMcUnformat(int32_t port, int32_t slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdUnformat, kMcResultNoEntry);
        return;
    }
    ports_[port].formatted = false;
    SetCommandResult(kMcCmdUnformat, kMcResultSucceed);
}

void NativeHostMemoryCard::SceMcGetEntSpace(int32_t port, int32_t slot, const std::string& /*path*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdGetEntSpace, kMcResultNoEntry);
        return;
    }
    SetCommandResult(kMcCmdGetEntSpace, 1024);
}

void NativeHostMemoryCard::SceMcRename(int32_t port, int32_t slot, const std::string& old_path,
                                      const std::string& new_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= static_cast<int32_t>(ports_.size()) || slot != 0) {
        SetCommandResult(kMcCmdRename, kMcResultNoEntry);
        return;
    }
    const std::filesystem::path src = ResolveHostPath(port, old_path);
    const std::filesystem::path dst = ResolveHostPath(port, new_path);
    std::error_code ec;
    std::filesystem::rename(src, dst, ec);
    if (!ec) {
        SetCommandResult(kMcCmdRename, kMcResultSucceed);
    } else {
        SetCommandResult(kMcCmdRename, kMcResultDeniedPermit);
    }
}

int32_t NativeHostMemoryCard::SceMcSync(int32_t /*mode*/, uint32_t cmd_ptr, uint32_t result_ptr,
                                       aoe2::runtime::GuestAccess& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int32_t cmd = last_cmd_;
    const int32_t res = last_result_;
    const bool was_pending = command_pending_;
    command_pending_ = false;

    if (cmd_ptr != 0) memory.Store32(cmd_ptr, static_cast<uint32_t>(cmd));
    if (result_ptr != 0) memory.Store32(result_ptr, static_cast<uint32_t>(res));

    return was_pending ? 1 : -1;
}

}  // namespace aoe2::mc
