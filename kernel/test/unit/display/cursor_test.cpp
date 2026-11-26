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

}  // namespace

namespace display_cursor_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;
    test_without_cursor_plane();
    TEST_SUMMARY("Display cursor");
}

}  // namespace display_cursor_test
