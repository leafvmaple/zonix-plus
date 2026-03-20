#include "test/test_defs.h"
#include "lib/string.h"
#include "lib/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// strlen
// ============================================================================

static void test_strlen() {
    TEST_START("strlen");

    TEST_ASSERT(strlen("") == 0, "Empty string has length 0");
    TEST_ASSERT(strlen("a") == 1, "Single char has length 1");
    TEST_ASSERT(strlen("hello") == 5, "\"hello\" has length 5");
    TEST_ASSERT(strlen("hello world") == 11, "\"hello world\" has length 11");

    TEST_END();
}

// ============================================================================
// strcmp
// ============================================================================

static void test_strcmp() {
    TEST_START("strcmp");

    TEST_ASSERT(strcmp("abc", "abc") == 0, "Equal strings return 0");
    TEST_ASSERT(strcmp("abc", "abd") < 0, "\"abc\" < \"abd\"");
    TEST_ASSERT(strcmp("abd", "abc") > 0, "\"abd\" > \"abc\"");
    TEST_ASSERT(strcmp("", "") == 0, "Empty strings are equal");
    TEST_ASSERT(strcmp("a", "") > 0, "Non-empty > empty");
    TEST_ASSERT(strcmp("", "a") < 0, "Empty < non-empty");
    TEST_ASSERT(strcmp("abc", "abcd") < 0, "Prefix < longer string");
    TEST_ASSERT(strcmp("abcd", "abc") > 0, "Longer string > prefix");

    TEST_END();
}

// ============================================================================
// strcpy / strncpy
// ============================================================================

static void test_strcpy() {
    TEST_START("strcpy");

    char buf[32];
    memset(buf, 0xFF, sizeof(buf));

    strcpy(buf, "hello");
    TEST_ASSERT(strcmp(buf, "hello") == 0, "strcpy copies string correctly");

    strcpy(buf, "");
    TEST_ASSERT(buf[0] == '\0', "strcpy copies empty string");

    TEST_END();
}

static void test_strncpy() {
    TEST_START("strncpy");

    char buf[16];
    memset(buf, 0xFF, sizeof(buf));

    strncpy(buf, "hello", 16);
    TEST_ASSERT(strcmp(buf, "hello") == 0, "strncpy copies string");
    TEST_ASSERT(buf[5] == '\0', "strncpy null-pads after string");
    TEST_ASSERT(buf[15] == '\0', "strncpy null-pads to end");

    memset(buf, 0xFF, sizeof(buf));
    strncpy(buf, "abcdefghijklmnop", 8);
    TEST_ASSERT(buf[0] == 'a' && buf[7] == 'h', "strncpy truncates at n");

    TEST_END();
}

// ============================================================================
// strchr
// ============================================================================

static void test_strchr() {
    TEST_START("strchr");

    const char* s = "hello world";
    TEST_ASSERT(strchr(s, 'h') == s, "Find first char");
    TEST_ASSERT(strchr(s, 'o') == s + 4, "Find middle char (first occurrence)");
    TEST_ASSERT(strchr(s, 'd') == s + 10, "Find last char");
    TEST_ASSERT(strchr(s, 'z') == nullptr, "Missing char returns nullptr");
    TEST_ASSERT(strchr("", 'a') == nullptr, "Empty string returns nullptr");

    TEST_END();
}

// ============================================================================
// str_starts_with
// ============================================================================

static void test_str_starts_with() {
    TEST_START("str_starts_with");

    TEST_ASSERT(str_starts_with("hello", "hel"), "\"hello\" starts with \"hel\"");
    TEST_ASSERT(str_starts_with("hello", "hello"), "\"hello\" starts with \"hello\"");
    TEST_ASSERT(str_starts_with("hello", ""), "Any string starts with empty");
    TEST_ASSERT(!str_starts_with("hello", "world"), "\"hello\" !starts with \"world\"");
    TEST_ASSERT(!str_starts_with("hi", "hello"), "Short string !starts with longer prefix");
    TEST_ASSERT(!str_starts_with(nullptr, "a"), "nullptr returns false");
    TEST_ASSERT(!str_starts_with("a", nullptr), "null prefix returns false");

    TEST_END();
}

// ============================================================================
// str_skip_char
// ============================================================================

static void test_str_skip_char() {
    TEST_START("str_skip_char");

    TEST_ASSERT(strcmp(str_skip_char("///path", '/'), "path") == 0, "Skips leading slashes");
    TEST_ASSERT(strcmp(str_skip_char("path", '/'), "path") == 0, "No leading char to skip");
    TEST_ASSERT(strcmp(str_skip_char("", '/'), "") == 0, "Empty string unchanged");
    TEST_ASSERT(str_skip_char(nullptr, '/') == nullptr, "nullptr returns nullptr");

    const char* all_slashes = "///";
    const char* result = str_skip_char(all_slashes, '/');
    TEST_ASSERT(*result == '\0', "All chars skipped leaves empty");

    TEST_END();
}

// ============================================================================
// memset / memcpy / memcmp
// ============================================================================

static void test_mem_functions() {
    TEST_START("memset/memcpy/memcmp");

    uint8_t buf1[64], buf2[64];

    memset(buf1, 0xAA, 64);
    bool all_aa = true;
    for (int i = 0; i < 64; i++) {
        if (buf1[i] != 0xAA) {
            all_aa = false;
            break;
        }
    }
    TEST_ASSERT(all_aa, "memset fills buffer correctly");

    memset(buf1, 0, 64);
    TEST_ASSERT(buf1[0] == 0 && buf1[63] == 0, "memset zero works");

    for (int i = 0; i < 64; i++)
        buf1[i] = (uint8_t)i;

    memcpy(buf2, buf1, 64);
    TEST_ASSERT(memcmp(buf1, buf2, 64) == 0, "memcpy + memcmp: buffers match");

    buf2[32] = 0xFF;
    TEST_ASSERT(memcmp(buf1, buf2, 64) != 0, "memcmp detects difference");

    TEST_ASSERT(memcmp(buf1, buf1, 0) == 0, "memcmp size=0 returns 0");

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace string_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_strlen();
    test_strcmp();
    test_strcpy();
    test_strncpy();
    test_strchr();
    test_str_starts_with();
    test_str_skip_char();
    test_mem_functions();

    TEST_SUMMARY("String Library");
}

}  // namespace string_test
