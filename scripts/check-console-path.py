#!/usr/bin/env python3
"""Architecture checks for the kebab console write path.

docs/BRING-UP-STATE.md 76 audited the path 0145 added:

    write(2) -> cnwrite() -> kebab_console_write() -> console_write()
             -> _cnputc() -> kebab_usb_log() + kebab_early_putc()

and could not find a deadlock.  The audit depends on five structural
properties that are easy to lose in a later patch and impossible to test
without the board:

  1. kernel printf must not travel through the console character device
     (cnwrite() sleeps on the console ring mutex; a userland writer could
     then park the kernel's own log behind a device write);
  2. the console device layer (bsd/dev/arm/cons.c) routes into the
     character device and has no sink of its own;
  3. the userland write path goes through console_write() and never
     drains the USB ring synchronously;
  4. the read path returns EOF in kernel context, so a reader cannot
     block or spin inside the kernel;
  5. the write path refuses an interrupt-masked context, which is the
     state of the USB transmit gate and of an interrupt handler.

check_console_path() in scripts/verify-all.sh runs this against the tree
with the whole patch series applied.  Exit status 0 = all checks pass.
"""

import argparse
import re
import sys

PRINTF_CORE = (
    "osfmk/kern/printf.c",
    "osfmk/console/serial_console.c",
    "bsd/kern/subr_prf.c",
)

CONSOLE_DEVICE = "bsd/dev/arm/cons.c"
CONSOLE_BACKEND = "bsd/dev/kebab_ufs.c"

# Symbols that only exist on the character-device side of the console.
DEVICE_SYMBOLS = re.compile(
    r"\b(cnopen|cnclose|cnread|cnwrite|cnioctl|cnselect|cndev|"
    r"kebab_console_(open|close|read|write|ioctl|select))\b")
# Calls that would put a console sink inside bsd/dev/arm/cons.c.
CONSOLE_SINK = re.compile(
    r"\b(console_write|console_write_char|console_write_unbuffered|"
    r"_cnputc|_cnputs|kebab_usb_log|kebab_usb_log_flush)\s*\(")
# Calls that would block or wait in the read path.
BLOCKING = re.compile(
    r"\b(assert_wait\w*|thread_block\w*|delay|msleep|lck_mtx_lock|"
    r"ud_sleep|while|for)\b")


class CheckError(Exception):
    pass


def strip_c(text):
    """Remove comments and literals, keeping the code (and newlines) only."""
    out = []
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if c in "\"'":
                state = c
                out.append(" ")
                i += 1
                continue
            out.append(c)
            i += 1
        elif state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
        else:  # string or char literal
            if c == "\\":
                out.append(" ")
                if i + 1 < n:
                    out.append(" " if text[i + 1] != "\n" else "\n")
                i += 2
                continue
            if c == state:
                state = "code"
            out.append("\n" if c == "\n" else " ")
            i += 1
    if state in ("block", "line", '"', "'"):
        raise CheckError("unterminated comment or literal")
    return "".join(out)


def read(tree, rel):
    path = "%s/%s" % (tree.rstrip("/"), rel)
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError as exc:
        raise CheckError("cannot read %s: %s" % (rel, exc))


def function_body(code, name):
    # The kernel style puts the return type on the line above the name.
    match = re.search(r"(?m)^" + re.escape(name) + r"\s*\(", code)
    if match is None:
        raise CheckError("function %s() not found" % name)
    start = code.find("{", match.end())
    if start < 0:
        raise CheckError("function %s() has no body" % name)
    depth = 0
    for i in range(start, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[start:i + 1]
    raise CheckError("unbalanced braces in %s()" % name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", help="xnu tree with the kebab series applied")
    args = parser.parse_args()

    failures = []

    def check(label, fn):
        try:
            detail = fn()
        except CheckError as exc:
            failures.append(label)
            print("FAIL: %s: %s" % (label, exc))
            return
        print("ok: %-14s %s" % (label, detail))

    def printf_is_independent():
        for rel in PRINTF_CORE:
            code = strip_c(read(args.tree, rel))
            hit = DEVICE_SYMBOLS.search(code)
            if hit:
                raise CheckError(
                    "%s calls the console device (%s); printf must reach the "
                    "console through console_write()/_cnputc() only"
                    % (rel, hit.group(0)))
        return "printf reaches the console without the character device"

    def device_layer_is_thin():
        code = strip_c(read(args.tree, CONSOLE_DEVICE))
        hit = CONSOLE_SINK.search(code)
        if hit:
            raise CheckError(
                "cons.c contains a console sink (%s); it must route to the "
                "character device instead" % hit.group(0))
        if not re.search(r"\bkebab_console_write\s*\(", code):
            raise CheckError(
                "cons.c no longer routes cnwrite() to kebab_console_write()")
        return "cn* entry points route to the character device, no sink"

    def write_path_is_buffered():
        code = strip_c(read(args.tree, CONSOLE_BACKEND))
        body = function_body(code, "kebab_console_write")
        if re.search(r"\bkebab_usb_log(_flush)?\s*\(", body):
            raise CheckError(
                "kebab_console_write() calls kebab_usb_log(): the userland "
                "path must not drain the USB ring")
        if not re.search(r"\bconsole_write\s*\(", body):
            raise CheckError(
                "kebab_console_write() does not go through console_write()")
        return "kebab_console_write() uses console_write(), no USB drain"

    def write_path_is_guarded():
        code = strip_c(read(args.tree, CONSOLE_BACKEND))
        body = function_body(code, "kebab_console_write")
        if "ml_get_interrupts_enabled" not in body:
            raise CheckError(
                "kebab_console_write() lost its interrupt-context guard; a "
                "write from the USB gate would panic in console_io_allowed()")
        if "EDEADLK" not in body:
            raise CheckError(
                "the console write guard no longer refuses with EDEADLK")
        return "an interrupt-masked write is refused with EDEADLK"

    def read_path_is_eof():
        code = strip_c(read(args.tree, CONSOLE_BACKEND))
        body = function_body(code, "kebab_console_read")
        hit = BLOCKING.search(body)
        if hit:
            raise CheckError(
                "kebab_console_read() contains %s: the read path must return "
                "EOF without waiting" % hit.group(0))
        if not re.fullmatch(r"\{\s*return\s+0\s*;\s*\}", body):
            raise CheckError(
                "kebab_console_read() is not a plain 'return 0' (EOF)")
        return "returns EOF with no wait or loop"

    def select_is_eof_semantics():
        code = strip_c(read(args.tree, CONSOLE_BACKEND))
        body = function_body(code, "kebab_console_select")
        if not re.search(r"\bseltrue\s*\(", body):
            raise CheckError(
                "kebab_console_select() no longer uses seltrue(); if it is made "
                "write-only, select() blocks on an fd whose read() returns EOF")
        return "seltrue(): EOF is reported as readable, like /dev/null"

    check("printf-path", printf_is_independent)
    check("console-device", device_layer_is_thin)
    check("console-write", write_path_is_buffered)
    check("console-guard", write_path_is_guarded)
    check("console-read", read_path_is_eof)
    check("console-select", select_is_eof_semantics)

    if failures:
        print("FAIL: %d console invariant(s) broken: %s"
              % (len(failures), ", ".join(failures)))
        return 1
    print("PASS: 6 console write-path invariants hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
