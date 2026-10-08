#!/usr/bin/env python3
"""Enforce architecture boundaries, state ownership and page table bit tests."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCEPTIONS = ROOT / "scripts/kernel_rule_exceptions.json"
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


def build_config(arch):
    env = dict(os.environ, MAKEFLAGS="", MFLAGS="")
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", f"ARCH={arch}", "TEST=1", "kernel-rule-config"],
        cwd=ROOT, env=env, check=True, capture_output=True, text=True
    )
    compiler, flags, sources = result.stdout.strip().splitlines()
    return shlex.split(compiler), shlex.split(flags), shlex.split(sources)


def check(arch, cache=None):
    compiler, flags, sources = build_config(arch)
    inputs = sorted(p for directory in ("kernel", "include", "arch")
                    for p in (ROOT / directory).rglob("*") if p.suffix in SOURCE_SUFFIXES)
    inputs += [Path(__file__), EXCEPTIONS, ROOT / "Makefile", ROOT / f"arch/{arch}/Makefile"]
    digest = hashlib.sha256(json.dumps([compiler, flags, sources]).encode())
    for path in inputs:
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest()
    if cache and cache.exists() and cache.read_text().strip() == fingerprint:
        print(f"Kernel rules ({arch}): OK (unchanged inputs)")
        return 0

    errors = []
    for path in inputs:
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
    for source in sources:
        if not source.startswith("kernel/test/"):
            state.update(compile_state(source, compiler, flags))
    for key, item in sorted(state.items()):
        if key not in allowed_keys:
            errors.append(f"{item['path']}:{item['line']}: {item['name']}: mutable persistent state "
                          "must be a member of its owning class")
    if errors:
        print("\n".join(errors), file=sys.stderr)
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
