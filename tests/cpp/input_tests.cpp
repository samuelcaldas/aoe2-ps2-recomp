#include <aoe2/input/guest_mouse.hpp>
#include <aoe2/input/surface_mapping.hpp>
#include <aoe2/input/touch_gestures.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aoe2::input;

namespace {

int g_failures = 0;

// Unlike assert(), stays active in Release builds.
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: "      \
                      << #condition << std::endl;                               \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

class RecordingSink : public InputSink {
public:
    void SubmitEvent(const ActionEvent& event) override { events.push_back(event); }
    std::vector<ActionEvent> events;
};

// Switch handheld touchscreen onto the 640x480 PS2 framebuffer.
const Extent kSwitchScreen{1280.0f, 720.0f};
const Extent kGuestFramebuffer{640.0f, 480.0f};

TouchContact Touch(uint32_t id, float x, float y, TouchPhase phase, uint64_t t) {
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
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void TestTapIsPrimaryClick() {
    RecordingSink sink;
    TouchGestureTranslator translator(sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
    translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Began, 0));
    translator.Submit(Touch(1, 642.0f, 361.0f, TouchPhase::Ended, 100000));
    CHECK(sink.events.size() == 2);
    CHECK(sink.events[0].type == ActionType::PointerMove);
    CHECK(sink.events[1].type == ActionType::PrimaryClick);
}

void TestLongPressIsSecondaryClick() {
    RecordingSink sink;
    TouchGestureTranslator translator(sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
    translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Began, 0));
    translator.Submit(Touch(1, 640.0f, 360.0f, TouchPhase::Ended, 600000));
    CHECK(sink.events.size() == 2);
    CHECK(sink.events[1].type == ActionType::SecondaryClick);
}

void TestOneFingerDragIsPointerDrag() {
    RecordingSink sink;
    TouchGestureTranslator translator(sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
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
    TouchGestureTranslator translator(sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
    translator.Submit(Touch(1, 500.0f, 300.0f, TouchPhase::Began, 0));
    translator.Submit(Touch(2, 700.0f, 300.0f, TouchPhase::Began, 10));
    translator.Submit(Touch(1, 530.0f, 300.0f, TouchPhase::Moved, 20));
    translator.Submit(Touch(2, 730.0f, 300.0f, TouchPhase::Moved, 20));
    translator.Submit(Touch(1, 530.0f, 300.0f, TouchPhase::Ended, 30));
    translator.Submit(Touch(2, 730.0f, 300.0f, TouchPhase::Ended, 30));
    CHECK(sink.events.size() == 2);
    float total_pan_x = 0.0f;
    for (const ActionEvent& event : sink.events) {
        CHECK(event.type == ActionType::Pan);
        total_pan_x += event.delta_x;
    }
    // 30 host px at scale 1.5 = 20 guest px of centroid motion.
    CHECK(Near(total_pan_x, 20.0f));
}

void TestCancelledDragReleasesButton() {
    RecordingSink sink;
    TouchGestureTranslator translator(sink, SurfaceMapping(kSwitchScreen, kGuestFramebuffer));
    translator.Submit(Touch(1, 400.0f, 300.0f, TouchPhase::Began, 0));
    translator.Submit(Touch(1, 500.0f, 400.0f, TouchPhase::Moved, 1000));
    translator.Submit(Touch(1, 500.0f, 400.0f, TouchPhase::Cancelled, 2000));
    CHECK(!sink.events.empty());
    CHECK(sink.events.back().type == ActionType::PointerUp);
}

void TestGuestMouseSeesTapAcrossTwoFrames() {
    GuestMouseAdapter mouse;
    mouse.SubmitEvent(ActionEvent{.type = ActionType::PrimaryClick, .x = 100.0f, .y = 50.0f});
    const GuestMouseState pressed = mouse.NextFrame();
    const GuestMouseState released = mouse.NextFrame();
    CHECK(pressed.left && Near(pressed.x, 100.0f) && Near(pressed.y, 50.0f));
    CHECK(!released.left);
    CHECK(!mouse.NextFrame().left);
}

void TestGuestMouseHoldsButtonDuringDrag() {
    GuestMouseAdapter mouse;
    mouse.SubmitEvent(ActionEvent{.type = ActionType::PointerDown, .x = 10.0f, .y = 10.0f});
    CHECK(mouse.NextFrame().left);
    mouse.SubmitEvent(ActionEvent{.type = ActionType::PointerMove, .x = 90.0f, .y = 70.0f});
    const GuestMouseState dragging = mouse.NextFrame();
    CHECK(dragging.left && Near(dragging.x, 90.0f));
    mouse.SubmitEvent(ActionEvent{.type = ActionType::PointerUp, .x = 90.0f, .y = 70.0f});
    CHECK(!mouse.NextFrame().left);
}

void TestGuestMouseLongPressIsRightButton() {
    GuestMouseAdapter mouse;
    mouse.SubmitEvent(ActionEvent{.type = ActionType::SecondaryClick, .x = 5.0f, .y = 5.0f});
    const GuestMouseState pressed = mouse.NextFrame();
    CHECK(pressed.right && !pressed.left);
    CHECK(!mouse.NextFrame().right);
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
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "All input tests passed." << std::endl;
    return EXIT_SUCCESS;
}
