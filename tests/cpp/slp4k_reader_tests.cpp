#include <aoe2/assets/slp4k_reader.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using aoe2::assets::Slp4kReader;
using aoe2::assets::Slp4kFrame;

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

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void PutU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

// Layout observed in the PS2 container: "4.2k", u32 frame count, then one 16-byte
// entry per frame: u32 command offset, u32 outline offset, u16 width, u16 height,
// i16 hotspot x, i16 hotspot y.
std::vector<uint8_t> TwoFrameShape() {
  std::vector<uint8_t> bytes = {'4', '.', '2', 'k'};
  PutU32(bytes, 2);
  PutU32(bytes, 1220);
  PutU32(bytes, 262376);
  PutU16(bytes, 53);
  PutU16(bytes, 35);
  PutU16(bytes, 43);
  PutU16(bytes, 24);
  PutU32(bytes, 1588);
  PutU32(bytes, 262448);
  PutU16(bytes, 51);
  PutU16(bytes, 33);
  PutU16(bytes, 41);
  PutU16(bytes, 23);
  return bytes;
}

void TestReadsFrameCount() {
  const std::vector<uint8_t> bytes = TwoFrameShape();
  const auto reader = Slp4kReader::Parse(bytes.data(), bytes.size());
  CHECK(reader.FrameCount() == 2);
}

void TestReadsFrameGeometry() {
  const std::vector<uint8_t> bytes = TwoFrameShape();
  const auto reader = Slp4kReader::Parse(bytes.data(), bytes.size());
  const Slp4kFrame first = reader.Frame(0);
  CHECK(first.command_offset == 1220 && first.outline_offset == 262376);
  CHECK(first.width == 53 && first.height == 35);
  CHECK(first.hotspot_x == 43 && first.hotspot_y == 24);
  const Slp4kFrame second = reader.Frame(1);
  CHECK(second.width == 51 && second.height == 33);
}

void TestRejectsWrongTag() {
  std::vector<uint8_t> bytes = TwoFrameShape();
  bytes[0] = '2';
  bool threw = false;
  try {
    Slp4kReader::Parse(bytes.data(), bytes.size());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestRejectsTruncatedFrameTable() {
  const std::vector<uint8_t> full = TwoFrameShape();
  const std::vector<uint8_t> truncated(full.begin(), full.end() - 1);
  bool threw = false;
  try {
    Slp4kReader::Parse(truncated.data(), truncated.size());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFrameIndexOutOfRangeThrows() {
  const std::vector<uint8_t> bytes = TwoFrameShape();
  const auto reader = Slp4kReader::Parse(bytes.data(), bytes.size());
  bool threw = false;
  try {
    reader.Frame(2);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
}

} // namespace

int main() {
  TestReadsFrameCount();
  TestReadsFrameGeometry();
  TestRejectsWrongTag();
  TestRejectsTruncatedFrameTable();
  TestFrameIndexOutOfRangeThrows();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "slp4k_reader: all checks passed" << std::endl;
  return EXIT_SUCCESS;
}
