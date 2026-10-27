// Implementation of shared MouseRpc and KeyboardRpc services that route through
// the host device models while allowing direct calls from native modules over GuestAccess.
#include <aoe2/game_hooks/native_input.hpp>
#include <aoe2/input/usb_keyboard_rpc.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include "guest_rdram_access.hpp"
#include "native_input_internal.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

namespace aoe2::game_hooks {

namespace {

using namespace aoe2::game_hooks::detail;

std::atomic<bool> g_native_mouse_installed{false};
std::atomic<bool> g_native_keyboard_installed{false};

thread_local uint8_t* tl_mouse_rdram = nullptr;
thread_local uint8_t* tl_keyboard_rdram = nullptr;

class SharedMouseRpcService final : public aoe2::mouse::MouseRpc {
public:
    int Info(uint32_t reply) override {
        uint8_t* rdram = tl_mouse_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_mouse_mutex);
        SampleHostMouse(rdram);
        const auto rep = g_mouse_model.InfoReply();
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }

    int Read(int port, uint32_t reply) override {
        uint8_t* rdram = tl_mouse_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_mouse_mutex);
        const bool first_port = (port == 0);
        const auto rep = first_port ? g_mouse_model.ReadReply() : aoe2::input::UsbMouseRpcModel::Reply{};
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }

    int DeviceInfo(int /*port*/, uint32_t reply) override {
        uint8_t* rdram = tl_mouse_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_mouse_mutex);
        const auto rep = g_mouse_model.DeviceInfoReply();
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }
};

class SharedKeyboardRpcService final : public aoe2::keyboard::KeyboardRpc {
public:
    int Info(uint32_t reply) override {
        uint8_t* rdram = tl_keyboard_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_keyboard_mutex);
        SampleHostKeyboard();
        const auto rep = g_keyboard_model.InfoReply();
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }

    int Read(int port, uint32_t reply) override {
        uint8_t* rdram = tl_keyboard_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_keyboard_mutex);
        const bool first_port = (port == 0);
        const auto rep = first_port ? g_keyboard_model.ReadReply() : aoe2::input::UsbKeyboardRpcModel::Reply{};
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }

    int DeviceInfo(int /*port*/, uint32_t reply) override {
        uint8_t* rdram = tl_keyboard_rdram;
        if (!rdram) return -1;
        uint8_t* dest = getMemPtr(rdram, reply);
        if (!dest) return -1;
        std::lock_guard<std::mutex> lock(g_keyboard_mutex);
        const auto rep = g_keyboard_model.DeviceInfoReply();
        std::memcpy(dest, rep.data(), rep.size());
        return 0;
    }

    int SetLed(int port, int mask) override {
        std::lock_guard<std::mutex> lock(g_keyboard_mutex);
        const auto rep = g_keyboard_model.SetLed(static_cast<uint8_t>(port), static_cast<uint8_t>(mask));
        return rep[0];
    }

    int SetCapsType(int port, int type) override {
        std::lock_guard<std::mutex> lock(g_keyboard_mutex);
        const auto rep = g_keyboard_model.SetCapsType(static_cast<uint8_t>(port), static_cast<uint8_t>(type));
        return rep[0];
    }
};

SharedMouseRpcService g_shared_mouse_rpc;
SharedKeyboardRpcService g_shared_keyboard_rpc;

}  // namespace

aoe2::mouse::MouseRpc& EnsureMouseRpc() {
    return g_shared_mouse_rpc;
}

aoe2::keyboard::KeyboardRpc& EnsureKeyboardRpc() {
    return g_shared_keyboard_rpc;
}

void BindMouseRpcMemory(uint8_t* rdram) {
    tl_mouse_rdram = rdram;
}

void BindKeyboardRpcMemory(uint8_t* rdram) {
    tl_keyboard_rdram = rdram;
}

bool NativeMouseModuleInstalled() {
    return g_native_mouse_installed.load(std::memory_order_relaxed);
}

bool NativeKeyboardModuleInstalled() {
    return g_native_keyboard_installed.load(std::memory_order_relaxed);
}

void SetNativeMouseModuleInstalled(bool installed) {
    g_native_mouse_installed.store(installed, std::memory_order_relaxed);
}

void SetNativeKeyboardModuleInstalled(bool installed) {
    g_native_keyboard_installed.store(installed, std::memory_order_relaxed);
}

}  // namespace aoe2::game_hooks
