#include <aoe2/input/guest_keyboard.hpp>
#include <aoe2/input/guest_mouse.hpp>
#include <aoe2/input/input_routing.hpp>
#include <aoe2/input/surface_mapping.hpp>
#include <aoe2/input/touch_gestures.hpp>
#include <aoe2/input/usb_keyboard_rpc.hpp>
#include <aoe2/input/usb_mouse.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aoe2::input;

namespace {

int g_failures = 0;

// Unlike assert(), stays active in Release builds.
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ":" << __LINE__                                 \
                << ": CHECK failed: " << #condition << std::endl;              \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

class RecordingSink : public InputSink {
public:
  void SubmitEvent(const ActionEvent &event) override {
    events.push_back(event);
  }
  std::vector<ActionEvent> events;
};

// Switch handheld touchscreen onto the 640x480 PS2 framebuffer.
const Extent kSwitchScreen{1280.0f, 720.0f};
const Extent kGuestFramebuffer{640.0f, 480.0f};

TouchContact Touch(uint32_t id, float x, float y, TouchPhase phase,
                   uint64_t t) {
  return TouchContact{id, x, y, phase, t};
}

void TestMappingPillarboxesFourByThreeOnWidescreen() {
  const SurfaceMapping mapping(kSwitchScreen, kGuestFramebuffer);
  // 4:3 at 720p is 960x720 centered with 160px bars; scale 1.5.
  const GuestPoint center = mapping.ToGuest(640.0f, 360.0f);
  CHECK(Near(center.x, 320.0f) && Near(center.y, 240.0f));
  const GuestPoint left_bar = mapping.ToGuest(10.0f, 360.0f);
  CHECK(Near(left_bar.x, 0.0f));
  const GuestPoint bottom_right = mapping.ToGuest(1280.0f, 720.0f);
  CHECK(Near(bottom_right.x, 639.0f) && Near(bottom_right.y, 479.0f));
}

void TestMappingRejectsEmptyExtent() {
  bool threw = false;
  try {
    SurfaceMapping(Extent{0.0f, 720.0f}, kGuestFramebuffer);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

void TestTapIsPrimaryClick() {
  RecordingSink sink;
  TouchGestureTranslator translator(
      sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
  translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Began, 0));
  translator.Submit(Touch(1, 642.0f, 361.0f, TouchPhase::Ended, 100000));
  CHECK(sink.events.size() == 2);
  CHECK(sink.events[0].type == ActionType::PointerMove);
  CHECK(sink.events[1].type == ActionType::PrimaryClick);
}

void TestLongPressIsSecondaryClick() {
  RecordingSink sink;
  TouchGestureTranslator translator(
      sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
  translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Began, 0));
  translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Ended, 600000));
  CHECK(sink.events.size() == 2);
  CHECK(sink.events[1].type == ActionType::SecondaryClick);
}

void TestOneFingerDragIsPointerDrag() {
  RecordingSink sink;
  TouchGestureTranslator translator(
      sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
  translator.Submit(Touch(1, 400.0f, 300.0f, TouchPhase::Began, 0));
  translator.Submit(Touch(1, 500.0f, 400.0f, TouchPhase::Moved, 1000));
  translator.Submit(Touch(1, 600.0f, 450.0f, TouchPhase::Ended, 2000));
  CHECK(sink.events.size() == 3);
  CHECK(sink.events[0].type == ActionType::PointerDown);
  CHECK(sink.events[1].type == ActionType::PointerMove);
  CHECK(sink.events[2].type == ActionType::PointerUp);
}

void TestTwoFingerDragIsPanWithoutClicks() {
  RecordingSink sink;
  TouchGestureTranslator translator(
      sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
  translator.Submit(Touch(1, 500.0f, 300.0f, TouchPhase::Began, 0));
  translator.Submit(Touch(2, 700.0f, 300.0f, TouchPhase::Began, 10));
  translator.Submit(Touch(1, 530.0f, 300.0f, TouchPhase::Moved, 20));
  translator.Submit(Touch(2, 730.0f, 300.0f, TouchPhase::Moved, 20));
  translator.Submit(Touch(1, 530.0f, 300.0f, TouchPhase::Ended, 30));
  translator.Submit(Touch(2, 730.0f, 300.0f, TouchPhase::Ended, 30));
  CHECK(sink.events.size() == 2);
  float total_pan_x = 0.0f;
  for (const ActionEvent &event : sink.events) {
    CHECK(event.type == ActionType::Pan);
    total_pan_x += event.delta_x;
  }
  // 30 host px at scale 1.5 = 20 guest px of centroid motion.
  CHECK(Near(total_pan_x, 20.0f));
}

void TestCancelledDragReleasesButton() {
  RecordingSink sink;
  TouchGestureTranslator translator(
      sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
  translator.Submit(Touch(1, 400.0f, 300.0f, TouchPhase::Began, 0));
  translator.Submit(Touch(1, 500.0f, 400.0f, TouchPhase::Moved, 1000));
  translator.Submit(Touch(1, 500.0f, 400.0f, TouchPhase::Cancelled, 2000));
  CHECK(!sink.events.empty());
  CHECK(sink.events.back().type == ActionType::PointerUp);
}

void TestGuestMouseSeesTapAcrossTwoFrames() {
  GuestMouseAdapter mouse;
  mouse.SubmitEvent(
      ActionEvent{.type = ActionType::PrimaryClick, .x = 100.0f, .y = 50.0f});
  const GuestMouseState pressed = mouse.NextFrame();
  const GuestMouseState released = mouse.NextFrame();
  CHECK(pressed.left && Near(pressed.x, 100.0f) && Near(pressed.y, 50.0f));
  CHECK(!released.left);
  CHECK(!mouse.NextFrame().left);
}

void TestGuestMouseHoldsButtonDuringDrag() {
  GuestMouseAdapter mouse;
  mouse.SubmitEvent(
      ActionEvent{.type = ActionType::PointerDown, .x = 10.0f, .y = 10.0f});
  CHECK(mouse.NextFrame().left);
  mouse.SubmitEvent(
      ActionEvent{.type = ActionType::PointerMove, .x = 90.0f, .y = 70.0f});
  const GuestMouseState dragging = mouse.NextFrame();
  CHECK(dragging.left && Near(dragging.x, 90.0f));
  mouse.SubmitEvent(
      ActionEvent{.type = ActionType::PointerUp, .x = 90.0f, .y = 70.0f});
  CHECK(!mouse.NextFrame().left);
}

void TestGuestMouseLongPressIsRightButton() {
  GuestMouseAdapter mouse;
  mouse.SubmitEvent(
      ActionEvent{.type = ActionType::SecondaryClick, .x = 5.0f, .y = 5.0f});
  const GuestMouseState pressed = mouse.NextFrame();
  CHECK(pressed.right && !pressed.left);
  CHECK(!mouse.NextFrame().right);
}

HostMouseReport MouseReport(float dx, float dy, uint32_t buttons,
                            uint64_t t = 0) {
  return HostMouseReport{dx, dy, buttons, t};
}

void TestUsbMouseMotionMovesCursorWithinGuest() {
  RecordingSink sink;
  UsbMouseTranslator mouse(sink, kGuestFramebuffer, GuestPoint{320.0f, 240.0f},
                           MouseConfig{2.0f});
  mouse.Submit(MouseReport(10.0f, -5.0f, 0));
  CHECK(sink.events.size() == 1);
  CHECK(sink.events[0].type == ActionType::PointerMove);
  CHECK(Near(sink.events[0].x, 340.0f) && Near(sink.events[0].y, 230.0f));
  mouse.Submit(MouseReport(10000.0f, 10000.0f, 0));
  CHECK(Near(sink.events.back().x, 639.0f) &&
        Near(sink.events.back().y, 479.0f));
}

void TestUsbMouseIdleReportEmitsNothing() {
  RecordingSink sink;
  UsbMouseTranslator mouse(sink, kGuestFramebuffer, GuestPoint{});
  mouse.Submit(MouseReport(0.0f, 0.0f, 0));
  CHECK(sink.events.empty());
}

void TestUsbMouseLeftButtonDrags() {
  RecordingSink sink;
  UsbMouseTranslator mouse(sink, kGuestFramebuffer, GuestPoint{100.0f, 100.0f});
  mouse.Submit(MouseReport(0.0f, 0.0f, kMouseButtonLeft));
  mouse.Submit(MouseReport(5.0f, 0.0f, kMouseButtonLeft));
  mouse.Submit(MouseReport(0.0f, 0.0f, 0));
  CHECK(sink.events.size() == 3);
  CHECK(sink.events[0].type == ActionType::PointerDown);
  CHECK(sink.events[1].type == ActionType::PointerMove);
  CHECK(sink.events[2].type == ActionType::PointerUp);
  CHECK(Near(sink.events[2].x, 105.0f));
}

void TestUsbMouseRightButtonIsSecondaryClickOnPress() {
  RecordingSink sink;
  UsbMouseTranslator mouse(sink, kGuestFramebuffer, GuestPoint{50.0f, 60.0f});
  mouse.Submit(MouseReport(0.0f, 0.0f, kMouseButtonRight));
  mouse.Submit(MouseReport(0.0f, 0.0f, 0));
  CHECK(sink.events.size() == 1);
  CHECK(sink.events[0].type == ActionType::SecondaryClick);
  CHECK(Near(sink.events[0].x, 50.0f) && Near(sink.events[0].y, 60.0f));
}

void TestUsbMouseFeedsGuestMouseAdapter() {
  GuestMouseAdapter guest(GuestPoint{10.0f, 10.0f});
  UsbMouseTranslator mouse(guest, kGuestFramebuffer, GuestPoint{10.0f, 10.0f});
  mouse.Submit(MouseReport(0.0f, 0.0f, kMouseButtonLeft));
  mouse.Submit(MouseReport(0.0f, 0.0f, 0));
  CHECK(guest.NextFrame().left);
  CHECK(!guest.NextFrame().left);
}

void TestUsbMouseRejectsNonPositiveSensitivity() {
  RecordingSink sink;
  bool threw = false;
  try {
    UsbMouseTranslator(sink, kGuestFramebuffer, GuestPoint{},
                       MouseConfig{0.0f});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

constexpr uint8_t kUsageA = 0x04;
constexpr uint8_t kUsageB = 0x05;
constexpr uint8_t kUsageLeftShift = 0xE1;

void TestKeyboardReportsHeldKeysInPressOrder() {
  GuestKeyboardAdapter keyboard;
  keyboard.Submit(KeyEvent{kUsageB, true});
  keyboard.Submit(KeyEvent{kUsageA, true});
  keyboard.NextFrame();
  const GuestKeyboardState state = keyboard.NextFrame();
  CHECK(state.keys[0] == kUsageB && state.keys[1] == kUsageA &&
        state.keys[2] == 0);
  CHECK(state.modifiers == 0);
}

void TestKeyboardModifiersUseBitsNotSlots() {
  GuestKeyboardAdapter keyboard;
  keyboard.Submit(KeyEvent{kUsageLeftShift, true});
  const GuestKeyboardState state = keyboard.NextFrame();
  CHECK(state.modifiers == 0x02);
  CHECK(state.keys[0] == 0);
}

void TestKeyboardQuickTapVisibleForOnePoll() {
  GuestKeyboardAdapter keyboard;
  keyboard.Submit(KeyEvent{kUsageA, true});
  keyboard.Submit(KeyEvent{kUsageA, false});
  CHECK(keyboard.NextFrame().keys[0] == kUsageA);
  CHECK(keyboard.NextFrame().keys[0] == 0);
  CHECK(keyboard.NextFrame().keys[0] == 0);
}

void TestKeyboardSeventhKeyReportsRollOver() {
  GuestKeyboardAdapter keyboard;
  for (uint8_t usage = kUsageA; usage < kUsageA + 7; ++usage) {
    keyboard.Submit(KeyEvent{usage, true});
  }
  GuestKeyboardState last;
  for (int poll = 0; poll < 7; ++poll) {
    last = keyboard.NextFrame();
  }
  for (uint8_t key : last.keys) {
    CHECK(key == kUsageErrorRollOver);
  }
}

void TestKeyboardIgnoresRepeatsAndUnknownReleases() {
  GuestKeyboardAdapter keyboard;
  keyboard.Submit(KeyEvent{kUsageA, true});
  keyboard.Submit(KeyEvent{kUsageA, true});
  keyboard.Submit(KeyEvent{kUsageB, false});
  keyboard.Submit(KeyEvent{0, true});
  CHECK(keyboard.NextFrame().keys[0] == kUsageA);
  const GuestKeyboardState steady = keyboard.NextFrame();
  CHECK(steady.keys[0] == kUsageA && steady.keys[1] == 0);
}

void TestDiffPressedKeysReportsReleasesThenPresses() {
  std::bitset<256> previous;
  std::bitset<256> current;
  previous.set(kUsageA);
  current.set(kUsageB);
  const std::vector<KeyEvent> events = DiffPressedKeys(previous, current);
  CHECK(events.size() == 2);
  CHECK(events[0].usage == kUsageA && !events[0].pressed);
  CHECK(events[1].usage == kUsageB && events[1].pressed);
  CHECK(DiffPressedKeys(current, current).empty());
}

void TestRoutingHandheldUsesTouchDockedUsesUsb() {
  const InputSources handheld = SourcesFor(OperationMode::Handheld);
  CHECK(handheld.touchscreen && handheld.controller);
  CHECK(!handheld.usb_mouse && !handheld.usb_keyboard);
  const InputSources docked = SourcesFor(OperationMode::Docked);
  CHECK(!docked.touchscreen && docked.controller);
  CHECK(docked.usb_mouse && docked.usb_keyboard);
}

void TestMouseRpcInfoReportsOnePortWhenConnected() {
  UsbMouseRpcModel model;
  model.SetConnected(true);
  const UsbMouseRpcModel::Reply info = model.InfoReply();
  CHECK(info[0] == 1 && info[1] == 1 && info[2] == 1);
  model.SetConnected(false);
  CHECK(model.InfoReply()[2] == 0);
}

void TestMouseRpcReadDrainsOneEventThenReportsEmpty() {
  UsbMouseRpcModel model;
  model.SetConnected(true);
  model.Submit(HostMouseReport{3.0f, -2.0f, kMouseButtonLeft, 0, 1.0f});
  const UsbMouseRpcModel::Reply event = model.ReadReply();
  CHECK(event[0] == 4);
  CHECK(event[1] == 0x01);
  CHECK(static_cast<int8_t>(event[2]) == 3 &&
        static_cast<int8_t>(event[3]) == -2);
  CHECK(static_cast<int8_t>(event[4]) == 1);
  CHECK(model.ReadReply()[0] == 0);
}

void TestMouseRpcSplitsLargeMotionAcrossEvents() {
  UsbMouseRpcModel model;
  model.SetConnected(true);
  model.Submit(HostMouseReport{300.0f, 0.0f, 0});
  int total = 0;
  for (int read = 0; read < 4; ++read) {
    const UsbMouseRpcModel::Reply event = model.ReadReply();
    if (event[0] == 0) {
      break;
    }
    total += static_cast<int8_t>(event[2]);
  }
  CHECK(total == 300);
}

void TestMouseRpcButtonChangeWithoutMotionIsAnEvent() {
  UsbMouseRpcModel model;
  model.SetConnected(true);
  model.Submit(HostMouseReport{0.0f, 0.0f, kMouseButtonRight});
  const UsbMouseRpcModel::Reply event = model.ReadReply();
  CHECK(event[0] == 4 && event[1] == 0x02);
  model.Submit(HostMouseReport{0.0f, 0.0f, kMouseButtonRight});
  CHECK(model.ReadReply()[0] == 0);
}

void TestMouseRpcDisconnectedMouseHasNoEvents() {
  UsbMouseRpcModel model;
  model.Submit(HostMouseReport{5.0f, 5.0f, kMouseButtonLeft});
  CHECK(model.ReadReply()[0] == 0);
}

void TestKeyboardRpcReportsConnectionAndDeviceInfo() {
  UsbKeyboardRpcModel model;
  CHECK(model.InfoReply()[0] == 1 && model.InfoReply()[1] == 0);
  model.SetConnected(true);
  CHECK(model.InfoReply()[1] == 1 && model.InfoReply()[2] == 1);
  CHECK(model.DeviceInfoReply()[0] == 0);
}

void TestKeyboardRpcQueuesChangedBootReports() {
  UsbKeyboardRpcModel model;
  model.SetConnected(true);
  GuestKeyboardState report{};
  report.modifiers = 0x02;
  report.keys[0] = 0x04;
  model.SubmitReport(report);
  const auto reply = model.ReadReply();
  CHECK(reply[1] == 8 && reply[2] == 0x02 && reply[4] == 0x04);
  CHECK(reply[0] == 0);  // mask byte: documented zero approximation
  CHECK(model.ReadReply()[1] == 0);
  model.SubmitReport(report);
  CHECK(model.ReadReply()[1] == 0);
  report.keys[0] = 0;
  model.SubmitReport(report);
  CHECK(model.ReadReply()[1] == 8 && model.ReadReply()[1] == 0);
}

void TestKeyboardRpcCoalescesWhenQueueIsFull() {
  UsbKeyboardRpcModel model;
  model.SetConnected(true);
  for (uint8_t usage = 4; usage < 8; ++usage) {
    GuestKeyboardState report{};
    report.keys[0] = usage;
    model.SubmitReport(report);
  }
  CHECK(model.ReadReply()[4] == 4);
  CHECK(model.ReadReply()[4] == 5);
  CHECK(model.ReadReply()[4] == 7);
  CHECK(model.ReadReply()[1] == 0);
}

void TestKeyboardRpcStoresLedAndValidatesUnits() {
  UsbKeyboardRpcModel model;
  CHECK(model.SetLed(1, 0x02)[0] == 0xFF);
  CHECK(model.SetLed(0, 0x02)[0] == 0xFE);
  model.SetConnected(true);
  CHECK(model.SetLed(0, 0x05)[0] == 0);
  CHECK(model.ReadReply()[0] == 0x05);
  CHECK(model.SetCapsType(0, 3)[0] == 0);
  CHECK(model.SetLed(2, 0)[0] == 0xFF);
}

} // namespace

int main() {
  TestMappingPillarboxesFourByThreeOnWidescreen();
  TestMappingRejectsEmptyExtent();
  TestTapIsPrimaryClick();
  TestLongPressIsSecondaryClick();
  TestOneFingerDragIsPointerDrag();
  TestTwoFingerDragIsPanWithoutClicks();
  TestCancelledDragReleasesButton();
  TestGuestMouseSeesTapAcrossTwoFrames();
  TestGuestMouseHoldsButtonDuringDrag();
  TestGuestMouseLongPressIsRightButton();
  TestUsbMouseMotionMovesCursorWithinGuest();
  TestUsbMouseIdleReportEmitsNothing();
  TestUsbMouseLeftButtonDrags();
  TestUsbMouseRightButtonIsSecondaryClickOnPress();
  TestUsbMouseFeedsGuestMouseAdapter();
  TestUsbMouseRejectsNonPositiveSensitivity();
  TestKeyboardReportsHeldKeysInPressOrder();
  TestKeyboardModifiersUseBitsNotSlots();
  TestKeyboardQuickTapVisibleForOnePoll();
  TestKeyboardSeventhKeyReportsRollOver();
  TestKeyboardIgnoresRepeatsAndUnknownReleases();
  TestDiffPressedKeysReportsReleasesThenPresses();
  TestRoutingHandheldUsesTouchDockedUsesUsb();
  TestMouseRpcInfoReportsOnePortWhenConnected();
  TestMouseRpcReadDrainsOneEventThenReportsEmpty();
  TestMouseRpcSplitsLargeMotionAcrossEvents();
  TestMouseRpcButtonChangeWithoutMotionIsAnEvent();
  TestMouseRpcDisconnectedMouseHasNoEvents();
  TestKeyboardRpcReportsConnectionAndDeviceInfo();
  TestKeyboardRpcQueuesChangedBootReports();
  TestKeyboardRpcCoalescesWhenQueueIsFull();
  TestKeyboardRpcStoresLedAndValidatesUnits();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All input tests passed." << std::endl;
  return EXIT_SUCCESS;
}
