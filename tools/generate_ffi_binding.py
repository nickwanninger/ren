#!/usr/bin/env python3
import argparse, os, re, shutil, subprocess, sys, tempfile
from typing import List, Tuple, Optional, Dict


def which_cc(preferred: Optional[str]) -> str:
    if preferred and shutil.which(preferred):
        return preferred
    for cc in ("clang", "gcc", "cc"):
        if shutil.which(cc):
            return cc
    sys.exit("error: no C compiler found (tried clang, gcc, cc)")


def run(cmd: List[str], data: bytes = None) -> Tuple[int, bytes, bytes]:
    p = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE if data is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = p.communicate(data)
    return p.returncode, out, err


# ----------------- scrubbing / normalization -----------------
SCRUB_PATTERNS = [
    (r"__attribute__\s*\(\(.*?\)\)", ""),
    (r"__declspec\s*\([^\)]*\)", ""),
    (r"__forceinline|__inline__|__inline", "inline"),
    (r"__restrict__|__restrict", "restrict"),
    (r"__extension__", ""),
    (r"__volatile__", "volatile"),
    (r"\b__stdcall\b|\b__cdecl\b|\b__fastcall\b|\b__thiscall\b", ""),
    (r'extern\s+"C"\s*', "extern "),
    (r"_Static_assert\s*\([^;]*\)\s*;", ";"),
    (r"_Noreturn\s+|_Thread_local\s+", ""),
    (r"_Atomic\s*\(", "("),
    (r"\b_Atomic\b", ""),
    (r"__attribute__\s*\(\s*\(\s*vector_size\s*\([^)]*\)\s*\)\s*\)", ""),
    (r"\b_ExtInt\s*\([^)]*\)", "int"),
    (r"__asm__\s*\([^)]*\)|\basm\s*\([^)]*\)", ""),
    (r"\b__int128\b", "long long"),
    (r"\b__uint128_t\b", "unsigned long long"),
    (r"\b__float128\b|\b_Float128\b", "long double"),
    (r"\b_Float64\b", "double"),
    (r"\b_Float32\b|\b_Float16\b", "float"),
    (
        r"\b__builtin_va_list\b|\b__gnuc_va_list\b|\b__va_list\b|\b__va_list_tag\b",
        "void *",
    ),
    (r"\b__(ptr32|ptr64)\b", ""),
    (r"\b__int64\b", "long long"),
    # Broad SAL-ish annotations (tweak if it nukes real ids in your codebase)
    (r"\b_[A-Z][A-Za-z0-9_]*_[A-Za-z0-9_]*\b", ""),
]


def scrub(text: str) -> str:
    s = text
    flags = re.DOTALL | re.MULTILINE
    for pat, repl in SCRUB_PATTERNS:
        s = re.sub(pat, repl, s, flags=flags)
    s = re.sub(r"__attribute__\s*\(\s*\(\s*\)\s*\)", "", s, flags=flags)
    s = re.sub(r"\btypeof(_unqual)?\s*\([^)]*\)", "int", s, flags=flags)
    # strip any remaining preprocessor lines defensively
    s = "\n".join(line for line in s.splitlines() if not line.lstrip().startswith("#"))
    return s


# ----------------- “no includes” preprocess -----------------
def read_and_strip_includes(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        src = f.read()
    # Remove #include and #include_next lines entirely
    out_lines = []
    for line in src.splitlines():
        if re.match(r"^\s*#\s*include(_next)?\b", line):
            continue
        out_lines.append(line)
    return "\n".join(out_lines)


import subprocess


def clang_format_code(code: str) -> str:

    custom_style = "{BasedOnStyle: google, ColumnLimit: 4000, IndentWidth: 4, BinPackParameters: true}"

    result = subprocess.run(
        ["clang-format", f"--style={custom_style}"],
        input=code.encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    if result.returncode != 0:
        raise RuntimeError(
            f"clang-format failed: {result.stderr.decode('utf-8', errors='ignore')}"
        )

    return result.stdout.decode("utf-8")


def preprocess_text(
    cc: str, text: str, include: List[str], defines: List[str], undefines: List[str]
) -> str:
    """
    Run the system preprocessor on the given text via stdin,
    but since we removed includes, it won’t expand any.
    """
    cmd = [cc, "-E", "-P", "-x", "c", "-"]  # read from stdin
    for inc in include:
        cmd.extend(["-I", inc])
    for d in defines:
        cmd.append(f"-D{d}")
    for u in undefines:
        cmd.append(f"-U{u}")
    # normalize a few impl-only tokens early (helps if they appear in conditions)
    cmd += [
        "-D__builtin_va_list=void*",
        "-D__gnuc_va_list=void*",
        "-D__uint128_t=unsigned long long",
        "-D__int128=long long",
        "-D__float128=long double",
        "-D__bool_true_false_are_defined=1",
    ]
    rc, out, err = run(cmd, data=text.encode("utf-8"))
    if rc != 0:
        sys.stderr.write(err.decode("utf-8", "ignore"))
        sys.exit(f"error: preprocessing failed ({rc})")
    # strip any residual directives (belt-and-suspenders)
    pp = out.decode("utf-8", "ignore")
    pp = "\n".join(l for l in pp.splitlines() if not l.lstrip().startswith("#"))
    return pp


def collect_defines_from_text(
    cc: str, text: str, include: List[str], defines: List[str], undefines: List[str]
) -> Dict[str, str]:
    """
    Dump macros from just this text (no includes) using -dM with stdin.
    """
    cmd = [cc, "-dM", "-E", "-P", "-x", "c", "-"]
    for inc in include:
        cmd.extend(["-I", inc])
    for d in defines:
        cmd.append(f"-D{d}")
    for u in undefines:
        cmd.append(f"-U{u}")
    rc, out, err = run(cmd, data=text.encode("utf-8"))
    if rc != 0:
        sys.stderr.write(err.decode("utf-8", "ignore"))
        return {}
    macros = {}
    for line in out.decode("utf-8", "ignore").splitlines():
        if not line.startswith("#define "):
            continue
        parts = line.split(maxsplit=2)
        if len(parts) == 2:
            name, val = parts[1], "1"
        elif len(parts) == 3:
            name, val = parts[1], parts[2]
        else:
            continue
        if "(" in name and ")" in name:  # function-like
            continue
        if name.startswith("__") and name.endswith("__"):
            continue
        v = val.strip()
        if (
            re.fullmatch(r"[-+]?(\d+|0x[0-9A-Fa-f]+)[uUlL]*", v)
            or re.fullmatch(r"\'(\\.|.)\'", v)
            or re.fullmatch(r'"([^"\\]|\\.)*"', v)
            or re.fullmatch(
                r"\(?\s*[-+]?(\d+|0x[0-9A-Fa-f]+)\s*([<>|&+\-*/]\s*[-+]?(?:\d+|0x[0-9A-Fa-f]+)\s*)+\)?[uUlL]*",
                v,
            )
            or re.fullmatch(
                r"\(\s*[a-zA-Z_][\w\s\*]*\)\s*[-+]?(?:\d+|0x[0-9A-Fa-f]+)[uUlL]*", v
            )
        ):
            macros[name] = v if v != "" else "0"
    return macros


# ----------------- CLI -----------------
def main():
    ap = argparse.ArgumentParser(
        description="Generate LuaJIT FFI cdef from a C header without expanding #includes."
    )
    ap.add_argument("header", help="Path to header file")
    ap.add_argument(
        "-I",
        dest="includes",
        action="append",
        default=[],
        help="Add include directory (still used for macros/conditions in this file)",
    )
    ap.add_argument(
        "-D",
        dest="defines",
        action="append",
        default=[],
        help="Predefine macro (NAME or NAME=VALUE)",
    )
    ap.add_argument(
        "-U", dest="undefines", action="append", default=[], help="Undefine macro"
    )
    ap.add_argument("--cc", default=None, help="Preprocessor to use (clang/gcc)")
    ap.add_argument(
        "--no-macros", action="store_true", help="Skip emitting constant macros"
    )
    ap.add_argument("--out", default=None, help="Write output to file")
    args = ap.parse_args()

    cc = which_cc(args.cc)

    raw = read_and_strip_includes(os.path.abspath(args.header))
    # macros = collect_defines_from_text(cc, raw, args.includes, args.defines, args.undefines) if not args.no_macros else {}
    # print(macros)
    pre = preprocess_text(cc, raw, args.includes, args.defines, args.undefines)
    pre = scrub(pre)
    # run clang-format over the pre, just so its nice.
    pre = clang_format_code(pre)

    print(f"""
-- Auto-generated FFI binding for {os.path.basename(args.header)}
local ffi = require 'ffi'
ffi.cdef[[
{pre}
]]
""")

    # print(pre)


if __name__ == "__main__":
    main()
