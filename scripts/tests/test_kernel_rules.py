import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout, redirect_stderr
from io import StringIO
from cxx_config import CXX_STANDARD_FLAG

SCRIPT = Path(__file__).resolve().parents[1] / "check_kernel_rules.py"
spec = importlib.util.spec_from_file_location("kernel_rules", SCRIPT)
rules = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rules)


class ArchitectureRules(unittest.TestCase):
    def test_rejects_architecture_selection_and_aliases(self):
        for source in (
            "#if defined(__riscv)\n#endif\n",
            "#ifdef __aarch64__\n#endif\n",
            "#if 0\n#elif defined(__x86_64__)\n#endif\n",
            "#ifndef ARCH_RISCV64\n#endif\n",
            "#if CONFIG_X86_64\n#endif\n",
            "#if BOARD_QEMU_VIRT\n#endif\n",
            "#define LOCAL_ISA __riscv\n",
            "#if defined(\\\n__riscv)\n#endif\n",
            "#if defined(__ri\\\nscv)\n#endif\n",
        ):
            with self.subTest(source=source):
                self.assertTrue(rules.architecture_errors(source, "kernel/example.cpp"))

    def test_rejects_inline_assembly(self):
        for source in ('asm volatile("nop");', '__asm__("nop");'):
            self.assertTrue(rules.architecture_errors(source, "kernel/example.cpp"))

    def test_allows_config_guards_and_ignores_comments_strings(self):
        source = '''
            #ifdef TEST_MODE
            #if CONFIG_DEBUG
            #endif
            #endif
            // #if defined(__riscv)
            /* #ifdef __x86_64__ */
            const char* text = "asm volatile( __aarch64__ )";
            const char* raw = R"label(
            #if defined(__riscv)
            )label";
        '''
        self.assertEqual(rules.architecture_errors(source, "kernel/example.cpp"), [])


class PageTableRules(unittest.TestCase):
    def errors(self, body, definitions=""):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            header = root / "arch/aarch64/include/asm/page.h"
            header.parent.mkdir(parents=True)
            header.write_text('''
                using uintptr_t = unsigned long;
                #define PTE_VALID (1UL << 0)
                #define PTE_USER (1UL << 6)
                #define PTE_TYPE_MASK (PTE_VALID | PTE_USER)
                constexpr uintptr_t PTE_READ_ONLY = 1UL << 7;
            ''' + definitions + "\nbool pte_test(uintptr_t entry) { " + body + " }\n")
            ast = rules.compile_ast(header, ["clang++"], [CXX_STANDARD_FLAG, "-x", "c++"], root)
            return rules.page_table_errors(ast, root)

    def test_allows_named_masks_explicit_comparisons_and_boolean_composition(self):
        for body in (
            "return (entry & PTE_VALID) != 0;",
            "return (entry & PTE_READ_ONLY) == 0;",
            "return (entry & PTE_TYPE_MASK) == PTE_VALID;",
            "return (entry & (PTE_VALID | PTE_USER)) != 0;",
            "bool present = (entry & PTE_VALID) != 0; return present && (entry & PTE_USER) != 0;",
        ):
            with self.subTest(body=body):
                self.assertEqual(self.errors(body), [])

    def test_rejects_implicit_boolean_bit_tests_in_every_context(self):
        for body in (
            "return entry & PTE_VALID;",
            "return !(entry & PTE_VALID);",
            "return true && (entry & PTE_USER);",
            "return (entry & PTE_USER) || false;",
            "if (entry & PTE_VALID) return true; return false;",
            "bool present = entry & PTE_VALID; return present;",
        ):
            with self.subTest(body=body):
                errors = self.errors(body)
                self.assertTrue(any("compare explicitly" in error for error in errors), errors)

    def test_rejects_numeric_masks_including_shifted_and_mixed_masks(self):
        for body in (
            "return (entry & 0x80) == 0;",
            "return (entry & (1UL << 7)) == 0;",
            "return ((1UL << 7) & entry) == 0;",
            "return (entry & (PTE_VALID | (1UL << 7))) != 0;",
        ):
            with self.subTest(body=body):
                errors = self.errors(body)
                self.assertTrue(any("semantic names" in error for error in errors), errors)

    def test_macros_do_not_hide_implicit_tests_or_numeric_mask_arguments(self):
        self.assertTrue(any("compare explicitly" in error for error in self.errors(
            "return TEST(entry);", "\n#define TEST(value) ((value) & PTE_USER)\n"
        )))
        self.assertTrue(any("semantic names" in error for error in self.errors(
            "return (entry & ID(1UL << 7)) == 0;", "\n#define ID(value) value\n"
        )))

    def test_rejects_raw_bit_positions_but_allows_named_shifts(self):
        self.assertTrue(any("semantic names" in error for error in self.errors(
            "return (entry >> 7) == 0;"
        )))
        self.assertEqual(self.errors(
            "return (entry >> PTE_SHIFT) == 0;", "\n#define PTE_SHIFT 7\n"
        ), [])


class OwnershipRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("clang++"):
            raise RuntimeError("clang++ is required to test the kernel harness")

    def state(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            path = root / "kernel/example.cpp"
            path.parent.mkdir()
            path.write_text(source)
            return set(rules.compile_state(path, ["clang++"], [CXX_STANDARD_FLAG], root))

    def test_rejects_namespace_extern_and_function_static_state(self):
        found = self.state('''
            int global;
            namespace { int hidden; }
            namespace subsystem { extern int external; }
            void function() { static int persistent; thread_local int thread_state; int temporary = 0; }
        ''')
        self.assertEqual(found, {
            "kernel/example.cpp:global", "kernel/example.cpp:hidden",
            "kernel/example.cpp:subsystem::external", "kernel/example.cpp:function::persistent",
            "kernel/example.cpp:function::thread_state",
        })

    def test_allows_class_members_out_of_class_definitions_and_constants(self):
        self.assertEqual(self.state('''
            class Owner {
                inline static int internal{};
                static int separate;
                int per_instance{};
            };
            int Owner::separate{};
            constexpr int limit = 4;
            const int lookup[] = {1, 2};
            const char* const text = "constant";
            void function() { int temporary = 0; static constexpr int constant = 1; }
            struct Fixture {
                Fixture() { int constructor_local = 0; }
                ~Fixture() { int destructor_local = 0; }
            };
        '''), set())

    def test_const_pointee_and_const_pointer_do_not_hide_mutable_state(self):
        found = self.state('''
            const int* pointer_to_const;
            int* const constant_pointer_to_mutable = nullptr;
            constexpr int* constexpr_pointer_to_mutable = nullptr;
            using Pointer = int*;
            Pointer aliased_pointer;
        ''')
        self.assertEqual(len(found), 4)

    def test_constinit_private_static_members_keep_class_ownership(self):
        self.assertEqual(self.state('''
            class Owner {
                inline static constinit int count_ = 0;
                static constinit int separate_;
            };
            constinit int Owner::separate_ = 0;
        '''), set())

    def test_constinit_does_not_hide_unowned_mutable_state(self):
        self.assertEqual(self.state('''
            constinit int global = 0;
            namespace { constinit int hidden = 0; }
            void function() { static constinit int persistent = 0; }
        '''), {"kernel/example.cpp:global", "kernel/example.cpp:hidden",
               "kernel/example.cpp:function::persistent"})

    def test_checks_globals_generated_by_macros_and_included_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            folder = root / "kernel"
            folder.mkdir()
            (folder / "state.h").write_text("inline int header_state;\n")
            path = folder / "example.cpp"
            path.write_text('#include "state.h"\n#define STATE(name) int name;\nSTATE(generated)\n')
            found = rules.compile_state(path, ["clang++"], [CXX_STANDARD_FLAG], root)
            self.assertIn("kernel/state.h:header_state", found)
            self.assertIn("kernel/example.cpp:generated", found)

    def test_macro_ranges_do_not_misattribute_later_declarations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            folder = root / "kernel"
            folder.mkdir()
            (folder / "helper.h").write_text("#define RETURN return\n")
            path = folder / "example.cpp"
            path.write_text('#include "helper.h"\nvoid f() { RETURN; }\nint actual_state;\n')
            found = rules.compile_state(path, ["clang++"], [CXX_STANDARD_FLAG], root)
            self.assertIn("kernel/example.cpp:actual_state", found)
            self.assertEqual(found["kernel/example.cpp:actual_state"]["line"], 3)

    def test_architecture_headers_are_checked_for_unowned_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            folder = root / "arch/x86/include/asm"
            folder.mkdir(parents=True)
            (folder / "pgtable.h").write_text('extern "C" unsigned long __kernel_pg_dir[512];\nint leaked;\n')
            path = root / "kernel/example.cpp"
            path.parent.mkdir()
            path.write_text('#include "../arch/x86/include/asm/pgtable.h"\n')
            found = rules.compile_state(path, ["clang++"], [CXX_STANDARD_FLAG], root)
            self.assertEqual(set(found), {
                "arch/x86/include/asm/pgtable.h:__kernel_pg_dir",
                "arch/x86/include/asm/pgtable.h:leaked",
            })

    def test_cached_success_is_invalidated_by_state_or_inactive_isa_branch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            for name in ("kernel", "include", "arch/x86", "docs", "boot", "user"):
                (root / name).mkdir(parents=True)
            (root / "Makefile").write_text("")
            (root / "arch/x86/Makefile").write_text("")
            exceptions = root / "exceptions.json"
            exceptions.write_text("[]")
            naming_exceptions = root / "naming-exceptions.json"
            naming_exceptions.write_text("[]")
            (root / ".clang-tidy").write_text("")
            (root / "docs/NAMING.md").write_text("")
            source = root / "kernel/example.cpp"
            owned = "class Owner { inline static int state_{}; };\n"
            source.write_text(owned)
            cache = root / "check.sha256"
            config = (["clang++"], [CXX_STANDARD_FLAG], ["kernel/example.cpp"])
            with patch.object(rules, "ROOT", root), patch.object(rules, "EXCEPTIONS", exceptions), \
                    patch.object(rules, "NAMING_EXCEPTIONS", naming_exceptions), \
                    patch.object(rules, "__file__", str(root / "checker.py")), \
                    patch.object(rules, "build_config", side_effect=lambda arch, target="kernel-rule-config":
                                 config if target == "kernel-rule-config" else []), \
                    redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                (root / "checker.py").write_text("")
                # compile_state's default root is bound on import; supply this fixture root.
                original = rules.compile_state
                with patch.object(rules, "compile_state", side_effect=lambda s, c, f: original(s, c, f, root)):
                    self.assertEqual(rules.check("x86", cache), 0)
                    self.assertEqual(rules.check("x86", cache), 0)
                    source.write_text(owned + "int unowned;\n")
                    self.assertEqual(rules.check("x86", cache), 1)
                    source.write_text(owned)
                    (root / "kernel/unused.h").write_text("#if 0\n#ifdef __riscv\n#endif\n#endif\n")
                    self.assertEqual(rules.check("x86", cache), 1)


if __name__ == "__main__":
    unittest.main()
