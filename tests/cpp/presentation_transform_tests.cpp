#include <aoe2/input/presentation_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

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

// PAL presentation: the game's 640x448 screen fills a 640x512 GS frame.
const Extent kFrame{640.0f, 512.0f};
const Extent kLogical{640.0f, 448.0f};
const FrameRect kLogicalInFrame{0.0f, 0.0f, 640.0f, 512.0f};

PresentationTransform At(float width, float height) {
  return PresentationTransform({width, height}, kFrame, kLogical, kLogicalInFrame);
}

void TestFrameDestinationAt1080p() {
  const FrameRect destination = At(1920.0f, 1080.0f).FrameDestination();
  CHECK(Near(destination.x, 285.0f) && Near(destination.y, 0.0f));
  CHECK(Near(destination.width, 1350.0f) && Near(destination.height, 1080.0f));
}

void TestToLogicalAt1080p() {
  const PresentationTransform transform = At(1920.0f, 1080.0f);
  const GuestPoint origin = transform.ToLogical(285.0f, 0.0f);
  CHECK(Near(origin.x, 0.0f) && Near(origin.y, 0.0f));
  const GuestPoint center = transform.ToLogical(960.0f, 540.0f);
  CHECK(Near(center.x, 320.0f) && Near(center.y, 224.0f));
  const GuestPoint pillar = transform.ToLogical(10.0f, 1079.0f);
  CHECK(Near(pillar.x, 0.0f) && Near(pillar.y, 447.0f));
}

void TestToWindowAt720p() {
  const PresentationTransform transform = At(1280.0f, 720.0f);
  const GuestPoint origin = transform.ToWindow(0.0f, 0.0f);
  CHECK(Near(origin.x, 190.0f) && Near(origin.y, 0.0f));
  const GuestPoint corner = transform.ToWindow(640.0f, 448.0f);
  CHECK(Near(corner.x, 1090.0f) && Near(corner.y, 720.0f));
}

void TestNativeWindowKeepsLogicalCenter() {
  const PresentationTransform transform = At(640.0f, 448.0f);
  const GuestPoint center = transform.ToWindow(320.0f, 224.0f);
  CHECK(Near(center.x, 320.0f) && Near(center.y, 224.0f));
  const GuestPoint origin = transform.ToLogical(40.0f, 0.0f);
  CHECK(Near(origin.x, 0.0f) && Near(origin.y, 0.0f));
}

void TestPortraitWindowLetterboxes() {
  const PresentationTransform transform = At(1000.0f, 1200.0f);
  const GuestPoint center = transform.ToLogical(500.0f, 600.0f);
  CHECK(Near(center.x, 320.0f) && Near(center.y, 224.0f));
  const GuestPoint bar = transform.ToLogical(500.0f, 100.0f);
  CHECK(Near(bar.y, 0.0f));
}

void TestRoundTripInsideLogicalScreen() {
  const PresentationTransform transform = At(1920.0f, 1080.0f);
  const GuestPoint window = transform.ToWindow(123.0f, 321.0f);
  const GuestPoint logical = transform.ToLogical(window.x, window.y);
  CHECK(Near(logical.x, 123.0f) && Near(logical.y, 321.0f));
}

void TestOffsetLogicalRectInFrame() {
  // A logical screen centered in a taller frame keeps its own origin.
  const PresentationTransform transform({640.0f, 512.0f}, kFrame, kLogical, {0.0f, 32.0f, 640.0f, 448.0f});
  const GuestPoint origin = transform.ToLogical(0.0f, 32.0f);
  CHECK(Near(origin.x, 0.0f) && Near(origin.y, 0.0f));
  const GuestPoint window = transform.ToWindow(0.0f, 0.0f);
  CHECK(Near(window.y, 32.0f));
}

void TestFilledFrameStretchesLogicalScreen() {
  // Before the GS reports PAL dimensions the runtime presents 640x448 frames.
  const PresentationTransform early = PresentationTransform::LogicalFillsFrame({1920.0f, 1080.0f}, {640.0f, 448.0f}, kLogical);
  const GuestPoint early_origin = early.ToWindow(0.0f, 0.0f);
  CHECK(Near(early_origin.x, 188.57f) && Near(early_origin.y, 0.0f));
  const PresentationTransform pal = PresentationTransform::LogicalFillsFrame({1920.0f, 1080.0f}, kFrame, kLogical);
  const GuestPoint corner = pal.ToWindow(640.0f, 448.0f);
  CHECK(Near(corner.x, 1635.0f) && Near(corner.y, 1080.0f));
}

template <typename Body> bool Throws(Body body) {
  try {
    body();
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

void TestRejectsInvalidGeometry() {
  CHECK(Throws([] { PresentationTransform({0.0f, 1080.0f}, kFrame, kLogical, kLogicalInFrame); }));
  CHECK(Throws([] { PresentationTransform({1920.0f, 1080.0f}, {0.0f, 512.0f}, kLogical, kLogicalInFrame); }));
  CHECK(Throws([] { PresentationTransform({1920.0f, 1080.0f}, kFrame, {640.0f, 0.0f}, kLogicalInFrame); }));
  CHECK(Throws([] { PresentationTransform({1920.0f, 1080.0f}, kFrame, kLogical, {0.0f, 64.0f, 640.0f, 512.0f}); }));
  CHECK(Throws([] { PresentationTransform({1920.0f, 1080.0f}, kFrame, kLogical, {0.0f, 0.0f, 0.0f, 512.0f}); }));
}

} // namespace

int main() {
  TestFrameDestinationAt1080p();
  TestToLogicalAt1080p();
  TestToWindowAt720p();
  TestNativeWindowKeepsLogicalCenter();
  TestPortraitWindowLetterboxes();
  TestRoundTripInsideLogicalScreen();
  TestOffsetLogicalRectInFrame();
  TestRejectsInvalidGeometry();
  TestFilledFrameStretchesLogicalScreen();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "presentation_transform: all checks passed" << std::endl;
  return EXIT_SUCCESS;
}
