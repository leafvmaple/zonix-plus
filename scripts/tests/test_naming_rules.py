import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout, redirect_stderr
from io import StringIO
from cxx_config import CXX_STANDARD_FLAG

SCRIPT = Path(__file__).resolve().parents[1] / "check_kernel_rules.py"
SPEC = importlib.util.spec_from_file_location("naming_rules", SCRIPT)
rules = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rules)


class NamingRules(unittest.TestCase):
    def errors(self, source, allowed=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            path = root / "kernel/example.cpp"
            path.parent.mkdir()
            path.write_text(source)
            ast = rules.compile_ast(path, ["clang++"], [CXX_STANDARD_FLAG], root)
            return rules.naming_errors(ast, root, allowed)

    def test_accepts_project_types_enums_aliases_and_template_roles(self):
        self.assertEqual(self.errors('''
            namespace vm_test {
            enum class PageState { Free, Mapped };
            template<typename Element, unsigned CAPACITY, bool REVERSE = false>
            struct PageArray { Element entries[CAPACITY]; };
            using ByteArray = PageArray<unsigned char, 4>;
            }
        '''), [])

    def test_accepts_concepts_requires_and_constrained_copy(self):
        self.assertEqual(self.errors('''
            template<typename T>
            concept Copyable = __is_constructible(T, const T&);
            template<typename T>
            concept Lockable = requires(T& lock) { lock.acquire(); lock.release(); };
            template<typename T>
            struct Wrapper {
                T value;
                Wrapper(const Wrapper& other) requires Copyable<T> : value(other.value) {}
            };
        '''), [])

    def test_rejects_wrong_case_in_concepts_and_requires_parameters(self):
        for source in (
            "template<typename T> concept copyable = __is_constructible(T, const T&);",
            "template<typename T> concept Lockable = requires(T& BadLock) { BadLock.acquire(); };",
        ):
            with self.subTest(source=source):
                self.assertTrue(self.errors(source))

    def test_accepts_private_instance_and_static_state_in_class_and_struct(self):
        self.assertEqual(self.errors('''
            class Owner {
                int count_{};
                const int capacity_ = 4;
                static int current_;
            public:
                static constexpr int MAX_COUNT = 8;
                int count() const { return count_; }
            };
            int Owner::current_ = 0;
            struct Record { int value; private: int cached_value_; };
        '''), [])

    def test_accepts_local_constexpr_and_class_namespace_constants(self):
        self.assertEqual(self.errors('''
            constexpr int PAGE_BYTES = 4096;
            const int LIMIT = 8;
            void check_value(int count) {
                constexpr int header_size = 4;
                const int remaining = count - header_size;
                auto [left, right] = [] { struct Pair { int left; int right; }; return Pair{}; }();
            }
        '''), [])

    def test_does_not_treat_pointer_to_const_as_a_constant_pointer(self):
        self.assertEqual(self.errors('''
            class Owner { static const char* name_; };
            const char* Owner::name_ = nullptr;
        '''), [])

    def test_rejects_storage_prefixes_and_missing_member_suffixes(self):
        for declaration in ("int count;", "int s_count;", "int m_count_;", "static int s_current;"):
            with self.subTest(declaration=declaration):
                self.assertTrue(self.errors("class Owner { " + declaration + " };"))

    def test_rejects_public_mutable_static_state(self):
        for declaration in ("static int current;", "static int current_;"):
            self.assertTrue(self.errors("struct Owner { " + declaration + " };"))

    def test_rejects_wrong_case_for_each_declaration_role(self):
        for source in (
            "namespace BadNamespace {}", "struct bad_type {};", "enum class state { ready };",
            "using byte_type = unsigned char;", "void badFunction(int BadParameter) {}",
            "void function() { int BadLocal; }", "constexpr int page_bytes = 4096;",
            "void function() { constexpr int HEADER_BYTES = 4; }",
            "template<typename element> struct Array {};",
            "template<unsigned Capacity> struct Array {};",
            "struct Record { int Value; };",
        ):
            with self.subTest(source=source):
                self.assertTrue(self.errors(source))

    def test_protocol_aliases_are_only_allowed_on_records(self):
        self.assertEqual(self.errors("struct Container { using iterator = int; using value_type = int; };"), [])
        self.assertTrue(self.errors("using iterator = int;"))

    def test_contract_exception_is_exact_and_does_not_cover_lookalikes(self):
        allowed = {("kernel/example.cpp", "__kernel_pg_dir")}
        self.assertEqual(self.errors("int __kernel_pg_dir;", allowed), [])
        self.assertTrue(self.errors("int __other_pg_dir;", allowed))
        self.assertTrue(self.errors("namespace other { int __kernel_pg_dir; }", allowed))
        self.assertTrue(self.errors("int __kernel_pg_dir;", {("kernel/other.cpp", "__kernel_pg_dir")}))

    def test_rejects_reserved_internal_names(self):
        for source in ("void __helper() {}", "void _Helper() {}", "void check() { int __temporary; }"):
            self.assertTrue(self.errors(source))

    def test_pure_property_style_and_output_argument_retrieval(self):
        self.assertTrue(self.errors("struct Owner { int get_count() const { return 0; } };"))
        self.assertEqual(self.errors("struct Owner { void get_filename(char* buffer, unsigned size); };"), [])

    def test_acronyms_and_filenames_follow_the_project_spelling(self):
        self.assertTrue(self.errors("struct PCIDevice {};"))
        self.assertEqual(self.errors("struct PciDevice {};"), [])
        self.assertTrue(rules.source_naming_errors("", "kernel/BadFilename.cpp"))

    def test_macro_checks_include_inactive_branches_but_ignore_comments_and_strings(self):
        source = '#if 0\n#define badMacro 1\n#endif\n// #define otherBad 1\nconst char* text = "#define anotherBad 1";\n'
        errors = rules.source_naming_errors(source, "kernel/example.cpp")
        self.assertEqual(len(errors), 1)
        self.assertIn("badMacro", errors[0])
        self.assertEqual(rules.source_naming_errors("#define PAGE_BYTES 4096\n", "kernel/example.cpp"), [])

    def test_generated_declarations_are_checked_at_the_spelling_location(self):
        self.assertTrue(self.errors("#define DECLARE int badName;\nvoid check() { DECLARE }"))

    def test_arch_interfaces_describe_purpose_and_transfer_width(self):
        for name in ("arch_invlpg", "arch_read_cr3", "arch_load_ttbr0", "arch_write_satp",
                     "arch_set_rsp0", "arch_port_inb", "arch_port_insw"):
            with self.subTest(name=name):
                self.assertTrue(rules.source_naming_errors(f"void {name}();", "arch/x86/include/asm/arch.h"))
        source = "void arch_invalidate_tlb_page(); void arch_read_page_table_root(); void arch_port_read16_buffer();"
        self.assertEqual(rules.source_naming_errors(source, "kernel/example.cpp"), [])
        self.assertEqual(rules.source_naming_errors('// arch_read_cr3();\nconst char* hint = "arch_invlpg()";',
                                                    "kernel/example.cpp"), [])
        self.assertEqual(rules.source_naming_errors("void rcr3();", "arch/x86/include/asm/cpu.h"), [])

    def test_assembly_local_labels_are_distinct_from_exported_boot_contracts(self):
        path = "arch/x86/kernel/head.S"
        self.assertTrue(rules.source_naming_errors("spin:\n jmp spin\n", path))
        self.assertEqual(rules.source_naming_errors(".globl _start\n_start:\n.Lspin:\n1:\n jmp 1b\n"
                                                    "__kernel_pg_dir:\n.space 4096\n", path), [])
        self.assertEqual(rules.source_naming_errors(".global first, second\nfirst:\nsecond:\n", path), [])
        self.assertEqual(rules.source_naming_errors('// fake_label:\n.ascii "fake_label:"\n', path), [])

    def test_exception_file_rejects_wildcards_and_missing_reasons(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "exceptions.json"
            for entry in ({"path": "kernel/example.cpp", "name": "*", "reason": "ABI"},
                          {"path": "kernel/example.cpp", "name": "__abi"}):
                path.write_text(json.dumps([entry]))
                with self.assertRaises(RuntimeError):
                    rules.load_naming_exceptions(path)

    def test_exception_file_accepts_distinct_exact_contracts(self):
        entries = [
            {"path": "arch/x86/include/asm/pgtable.h", "name": "__kernel_pg_dir", "reason": "Assembly ABI"},
            {"path": "arch/aarch64/include/asm/pgtable.h", "name": "__kernel_pg_dir", "reason": "Assembly ABI"},
            {"path": "include/uefi/uefi.h", "name": "EFI_MEMORY_DESCRIPTOR::PhysicalStart", "reason": "UEFI ABI"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "exceptions.json"
            path.write_text(json.dumps(entries))
            self.assertEqual(rules.load_naming_exceptions(path), {
                (entry["path"], entry["name"]) for entry in entries
            })

    def test_exception_file_rejects_duplicate_contracts(self):
        entry = {"path": "kernel/example.cpp", "name": "__boot_symbol", "reason": "Assembly ABI"}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "exceptions.json"
            path.write_text(json.dumps([entry, dict(entry, reason="Different explanation")]))
            with self.assertRaisesRegex(RuntimeError, "Duplicate naming exception"):
                rules.load_naming_exceptions(path)

    def test_exception_file_rejects_noncanonical_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "exceptions.json"
            for relative in ("/kernel/example.cpp", "C:/kernel/example.cpp", "kernel\\example.cpp",
                             "./kernel/example.cpp", "kernel//example.cpp", "kernel/../example.cpp"):
                with self.subTest(relative=relative):
                    path.write_text(json.dumps([{"path": relative, "name": "__boot_symbol", "reason": "ABI"}]))
                    with self.assertRaisesRegex(RuntimeError, "repository-relative"):
                        rules.load_naming_exceptions(path)

    def test_exception_file_rejects_patterns_and_empty_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "exceptions.json"
            entry = {"path": "kernel/example.cpp", "name": "__boot_symbol", "reason": "Assembly ABI"}
            invalid = [[dict(entry, name=name)] for name in ("__boot_?", "__boot_[ab]")]
            invalid.extend([[dict(entry, path="kernel/*.cpp")], [dict(entry, reason="   ")],
                            [dict(entry, name=123)], [None], {"contracts": [entry]}])
            for entries in invalid:
                with self.subTest(entries=entries):
                    path.write_text(json.dumps(entries))
                    with self.assertRaises(RuntimeError):
                        rules.load_naming_exceptions(path)

    def check_unused_source(self, relative, source, selected_elsewhere=False, dependency_cache=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            for folder in ("kernel", "include/base", "arch/x86", "boot", "user", "docs"):
                (root / folder).mkdir(parents=True)
            for name in ("Makefile", "arch/x86/Makefile", "checker.py", ".clang-tidy",
                         "docs/NAMING.md", "include/base/types.h"):
                (root / name).write_text("")
            ownership = root / "ownership.json"
            exceptions = root / "naming.json"
            ownership.write_text("[]")
            exceptions.write_text("[]")
            (root / "kernel/example.cpp").write_text("class Owner { static int count_; };\n")
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            (root / relative).write_text(source)
            flags = [CXX_STANDARD_FLAG, "-I" + str(root / "include")]

            def config(arch, target="kernel-rule-config"):
                if target != "kernel-rule-config":
                    return []
                sources = ["kernel/example.cpp"]
                if selected_elsewhere and arch != "x86":
                    sources.append(relative)
                return ["clang++"], flags, sources

            with patch.object(rules, "ROOT", root), patch.object(rules, "EXCEPTIONS", ownership), \
                    patch.object(rules, "NAMING_EXCEPTIONS", exceptions), \
                    patch.object(rules, "__file__", str(root / "checker.py")), \
                    patch.object(rules, "build_config", side_effect=config), \
                    redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                if dependency_cache:
                    cache = root / "rules.sha256"
                    with patch.object(rules, "compile_ast", wraps=rules.compile_ast) as compile_ast:
                        self.assertEqual(rules.check("x86", cache), 0)
                        first_calls = compile_ast.call_count
                        self.assertEqual(rules.check("x86", cache), 0)
                        self.assertEqual(compile_ast.call_count, first_calls)
                        (root / relative).write_text(source + "\n// dependency changed\n")
                        self.assertEqual(rules.check("x86", cache), 0)
                        self.assertGreater(compile_ast.call_count, first_calls)
                    return 0
                return rules.check("x86")

    def test_unused_headers_and_dormant_sources_cannot_hide_bad_declarations(self):
        for relative in ("kernel/unused.h", "kernel/dormant.cpp"):
            with self.subTest(relative=relative):
                self.assertEqual(self.check_unused_source(relative, "struct BadRecord { int BadField; };"), 1)
                self.assertEqual(self.check_unused_source(relative, "struct GoodRecord { int good_field; };"), 0)

    def test_sources_selected_by_another_architecture_keep_their_build_context(self):
        self.assertEqual(self.check_unused_source("kernel/other_arch.cpp", '#include "other_arch_only.h"\n',
                                                  selected_elsewhere=True), 0)

    def test_unused_imported_headers_are_not_compiled_as_first_party_sources(self):
        self.assertEqual(self.check_unused_source("external/zstl/include/sys/unused.hpp",
                                                  "#error dormant imported headers need their own build context\n"), 0)

    def test_stl_dependency_changes_invalidate_cached_ast_checks(self):
        self.assertEqual(self.check_unused_source("external/zstl/include/sys/unused.hpp", "// external header\n",
                                                  dependency_cache=True), 0)


if __name__ == "__main__":
    unittest.main()
