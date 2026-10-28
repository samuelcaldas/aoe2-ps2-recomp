#include "shape_trace_model.hpp"

#include <cstdlib>
#include <iostream>

using aoe2::game_hooks::ShapeTraceModel;

namespace {

int g_failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ":" << __LINE__                                 \
                << ": CHECK failed: " << #condition << std::endl;              \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void TestDrawOfLoadedShapeUsesItsNameAndId() {
  ShapeTraceModel model;
  model.Loaded(0x1000, "interfac", 50100);
  model.Drawn(0x1000, 3);
  model.Drawn(0x1000, 3);
  model.Drawn(0x1000, 4);
  CHECK(model.Report(8) == "interfac#50100/3:2 interfac#50100/4:1");
}

void TestUnknownShapeIsReportedByAddress() {
  ShapeTraceModel model;
  model.Drawn(0x2a0, 0);
  CHECK(model.Report(8) == "@0x2a0/0:1");
}

void TestReloadReplacesIdentity() {
  ShapeTraceModel model;
  model.Loaded(0x1000, "old", 1);
  model.Loaded(0x1000, "new", 2);
  model.Drawn(0x1000, 0);
  CHECK(model.Report(8) == "new#2/0:1");
}

void TestReportIsBusiestFirstAndLimitedThenResets() {
  ShapeTraceModel model;
  model.Loaded(0x10, "a", 1);
  model.Loaded(0x20, "b", 2);
  model.Drawn(0x10, 0);
  model.Drawn(0x20, 0);
  model.Drawn(0x20, 0);
  CHECK(model.Report(1) == "b#2/0:2");
  CHECK(model.Report(8).empty());
  CHECK(model.draws() == 3);
}

} // namespace

int main() {
  TestDrawOfLoadedShapeUsesItsNameAndId();
  TestUnknownShapeIsReportedByAddress();
  TestReloadReplacesIdentity();
  TestReportIsBusiestFirstAndLimitedThenResets();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "shape_trace_model: all checks passed" << std::endl;
  return EXIT_SUCCESS;
}
