#!/usr/bin/env python3
"""Enforce architecture boundaries, state ownership, naming and page table tests."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCEPTIONS = ROOT / "scripts/kernel_rule_exceptions.json"
NAMING_EXCEPTIONS = ROOT / "scripts/naming_exceptions.json"
SHARED = ("kernel", "include/base", "include/kernel")
SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cc", ".cxx", ".S"}
ARCH_TOKEN = re.compile(
    r"\b(?:__(?:riscv\w*|aarch64\w*|arm\w*|x86\w*|i[3-6]86\w*|"
    r"amd64\w*|mips\w*|powerpc\w*)|_M_(?:X64|IX86|ARM\w*)|"
    r"ARCH(?:_\w+)?|CONFIG_(?:X86\w*|ARM\w*|RISCV\w*)|BOARD_\w+)\b"
)
# Preserve line numbers, and do not interpret comments or strings as directives.
COMMENT_OR_STRING = re.compile(
    r'/\*.*?\*/|//[^\n]*|R"([^ ()\\\t\r\n]*)\(.*?\)\1"|'
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S
)
DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)\b((?:[^\n]*\\\n)*[^\n]*)", re.M)


def code_only(source):
    return COMMENT_OR_STRING.sub(
        lambda match: "".join("\n" if c == "\n" else " " for c in match.group()), source
    )


def architecture_errors(source, path):
    code = code_only(source)
    errors = []
    for match in DIRECTIVE.finditer(code):
        if match[1] in {"if", "elif", "ifdef", "ifndef", "define", "undef"}:
            token = ARCH_TOKEN.search(match[2].replace("\\\n", ""))
            if token:
                line = code.count("\n", 0, match.start()) + 1
                errors.append(f"{path}:{line}: architecture selection {token[0]} belongs in arch/<arch>/")
    for match in re.finditer(r"\b(?:asm|__asm|__asm__)\s*(?:(?:volatile|__volatile__)\s*)?\(", code):
        line = code.count("\n", 0, match.start()) + 1
        errors.append(f"{path}:{line}: inline assembly belongs in arch/<arch>/")
    return errors


def immutable(node):
    typ = node.get("type", {})
    name = typ.get("desugaredQualType", typ.get("qualType", ""))
    if "&" in name:
        return False
    if "*" not in name:
        return node.get("constexpr", False) or name.startswith("const ")
    # Immutable function pointers and pointers to immutable data are permitted.
    if re.search(r"\(\*\s*const\s*\)", name):
        return True
    parts = name.split("*")
    return parts[0].strip().startswith("const ") and all(
        re.match(r"^const\b", part.strip()) for part in parts[1:]
    )


def resolve_locations(ast):
    # Clang elides repeated filenames across ALL serialized locations, including
    # statement ranges and macro expansions. Resolve them in JSON field order.
    current_file = None

    def resolve(value):
        nonlocal current_file
        if isinstance(value, dict):
            if "offset" in value:
                if "file" in value:
                    current_file = value["file"]
                value["_file"] = current_file
            for key, child in list(value.items()):
                if key not in {"includedFrom", "_file"}:
                    resolve(child)
        elif isinstance(value, list):
            for child in value:
                resolve(child)

    resolve(ast)


def persistent_state(ast, root):
    """Use declaration contexts, not declaration-shaped source regexes."""
    records = set()

    def collect(node):
        if node.get("kind") in {"CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"}:
            records.add(node.get("id"))
        for child in node.get("inner", []):
            collect(child)

    collect(ast)
    resolve_locations(ast)
    found = {}

    def visit(node, contexts):
        loc = node.get("loc", {})
        loc = loc.get("expansionLoc", loc)
        source_file = loc.get("_file")
        kind = node.get("kind")
        if kind == "VarDecl" and loc and not node.get("isImplicit"):
            is_member = (contexts and contexts[-1][0] in {
                "CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"
            }) or node.get("parentDeclContextId") in records
            in_function = any(k in {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl",
                                   "CXXDestructorDecl", "CXXConversionDecl", "LambdaExpr"}
                              for k, _ in contexts)
            persistent = not in_function or node.get("storageClass") in {"static", "extern"} or node.get("tls")
            if persistent and not is_member and not immutable(node) and source_file:
                absolute = (root / source_file).resolve()
                try:
                    path = absolute.relative_to(root).as_posix()
                except ValueError:
                    path = ""
                if (path.startswith("kernel/") and not path.startswith("kernel/test/")) or (
                    path.startswith("arch/") and ("/kernel/" in path or "/include/asm/" in path)
                ) or path.startswith(("include/base/", "include/kernel/")):
                    scopes = [name for _, name in contexts if name]
                    name = "::".join(scopes + [node.get("name", "<unnamed>")])
                    # Anonymous namespaces still have no owning class.
                    key = f"{path}:{name}"
                    line = loc.get("line")
                    if line is None:
                        line = absolute.read_bytes()[:loc["offset"]].count(b"\n") + 1
                    found[key] = {"path": path, "name": name, "line": line}
        next_contexts = contexts
        if kind in {"NamespaceDecl", "CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl",
                    "FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl",
                    "CXXConversionDecl", "LambdaExpr"}:
            next_contexts = contexts + [(kind, node.get("name", ""))]
        for child in node.get("inner", []):
            visit(child, next_contexts)

    visit(ast, [])
    return found


def compile_ast(source, compiler, flags, root):
    result = subprocess.run(
        compiler + flags + [f"-I{Path(source).parent}", "-Xclang", "-ast-dump=json",
                            "-fsyntax-only", str(source)],
        cwd=root, capture_output=True, text=True
    )
    if result.returncode:
        raise RuntimeError(f"AST check failed for {source}:\n{result.stderr}")
    return json.loads(result.stdout)


def compile_state(source, compiler, flags, root=ROOT):
    return persistent_state(compile_ast(source, compiler, flags, root), root)


SNAKE_CASE = re.compile(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*\Z")
PASCAL_CASE = re.compile(r"[A-Z](?:[a-z0-9]+(?:[A-Z][a-z0-9]+)*)?\Z")
UPPER_CASE = re.compile(r"[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)*\Z")
ARCH_INTERFACE_REGISTER = re.compile(
    r"(?:^|_)(?:cr[0-9]+|ttbr[0-9]*|satp|rsp0|esp|invlpg|"
    r"in[blw]|out[blw]|ins[wl]|outs[wl])(?:_|$)"
)
PROTOCOL_ALIASES = {"iterator", "const_iterator", "reverse_iterator", "const_reverse_iterator",
                    "value_type", "size_type", "difference_type", "pointer", "const_pointer",
                    "reference", "const_reference"}


def load_naming_exceptions(path):
    allowed = set()
    entries = json.loads(path.read_text())
    if not isinstance(entries, list):
        raise RuntimeError("Naming exceptions must be a list of exact contracts")
    for item in entries:
        if not isinstance(item, dict) or not all(
                isinstance(item.get(key), str) and item[key].strip() for key in ("reason", "name", "path")):
            raise RuntimeError("Naming exceptions require an exact path, name and contract explanation")
        if any(char in item[key] for key in ("name", "path") for char in "*?[]"):
            raise RuntimeError("Naming exceptions must not exempt entire files or name patterns")
        relative = PurePosixPath(item["path"])
        if (relative.is_absolute() or relative.as_posix() != item["path"] or ".." in relative.parts
                or "\\" in item["path"] or ":" in item["path"]):
            raise RuntimeError("Naming exception paths must be canonical repository-relative paths")
        key = (item["path"], item["name"])
        if key in allowed:
            raise RuntimeError(f"Duplicate naming exception: {item['path']}:{item['name']}")
        allowed.add(key)
    return allowed


def naming_errors(ast, root, allowed=()):
    """Classify declarations by their AST context, access and semantic role."""
    resolve_locations(ast)
    errors = {}
    records = {}
    member_access = {}
    files = {}

    def collect(node, scope=()):
        kind = node.get("kind")
        if kind in {"NamespaceDecl", "CXXRecordDecl", "RecordDecl",
                      "ClassTemplateSpecializationDecl", "EnumDecl"}:
            scope = scope + ((node["name"],) if node.get("name") else ())
            records[node.get("id")] = (scope, node.get("tagUsed", "struct"))
        if kind in {"CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"}:
            access = "private" if node.get("tagUsed") == "class" else "public"
            # Resolve declaration access in source order, including out-of-line definitions.
            for sibling in node.get("inner", []):
                if sibling.get("kind") == "AccessSpecDecl":
                    access = sibling["access"]
                elif sibling.get("kind") in {"FieldDecl", "VarDecl"}:
                    member_access[sibling.get("id")] = access
        for child in node.get("inner", []):
            collect(child, scope)

    collect(ast)

    def visit(node, scope=(), record=None, access="public", in_function=False):
        kind = node.get("kind")
        name = node.get("name", "")
        loc = node.get("loc", {})
        loc = loc.get("spellingLoc", loc)
        source = loc.get("_file")
        expected = None
        access = member_access.get(node.get("previousDecl"), node.get("access", access))
        member = kind == "FieldDecl" or (kind == "VarDecl" and (
            record is not None and not in_function or node.get("parentDeclContextId") in records))
        if kind in {"NamespaceDecl", "FunctionDecl", "CXXMethodDecl", "ParmVarDecl"}:
            expected = "snake_case"
        elif kind in {"CXXRecordDecl", "RecordDecl", "EnumDecl", "TypeAliasDecl", "TypedefDecl",
                      "TemplateTypeParmDecl", "TemplateTemplateParmDecl", "ConceptDecl"}:
            expected = "PascalCase"
        elif kind == "EnumConstantDecl":
            expected = "PascalCase"
        elif kind == "NonTypeTemplateParmDecl":
            expected = "UPPER_CASE"
        elif kind in {"VarDecl", "FieldDecl", "BindingDecl"}:
            constant = immutable(node)
            if kind == "VarDecl" and constant and not in_function:
                expected = "UPPER_CASE"
            elif member and access in {"private", "protected"}:
                expected = "snake_case_"
            else:
                expected = "snake_case"
        declaration_scope = scope
        if node.get("parentDeclContextId") in records:
            declaration_scope = records[node["parentDeclContextId"]][0]
        qualified = "::".join(declaration_scope + (name,))
        if expected and name and source and not node.get("isImplicit") and not name.startswith("operator"):
            if source not in files:
                absolute = (root / source).resolve()
                try:
                    path = absolute.relative_to(root).as_posix()
                except ValueError:
                    path = ""
                files[source] = (absolute, path)
            absolute, path = files[source]
            if path.startswith(("kernel/", "include/", "arch/", "boot/", "user/")) and not path.startswith("user/zcc/"):
                exempt = (path, qualified) in allowed
                if kind in {"TypeAliasDecl", "TypedefDecl"} and record and name in PROTOCOL_ALIASES:
                    exempt = True
                pattern = {"snake_case": SNAKE_CASE, "PascalCase": PASCAL_CASE,
                           "UPPER_CASE": UPPER_CASE, "snake_case_": SNAKE_CASE}[expected]
                candidate = name[:-1] if expected == "snake_case_" and name.endswith("_") else name
                valid = bool(pattern.fullmatch(candidate)) and (expected != "snake_case_" or name.endswith("_"))
                if "__" in name or name.startswith("_"):
                    valid = False
                if member and name.startswith(("s_", "m_", "g_")):
                    valid = False
                if member and kind == "VarDecl" and not constant and access != "private":
                    valid = False
                    expected = "private snake_case_ static state"
                if kind == "CXXMethodDecl" and name.startswith("get_") and not any(
                        child.get("kind") == "ParmVarDecl" for child in node.get("inner", [])):
                    valid = False
                    expected = "a bare property name (zero-argument getter)"
                if not exempt and not valid:
                    line = loc.get("line") or absolute.read_bytes()[:loc["offset"]].count(b"\n") + 1
                    key = (path, loc["offset"], name)
                    errors[key] = f"{path}:{line}: {qualified}: use {expected} (docs/NAMING.md)"
        child_scope = scope
        child_record = record
        if kind in {"NamespaceDecl", "CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl",
                    "EnumDecl", "FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl"}:
            child_scope = scope + ((name,) if name else ())
            if kind in {"CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"}:
                child_record = node.get("id")
                access = "private" if node.get("tagUsed") == "class" else "public"
        child_function = in_function or kind in {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl",
                                                "CXXDestructorDecl", "CXXConversionDecl", "LambdaExpr"}
        if kind in {"CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"}:
            child_function = False
        for child in node.get("inner", []):
            if child.get("kind") == "AccessSpecDecl":
                access = child["access"]
            visit(child, child_scope, child_record, access, child_function)

    visit(ast)
    return sorted(errors.values())


def source_naming_errors(source, path, allowed=()):
    errors = []
    if not SNAKE_CASE.fullmatch(Path(path).stem):
        errors.append(f"{path}: source filename must use snake_case (docs/NAMING.md)")
    code = code_only(source)
    for match in re.finditer(r"\b(arch_[a-z0-9_]+)\s*\(", code):
        if ARCH_INTERFACE_REGISTER.search(match[1]) and (path, match[1]) not in allowed:
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{path}:{line}: {match[1]}: name arch interfaces by purpose, not ISA registers/instructions")
    if Path(path).suffix == ".S":
        exported = set()
        for match in re.finditer(r"^[ \t]*\.(?:globl|global)[ \t]+([^\n#]+)", code, re.M):
            exported.update(re.findall(r"[A-Za-z_]\w*", match[1]))
        for match in re.finditer(r"^[ \t]*([A-Za-z_]\w*):", code, re.M):
            name = match[1]
            if name not in exported and not name.startswith("__") and (path, name) not in allowed:
                line = code.count("\n", 0, match.start()) + 1
                errors.append(f"{path}:{line}: local assembly label {name}: use .L (docs/NAMING.md)")
    for match in DIRECTIVE.finditer(code):
        if match[1] == "define":
            identifier = re.match(r"\s*([A-Za-z_]\w*)", match[2])
            if identifier and not UPPER_CASE.fullmatch(identifier[1]) and (path, identifier[1]) not in allowed:
                line = source.count("\n", 0, match.start()) + 1
                errors.append(f"{path}:{line}: macro {identifier[1]}: use UPPER_CASE (docs/NAMING.md)")
    return errors


def declared_source_files(ast, root):
    """Track headers already checked with their real translation unit context."""
    found = set()
    paths = {}

    def visit(node):
        loc = node.get("loc", {})
        loc = loc.get("spellingLoc", loc)
        source = loc.get("_file")
        if source:
            if source not in paths:
                absolute = (root / source).resolve()
                paths[source] = absolute.relative_to(root).as_posix() if absolute.is_relative_to(root) else ""
            if paths[source]:
                found.add(paths[source])
        for child in node.get("inner", []):
            visit(child)

    visit(ast)
    return found


def page_table_errors(ast, root):
    """Inspect real expressions; named macro masks retain expansion locations."""
    resolve_locations(ast)
    errors = set()

    def unwrapped(node):
        while node.get("kind") in {"ParenExpr", "ImplicitCastExpr"} and node.get("inner"):
            node = node["inner"][0]
        return node

    def numeric_mask(node):
        if node.get("kind") == "IntegerLiteral":
            # An expanded named mask is allowed; a literal at the use site is not.
            expansion = node.get("range", {}).get("begin", {}).get("expansionLoc")
            return expansion is None or expansion.get("isMacroArgExpansion", False)
        return any(numeric_mask(child) for child in node.get("inner", []))

    def report(node, message):
        loc = node.get("range", {}).get("begin", {})
        loc = loc.get("expansionLoc", loc)
        if not loc.get("_file"):
            return
        absolute = (root / loc["_file"]).resolve()
        try:
            path = absolute.relative_to(root).as_posix()
        except ValueError:
            return
        if not (path.startswith("arch/") and path.endswith("/include/asm/page.h")):
            return
        line = loc.get("line") or absolute.read_bytes()[:loc["offset"]].count(b"\n") + 1
        errors.add(f"{path}:{line}: {message}")

    def visit(node, in_helper=False):
        if node.get("kind") == "FunctionDecl":
            name = node.get("name", "")
            in_helper = name.startswith(("pte_", "pde_")) or name in {
                "user_page_perm", "merge_user_page_perm"
            }
        if in_helper:
            if node.get("kind") == "BinaryOperator" and node.get("opcode") == "&":
                if any(numeric_mask(child) for child in node.get("inner", [])):
                    report(node, "page table masks must have semantic names")
            if node.get("kind") == "BinaryOperator" and node.get("opcode") in {"<<", ">>"}:
                if numeric_mask(node["inner"][1]):
                    report(node, "page table bit positions must have semantic names")
            if node.get("kind") == "ImplicitCastExpr" and node.get("castKind") == "IntegralToBoolean":
                value = unwrapped(node["inner"][0])
                if value.get("kind") == "BinaryOperator" and value.get("opcode") == "&":
                    report(node, "page table bit tests must compare explicitly with zero or a named encoding")
        for child in node.get("inner", []):
            visit(child, in_helper)

    visit(ast)
    return sorted(errors)


def build_config(arch, target="kernel-rule-config"):
    env = dict(os.environ, MAKEFLAGS="", MFLAGS="")
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", f"ARCH={arch}", "TEST=1", target],
        cwd=ROOT, env=env, check=True, capture_output=True, text=True
    )
    lines = result.stdout.splitlines()
    configs = [(shlex.split(lines[i]), shlex.split(lines[i + 1]), shlex.split(lines[i + 2]))
               for i in range(0, len(lines), 3)]
    return configs[0] if target == "kernel-rule-config" else configs


def check(arch, cache=None):
    compiler, flags, sources = build_config(arch)
    boot_configs = build_config(arch, "naming-boot-config")
    user_configs = build_config(arch, "naming-user-config")
    selected_elsewhere = {source for other in ("x86", "aarch64", "riscv64") if other != arch
                          for source in build_config(other)[2]} - set(sources)
    inputs = sorted(p for directory in ("kernel", "include", "arch", "boot", "user")
                    for p in (ROOT / directory).rglob("*") if p.suffix in SOURCE_SUFFIXES)
    inputs = [p for p in inputs if not p.is_relative_to(ROOT / "user/zcc")]
    inputs += [Path(__file__), EXCEPTIONS, NAMING_EXCEPTIONS, ROOT / ".clang-tidy",
               ROOT / "docs/NAMING.md", ROOT / "Makefile", ROOT / f"arch/{arch}/Makefile"]
    digest = hashlib.sha256(json.dumps([compiler, flags, sources, boot_configs, user_configs,
                                      sorted(selected_elsewhere)]).encode())
    digest.update(subprocess.check_output(compiler + ["--version"]))
    for path in inputs:
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest()
    if cache and cache.exists() and cache.read_text().strip() == fingerprint:
        print(f"Kernel rules ({arch}): OK (unchanged inputs)")
        return 0

    errors = []
    naming_allowed = load_naming_exceptions(NAMING_EXCEPTIONS)
    for path in inputs:
        if path.suffix in SOURCE_SUFFIXES:
            errors.extend(source_naming_errors(path.read_text(), path.relative_to(ROOT).as_posix(), naming_allowed))
        if any(path.is_relative_to(ROOT / directory) for directory in SHARED):
            errors.extend(architecture_errors(path.read_text(), path.relative_to(ROOT).as_posix()))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    page_header = ROOT / f"arch/{arch}/include/asm/page.h"
    if page_header.exists():
        errors.extend(page_table_errors(
            compile_ast(page_header, compiler, flags + ["-x", "c++"], ROOT), ROOT
        ))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    allowed = json.loads(EXCEPTIONS.read_text())
    allowed_keys = set()
    for item in allowed:
        if not item.get("reason"):
            raise RuntimeError("Every kernel rule exception needs an ownership/ABI explanation")
        allowed_keys.add(f"{item['path']}:{item['name']}")
    state = {}
    covered = set()
    for source in sources:
        ast = compile_ast(source, compiler, flags, ROOT)
        errors.extend(naming_errors(ast, ROOT, naming_allowed))
        covered.update(declared_source_files(ast, ROOT))
        covered.add(source)
        if not source.startswith("kernel/test/"):
            state.update(persistent_state(ast, ROOT))
    for boot_compiler, boot_flags, boot_sources in boot_configs + user_configs:
        for source in boot_sources:
            ast = compile_ast(source, boot_compiler, boot_flags, ROOT)
            errors.extend(naming_errors(ast, ROOT, naming_allowed))
            covered.update(declared_source_files(ast, ROOT))
            covered.add(source)
    # Dormant sources and otherwise unused headers must follow the same naming
    # convention. Keep their compilation context separate from persistent state
    # ownership, which is checked on the actual kernel build above.
    for path in inputs:
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix not in {".cpp", ".cc", ".cxx", ".h", ".hpp"} or relative in covered:
            continue
        if relative in selected_elsewhere:
            continue
        if relative.startswith("arch/") and not relative.startswith(f"arch/{arch}/"):
            continue
        if relative.startswith(("scripts/", "user/")):
            continue
        extra_compiler, extra_flags = compiler, flags + ["-include", "base/types.h", "-x", "c++"]
        if relative.startswith(("boot/", "include/boot/", "include/uefi/", f"arch/{arch}/boot/")):
            config = next((config for config in boot_configs if "/bios/" in relative and any(
                "/bios/" in source for source in config[2])), boot_configs[-1])
            extra_compiler, extra_flags = config[0], config[1] + ["-include", "base/types.h", "-x", "c++"]
        ast = compile_ast(path, extra_compiler, extra_flags, ROOT)
        errors.extend(naming_errors(ast, ROOT, naming_allowed))
        covered.update(declared_source_files(ast, ROOT))
    for key, item in sorted(state.items()):
        if key not in allowed_keys:
            errors.append(f"{item['path']}:{item['line']}: {item['name']}: mutable persistent state "
                          "must be a member of its owning class")
    if errors:
        print("\n".join(sorted(set(errors))), file=sys.stderr)
        return 1
    if cache:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(fingerprint + "\n")
    print(f"Kernel rules ({arch}): OK")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x86", "aarch64", "riscv64"), default="x86")
    parser.add_argument("--cache", type=Path)
    args = parser.parse_args()
    try:
        return check(args.arch, args.cache)
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
