#include "test/test_defs.h"
#include "drivers/display.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// Desktop draws its own pointer when the display refuses an image, and keeps
// the position for an image shown later. The test machines have no cursor
// plane, so the image is refused once its arguments are known to be valid.
void test_without_cursor_plane() {
    TEST_START("Display cursor: requests on a display without a cursor plane");
    static uint32_t pixels[DISPLAY_CURSOR_SIZE * DISPLAY_CURSOR_SIZE]{};
    const display::Surface screen = display::surface();
    if (!screen.pixels) {
        TEST_ASSERT(display::show_cursor({pixels, 0, 0}) == Error::NoDevice &&
                        display::move_cursor(0, 0) == Error::NoDevice,
                    "Without a screen there is no cursor");
    } else {
        TEST_ASSERT(display::show_cursor({pixels, DISPLAY_CURSOR_SIZE, 0}) == Error::Invalid &&
                        display::show_cursor({nullptr, 0, 0}) == Error::Invalid,
                    "A hot pixel outside the image or a missing image is invalid");
        TEST_ASSERT(display::show_cursor({pixels, DISPLAY_CURSOR_SIZE - 1, DISPLAY_CURSOR_SIZE - 1}) ==
                        Error::NotSupported,
                    "A valid image is refused, so the client draws the pointer");
        TEST_ASSERT(display::move_cursor(screen.width - 1, screen.height - 1) == Error::None,
                    "A hidden cursor keeps any position on the screen");
        TEST_ASSERT(display::move_cursor(screen.width, 0) == Error::Invalid &&
                        display::move_cursor(0, screen.height) == Error::Invalid,
                    "A position off the screen is invalid");
        static_cast<void>(display::move_cursor(0, 0));
    }
    display::hide_cursor();
    TEST_END();
}

// The pixels of a rectangle, one row of `width` words after another.
class Pattern final : public display::BlitSource {
public:
    Pattern(const uint32_t* pixels, uint32_t width) : pixels_(pixels), width_(width) {}
    Error copy_row(uint32_t row, uint32_t* out, uint32_t width) override {
        for (uint32_t col = 0; col < width; ++col)
            out[col] = pixels_[row * width_ + col];
        return Error::None;
    }

private:
    const uint32_t* pixels_;
    uint32_t width_;
};

// Reading the display device is how a screenshot leaves the machine: it must
// return what was presented, in XRGB order whatever the firmware's layout, at
// any byte offset, and end where the screen ends.
void test_capture() {
    TEST_START("Display capture: reads return the presented pixels");
    const display::Surface screen = display::surface();
    uint8_t probe[8]{};
    if (!screen.pixels) {
        auto none = display::capture(probe, sizeof(probe), 0);
        TEST_ASSERT(!none.ok() && none.error() == Error::NoDevice, "Without a screen there is nothing to read");
        TEST_END();
        return;
    }
    constexpr uint32_t cols = 3;
    constexpr uint32_t rows = 2;
    const size_t row_bytes = static_cast<size_t>(screen.width) * sizeof(uint32_t);
    const size_t total = row_bytes * screen.height;
    uint32_t saved[cols * rows]{};
    bool ok = true;
    for (uint32_t row = 0; row < rows; ++row)
        ok = ok &&
             display::capture(saved + row * cols, cols * 4, row * row_bytes).value_or(-1) == static_cast<int>(cols * 4);
    const uint32_t pattern[cols * rows]{0x00112233, 0x00445566, 0x00778899, 0x00AABBCC, 0x00DDEEFF, 0x00010203};
    Pattern source(pattern, cols);
    ok = ok && display::present({0, 0, cols, rows, &source}) == Error::None;
    uint32_t back[cols * rows]{};
    for (uint32_t row = 0; row < rows; ++row)
        ok = ok &&
             display::capture(back + row * cols, cols * 4, row * row_bytes).value_or(-1) == static_cast<int>(cols * 4);
    // Bytes 1..6 span the end of one pixel and most of the next. Every read
    // comes before the first assertion, whose log line may land on the
    // framebuffer console over these pixels.
    uint8_t slice[6]{};
    bool sliced = display::capture(slice, sizeof(slice), 1).value_or(-1) == 6;
    Pattern restore(saved, cols);
    static_cast<void>(display::present({0, 0, cols, rows, &restore}));

    bool same = ok;
    for (uint32_t i = 0; i < cols * rows; ++i)
        same = same && back[i] == pattern[i];
    TEST_ASSERT(same, "Rows read back hold the presented XRGB pixels");
    const auto* expected = reinterpret_cast<const uint8_t*>(pattern);
    for (size_t i = 0; i < sizeof(slice); ++i)
        sliced = sliced && slice[i] == expected[1 + i];
    TEST_ASSERT(sliced, "A read may start and end inside a pixel");

    TEST_ASSERT(display::capture(probe, sizeof(probe), total - 2).value_or(-1) == 2 &&
                    display::capture(probe, sizeof(probe), total).value_or(-1) == 0,
                "A read stops at the end of the screen");
    TEST_END();
}

}  // namespace

namespace display_cursor_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;
    test_without_cursor_plane();
    test_capture();
    TEST_SUMMARY("Display");
}

}  // namespace display_cursor_test
