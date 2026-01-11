#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def reverse_line_bytes(line: bytes) -> bytes:
    """Preserve EOL exactly; reverse only content before it.
    Reverse by UTF-8 code points; fallback to raw bytes if decode fails.
    """
    eol = b""
    content = line

    if line.endswith(b"\n"):
        if line.endswith(b"\r\n"):
            eol = b"\r\n"
            content = line[:-2]
        else:
            eol = b"\n"
            content = line[:-1]

    try:
        s = content.decode("utf-8")
        rev = s[::-1].encode("utf-8")
    except UnicodeDecodeError:
        rev = content[::-1]

    return rev + eol


def first_mismatch(a: bytes, b: bytes) -> int:
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return n if len(a) != len(b) else -1


def preview(x: bytes, i: int, w: int = 40) -> str:
    lo = max(0, i - w)
    hi = min(len(x), i + w)
    return (
        x[lo:hi]
        .decode("utf-8", errors="replace")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Verify that output files contain line-by-line reversed input."
    )
    ap.add_argument("input_dir", type=Path, help="Input folder")
    ap.add_argument("output_dir", type=Path, help="Output folder (expects same filenames)")
    ap.add_argument("--pattern", default="*.txt", help="Glob for input files (default: *.txt)")
    ap.add_argument("--context", type=int, default=40, help="Preview width around mismatch (default: 40)")
    args = ap.parse_args()

    in_dir: Path = args.input_dir
    out_dir: Path = args.output_dir
    pattern: str = args.pattern
    ctx: int = args.context

    if not in_dir.is_dir():
        print(f"ERROR: input_dir is not a directory: {in_dir}")
        return 2
    if not out_dir.is_dir():
        print(f"ERROR: output_dir is not a directory: {out_dir}")
        return 2

    inputs = sorted(in_dir.glob(pattern))
    if not inputs:
        print(f"No input files found in {in_dir} matching {pattern!r}")
        return 1

    files_tested = 0
    files_passed = 0
    files_skipped = 0

    total_lines_tested = 0
    total_lines_passed = 0
    total_extra_output_lines = 0

    for in_path in inputs:
        out_path = out_dir / in_path.name
        if not out_path.exists():
            files_skipped += 1
            print(f"SKIP  {in_path.name} (no matching {out_path})")
            continue

        files_tested += 1
        file_ok = True

        file_lines_tested = 0
        file_lines_passed = 0
        file_extra_output_lines = 0

        with in_path.open("rb") as fin, out_path.open("rb") as fout:
            line_no = 0

            while True:
                in_bytes_before_line = fin.tell()
                in_line = fin.readline()
                if not in_line:
                    break
                in_bytes_to_line = fin.tell()

                line_no += 1
                file_lines_tested += 1

                out_bytes_before_line = fout.tell()
                out_line = fout.readline()
                out_bytes_to_line = fout.tell()

                exp = reverse_line_bytes(in_line)

                if not out_line:
                    file_ok = False
                    print(
                        f"FAIL  {in_path.name}: line {line_no}: output ended early "
                        f"(in_bytes_before_line={in_bytes_before_line}, in_bytes_to_line={in_bytes_to_line}; "
                        f"out_bytes_before_line={out_bytes_before_line}, out_bytes_to_line={out_bytes_to_line})"
                    )
                    show_i = min(len(exp), ctx)
                    print(f"  exp: ...{preview(exp, show_i, w=ctx)}...")
                    continue

                if exp == out_line:
                    file_lines_passed += 1
                    continue

                # mismatch
                file_ok = False
                idx = first_mismatch(exp, out_line)
                show_i = idx if idx >= 0 else 0

                # absolute mismatch offsets (best-effort)
                abs_in = (in_bytes_before_line + idx) if idx >= 0 else None
                abs_out = (out_bytes_before_line + idx) if idx >= 0 else None

                abs_in_s = str(abs_in) if abs_in is not None else "n/a"
                abs_out_s = str(abs_out) if abs_out is not None else "n/a"

                print(
                    f"FAIL  {in_path.name}: line {line_no}, byte {idx} "
                    f"(in_bytes_before_line={in_bytes_before_line}, in_bytes_to_line={in_bytes_to_line}, abs_in_byte={abs_in_s}; "
                    f"out_bytes_before_line={out_bytes_before_line}, out_bytes_to_line={out_bytes_to_line}, abs_out_byte={abs_out_s})"
                )
                print(f"  exp: ...{preview(exp, show_i, w=ctx)}...")
                print(f"  out: ...{preview(out_line, show_i, w=ctx)}...")

            # extra output lines after input EOF
            extra_line_no = line_no + 1
            in_eof = fin.tell()
            while True:
                out_before = fout.tell()
                extra = fout.readline()
                if not extra:
                    break
                out_after = fout.tell()

                file_ok = False
                file_extra_output_lines += 1
                print(
                    f"FAIL  {in_path.name}: extra output line {extra_line_no} "
                    f"(in_bytes_before_line={in_eof}, in_bytes_to_line={in_eof}; "
                    f"out_bytes_before_line={out_before}, out_bytes_to_line={out_after})"
                )
                extra_line_no += 1

        total_lines_tested += file_lines_tested
        total_lines_passed += file_lines_passed
        total_extra_output_lines += file_extra_output_lines

        if file_ok:
            files_passed += 1
            print(f"FILE  PASS  {in_path.name}  (lines {file_lines_passed}/{file_lines_tested})")
        else:
            print(
                f"FILE  FAIL  {in_path.name}  "
                f"(lines {file_lines_passed}/{file_lines_tested}, extra_out_lines={file_extra_output_lines})"
            )

    print("\n=== Summary ===")
    print(f"Files: {files_passed}/{files_tested} passed (skipped {files_skipped})")
    print(f"Lines: {total_lines_passed}/{total_lines_tested} matched")
    if total_extra_output_lines:
        print(f"Extra output lines (beyond input EOF): {total_extra_output_lines}")

    return 0 if files_passed == files_tested else 2


if __name__ == "__main__":
    raise SystemExit(main())
