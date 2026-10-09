"""Exercise the kernel Result contract and assertion failures on the host."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from cxx_config import CXX_STANDARD_FLAG

ROOT = Path(__file__).resolve().parents[2]
CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for Result contract tests")
class ResultContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-result-tests-")
        cls.binary = Path(cls.directory.name) / "result-tests"
        cls.flags = [CXX_STANDARD_FLAG, "-ffreestanding", "-fno-exceptions", "-fno-rtti",
                     "-Werror=unused-result", "-Werror=unused-value", "-I" + str(ROOT / "kernel"),
                     "-I" + str(ROOT / "include"), "-I" + str(ROOT / "arch/x86/include"),
                     "-I" + str(ROOT / "arch/x86/kernel")]
        result = subprocess.run([CLANG, *cls.flags, str(ROOT / "kernel/test/unit/lib/result_test.cpp"),
                                 str(ROOT / "kernel/test/host/result_contract_test.cpp"), "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("Result contract test compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_value_error_copy_move_lifetime_and_try(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("[FAIL]", result.stdout)
        self.assertIn("All Result Library tests passed", result.stdout)

    def test_invalid_states_and_access_panic(self):
        for mode in ("none", "value", "error", "taken-value", "queried-error", "fallback",
                     "wrong-void", "success-void", "x-void", "yielded-void"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 77, result.stdout + result.stderr)

    def test_constrained_copy_control_in_freestanding_cxx20(self):
        source = '''
            static_assert(__cplusplus >= 202002L);
            struct MoveOnly {
                MoveOnly() = default;
                MoveOnly(const MoveOnly&) = delete;
                MoveOnly(MoveOnly&&) = default;
            };
            template<typename T>
            struct CopyControl {
                CopyControl() = default;
                CopyControl(const CopyControl&) requires (__is_constructible(T, const T&)) {}
                CopyControl(CopyControl&&) = default;
            };
            static_assert(__is_constructible(CopyControl<int>, const CopyControl<int>&));
            static_assert(!__is_constructible(CopyControl<MoveOnly>, const CopyControl<MoveOnly>&));
            static_assert(__is_constructible(CopyControl<MoveOnly>, CopyControl<MoveOnly>&&));
        '''
        result = subprocess.run([CLANG, *self.flags, "-nostdinc", "-nostdinc++", "-x", "c++", "-fsyntax-only", "-"],
                                input=source, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_borrowing_a_temporary_and_ignoring_a_result(self):
        for statement in ("Result<int>(42).value();", "Result<int>(42);", "Result<void>();"):
            with self.subTest(statement=statement):
                result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                        input='#include "lib/result.h"\nvoid misuse() { ' + statement + ' }\n',
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stderr)

    def test_rejects_copying_a_move_only_result(self):
        source = '''
            #include "lib/result.h"
            struct MoveOnly {
                MoveOnly() = default;
                MoveOnly(const MoveOnly&) = delete;
                MoveOnly(MoveOnly&&) = default;
            };
            void misuse() {
                Result<MoveOnly> source{MoveOnly{}};
        '''
        for statement in ("Result<MoveOnly> copy{source};", "Result<MoveOnly> copy{Error::Io}; copy = source;"):
            with self.subTest(statement=statement):
                result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                        input=source + statement + "\n}\n",
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn("deleted", result.stderr)

    def test_rejects_copying_a_move_only_value_or_fallback(self):
        source = '''
            #include "lib/result.h"
            struct MoveOnly {
                MoveOnly() = default;
                MoveOnly(const MoveOnly&) = delete;
                MoveOnly(MoveOnly&&) = default;
            };
            void misuse(const MoveOnly& value) {
        '''
        for statement in ("Result<MoveOnly> result{value};",
                          "const Result<MoveOnly> result{Error::Io}; (void)result.value_or(value);"):
            with self.subTest(statement=statement):
                result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                        input=source + statement + "\n}\n",
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn("constraints not satisfied", result.stderr)

    def test_rejects_ignoring_plain_error_status(self):
        source = '#include "lib/result.h"\nError operation() { return Error::Io; }\n'
        result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                input=source + 'void misuse() { operation(); }\n',
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn("nodiscard", result.stderr)
        result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                input=source + '''
                                    Error propagate() { return TRY(operation()), Error::None; }
                                    bool check() { return operation() == Error::None; }
                                    void discard() { (void)operation(); }
                                ''', capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
