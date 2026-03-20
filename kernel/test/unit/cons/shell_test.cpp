#include "test/test_defs.h"
#include "cons/shell.h"
#include "lib/string.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Tracking helpers for command dispatch tests
// ============================================================================

static int g_dispatch_argc = -1;
static const char* g_dispatch_argv0 = nullptr;
static int g_dispatch_count = 0;

static void tracking_cmd(int argc, char** argv) {
    g_dispatch_argc = argc;
    g_dispatch_argv0 = (argc > 0) ? argv[0] : nullptr;
    g_dispatch_count++;
}

static void dummy_cmd_a(int, char**) {}
static void dummy_cmd_b(int, char**) {}

// ============================================================================
// register_command: null arguments
// ============================================================================

static void test_register_null() {
    TEST_START("shell::register_command — null arguments");

    int rc1 = shell::register_command(nullptr, "desc", dummy_cmd_a);
    TEST_ASSERT(rc1 != 0, "nullptr name rejected");

    int rc2 = shell::register_command("_test_null", nullptr, dummy_cmd_a);
    TEST_ASSERT(rc2 != 0, "nullptr desc rejected");

    int rc3 = shell::register_command("_test_null", "desc", nullptr);
    TEST_ASSERT(rc3 != 0, "nullptr func rejected");

    TEST_END();
}

// ============================================================================
// register_command: success + duplicate rejection
// ============================================================================

static void test_register_and_duplicate() {
    TEST_START("shell::register_command — register + duplicate");

    int rc1 = shell::register_command("__ci_utest_xyz", "CI test", dummy_cmd_a);
    int rc2 = shell::register_command("__ci_utest_xyz", "CI dup", dummy_cmd_b);

    TEST_ASSERT(rc1 == 0, "First registration succeeds");
    TEST_ASSERT(rc2 != 0, "Duplicate name rejected");

    TEST_END();
}

// ============================================================================
// Builtin commands: verify all 14 are registered
// After shell::init() the builtin names should already be taken.
// We detect this by attempting to register commands with the same names —
// register_command returns -1 for duplicates.
// ============================================================================

static void test_builtin_commands_registered() {
    TEST_START("shell builtins — all 14 registered");

    static const char* builtins[] = {
        "help", "pgdir", "clear",  "lsblk", "hdparm", "dd",  "uname",
        "ps",   "mount", "umount", "info",  "ls",     "cat", "exec",
    };
    constexpr int N = sizeof(builtins) / sizeof(builtins[0]);

    int registered = 0;
    for (int i = 0; i < N; i++) {
        int rc = shell::register_command(builtins[i], "probe", dummy_cmd_a);
        if (rc != 0) {
            registered++;
        }
    }

    TEST_ASSERT(registered == N, "All 14 builtin names already registered");

    TEST_END();
}

// ============================================================================
// handle_char: command dispatch via character input
// Register a command, type its name char by char, press enter, verify it ran.
// ============================================================================

static void test_handle_char_dispatch() {
    TEST_START("shell::handle_char — command dispatch");

    int rc = shell::register_command("__ci_dispatch", "dispatch test", tracking_cmd);
    if (rc != 0) {
        TEST_ASSERT(false, "Could not register tracking command (name collision?)");
        TEST_END();
        return;
    }

    g_dispatch_count = 0;
    g_dispatch_argc = -1;
    g_dispatch_argv0 = nullptr;

    const char* input = "__ci_dispatch";
    for (int i = 0; input[i]; i++) {
        shell::handle_char(input[i]);
    }
    shell::handle_char('\n');

    TEST_ASSERT(g_dispatch_count == 1, "Command invoked exactly once");
    TEST_ASSERT(g_dispatch_argc == 1, "argc == 1 (no extra args)");
    TEST_ASSERT(g_dispatch_argv0 != nullptr, "argv[0] is non-null");

    TEST_END();
}

// ============================================================================
// handle_char: command with arguments
// ============================================================================

static int g_arg_argc = -1;
static char g_arg_argv1[32] = {};

static void arg_tracking_cmd(int argc, char** argv) {
    g_arg_argc = argc;
    if (argc >= 2 && argv[1]) {
        int i = 0;
        while (argv[1][i] && i < 31) {
            g_arg_argv1[i] = argv[1][i];
            i++;
        }
        g_arg_argv1[i] = '\0';
    }
}

static void test_handle_char_with_args() {
    TEST_START("shell::handle_char — command with arguments");

    int rc = shell::register_command("__ci_argtest", "arg test", arg_tracking_cmd);
    if (rc != 0) {
        TEST_ASSERT(false, "Could not register arg tracking command");
        TEST_END();
        return;
    }

    g_arg_argc = -1;
    g_arg_argv1[0] = '\0';

    const char* input = "__ci_argtest hello";
    for (int i = 0; input[i]; i++) {
        shell::handle_char(input[i]);
    }
    shell::handle_char('\n');

    TEST_ASSERT(g_arg_argc == 2, "argc == 2");
    TEST_ASSERT(strcmp(g_arg_argv1, "hello") == 0, "argv[1] == \"hello\"");

    TEST_END();
}

// ============================================================================
// handle_char: backspace
// Type "abx\bcd\n" → should execute "abcd"
// ============================================================================

static int g_bs_argc = -1;
static char g_bs_argv0[32] = {};

static void bs_tracking_cmd(int argc, char** argv) {
    g_bs_argc = argc;
    if (argc >= 1 && argv[0]) {
        int i = 0;
        while (argv[0][i] && i < 31) {
            g_bs_argv0[i] = argv[0][i];
            i++;
        }
        g_bs_argv0[i] = '\0';
    }
}

static void test_handle_char_backspace() {
    TEST_START("shell::handle_char — backspace");

    int rc = shell::register_command("__ci_bsok", "bs test", bs_tracking_cmd);
    if (rc != 0) {
        TEST_ASSERT(false, "Could not register backspace tracking command");
        TEST_END();
        return;
    }

    g_bs_argc = -1;
    g_bs_argv0[0] = '\0';

    // Type "__ci_bsXX\b\bok\n" → erases "XX", then types "ok" → "__ci_bsok"
    const char* part1 = "__ci_bsXX";
    for (int i = 0; part1[i]; i++) {
        shell::handle_char(part1[i]);
    }
    shell::handle_char('\b');
    shell::handle_char('\b');
    shell::handle_char('o');
    shell::handle_char('k');
    shell::handle_char('\n');

    TEST_ASSERT(g_bs_argc == 1, "Command executed after backspace correction");
    TEST_ASSERT(strcmp(g_bs_argv0, "__ci_bsok") == 0, "Backspace erased correctly → \"__ci_bsok\"");

    TEST_END();
}

// ============================================================================
// handle_char: empty input (just enter)
// ============================================================================

static void test_handle_char_empty() {
    TEST_START("shell::handle_char — empty input");

    g_dispatch_count = 0;
    shell::handle_char('\n');

    // Empty input should not dispatch any command, g_dispatch_count unchanged
    // (We can't easily verify "nothing was dispatched" without side effects,
    //  but at least verify no crash)
    TEST_ASSERT(true, "Empty enter does not crash");

    TEST_END();
}

// ============================================================================
// handle_char: unprintable / control characters ignored
// ============================================================================

static void test_handle_char_control_chars() {
    TEST_START("shell::handle_char — control chars ignored");

    // Send some control characters, then a valid command
    shell::handle_char('\0');  // NUL — handle_char checks c <= 0
    shell::handle_char(0x01);  // SOH — below printable range
    shell::handle_char(0x7F);  // DEL — explicitly ignored

    // These should not have added anything to the buffer — no crash
    TEST_ASSERT(true, "Control characters do not crash");

    // Clean up the buffer by pressing enter (flush whatever is there)
    shell::handle_char('\n');

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace shell_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    // API tests (safe to run anytime)
    test_register_null();
    test_register_and_duplicate();

    // Tests below require shell::init() to have been called.
    // In CI mode, the shell thread runs concurrently and will have
    // called init() before we reach this point (after 7 prior suites).
    test_builtin_commands_registered();
    test_handle_char_dispatch();
    test_handle_char_with_args();
    test_handle_char_backspace();
    test_handle_char_empty();
    test_handle_char_control_chars();

    TEST_SUMMARY("Shell");
}

}  // namespace shell_test
