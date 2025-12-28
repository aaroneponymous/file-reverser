#!/usr/bin/env python3
from __future__ import annotations

import os
import random
from pathlib import Path

# -----------------------------
# Config
# -----------------------------
# Set BUF_SIZE to match your C++ BUFFSIZE when you want "boundary" files to align.
# Example:
#   BUF_SIZE=8192 python3 generate_tests.py
BUF_SIZE = int(os.environ.get("BUF_SIZE", "4096"))

MAX_LINE_TOTAL = 4096  # max bytes per line INCLUDING newline bytes, or EOF (your invariant)

OUT_DIR = Path(f"input/testsuite35_utf8_buf{BUF_SIZE}")
OUT_DIR.mkdir(parents=True, exist_ok=True)

MiB = 1024 * 1024


# -----------------------------
# Helpers
# -----------------------------
def fill_bytes(n: int, alphabet: bytes = b"abcdefghijklmnopqrstuvwxyz") -> bytes:
    if n <= 0:
        return b""
    reps, rem = divmod(n, len(alphabet))
    return alphabet * reps + alphabet[:rem]


def utf8_fill_exact_bytes(n: int, token: str, ascii_fallback: bytes = b"a") -> bytes:
    """
    Produce exactly n bytes of VALID UTF-8 by repeating `token` and then padding with ASCII.
    """
    if n <= 0:
        return b""
    tb = token.encode("utf-8")
    k = n // len(tb)
    out = tb * k
    rem = n - len(out)
    if rem:
        out += fill_bytes(rem, ascii_fallback)
    return out


def write_line(f, content: bytes, newline: bytes) -> int:
    """
    Write one line with newline bytes. Enforces MAX_LINE_TOTAL invariant.
    """
    assert len(newline) in (1, 2)
    assert len(content) + len(newline) <= MAX_LINE_TOTAL
    f.write(content)
    f.write(newline)
    return len(content) + len(newline)


def ensure_dir(p: Path) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)


def manifest_row(rows: list[str], name: str, newline: str, approx_size: int, desc: str) -> None:
    rows.append(f"{name}\t{newline}\t{approx_size}\t{desc}")


def set_file_bytes_exact(path: Path, data: bytes) -> int:
    ensure_dir(path)
    with path.open("wb") as f:
        f.write(data)
    return len(data)


def stream_write_target(
    path: Path,
    target_bytes: int,
    make_bytes,
    desc: str,
    newline_label: str,
    manifest: list[str],
) -> None:
    """
    Stream writer: repeatedly emits bytes until target_bytes reached exactly.
    `make_bytes(remaining_bytes)` must return a bytes object.
    """
    ensure_dir(path)
    written = 0
    with path.open("wb", buffering=1024 * 1024) as f:
        while written < target_bytes:
            remaining = target_bytes - written
            chunk = make_bytes(remaining)
            if not chunk:
                raise RuntimeError(f"make_bytes returned empty at written={written}")
            f.write(chunk)
            written += len(chunk)
    manifest_row(manifest, path.name, newline_label, written, desc)


def advance_to(f, written: int, target_written: int, newline: bytes) -> int:
    """
    Advance the file position from `written` to `target_written` using valid lines terminated by `newline`,
    respecting MAX_LINE_TOTAL. Works for LF and CRLF.
    """
    assert target_written >= written
    nl_len = len(newline)
    max_content = MAX_LINE_TOTAL - nl_len  # 4095 for LF, 4094 for CRLF
    remaining = target_written - written

    while remaining > 0:
        if remaining > MAX_LINE_TOTAL:
            step = MAX_LINE_TOTAL
            # CRLF: avoid leaving a remainder of 1 (can't form a 1-byte CRLF line)
            if nl_len == 2 and (remaining - step) == 1:
                step = MAX_LINE_TOTAL - 1  # 4095 leaves remainder 2

            content_len = step - nl_len
            assert 0 <= content_len <= max_content
            written += write_line(f, fill_bytes(content_len, b"P"), newline)
            remaining = target_written - written
            continue

        # remaining <= MAX_LINE_TOTAL
        if remaining < nl_len:
            # LF: nl_len=1 => remaining<1 impossible
            # CRLF: nl_len=2 => remaining==1 is the only bad case; we avoid above.
            raise RuntimeError(f"Cannot advance by {remaining} bytes with newline length {nl_len}")

        content_len = remaining - nl_len
        assert 0 <= content_len <= max_content
        written += write_line(f, fill_bytes(content_len, b"Q"), newline)
        break

    assert written == target_written
    return written


# -----------------------------
# Boundary builders (absolute offsets)
# -----------------------------
def make_newline_at_offset_lf(path: Path, newline_at: int, desc: str, manifest: list[str]) -> None:
    """
    Create an LF file where a line ends with '\\n' exactly at absolute index newline_at.
    """
    ensure_dir(path)
    with path.open("wb") as f:
        written = 0
        max_content = MAX_LINE_TOTAL - 1  # 4095
        # We need content_len = newline_at - written <= 4095 => written >= newline_at - 4095
        target_written = max(0, newline_at - max_content)
        written = advance_to(f, written, target_written, b"\n")

        content_len = newline_at - written
        assert 0 <= content_len <= max_content
        written += write_line(f, fill_bytes(content_len, b"A"), b"\n")

        # small tail line
        written += write_line(f, b"tail", b"\n")

    manifest_row(manifest, path.name, "LF", path.stat().st_size, desc)


def make_crlf_split_across_boundary(path: Path, cr_at: int, desc: str, manifest: list[str]) -> None:
    """
    Create a CRLF file where '\r' is at absolute index cr_at and '\n' at cr_at+1.
    """
    ensure_dir(path)
    with path.open("wb") as f:
        written = 0
        nl = b"\r\n"
        nl_len = 2
        max_content = MAX_LINE_TOTAL - nl_len  # 4094

        # Need content_len = cr_at - written <= 4094 => written >= cr_at - 4094
        target_written = max(0, cr_at - max_content)

        # ---- FIX: with CRLF you can never reach offset 1 using whole CRLF-terminated lines
        # For BUF_SIZE=4096, cr_at=4095 => target_written becomes 1. Bump to 2.
        if target_written == 1:
            target_written = 2
        # ---- end fix

        written = advance_to(f, written, target_written, nl)

        content_len = cr_at - written
        assert 0 <= content_len <= max_content
        written += write_line(f, fill_bytes(content_len, b"B"), nl)

        written += write_line(f, b"tail", nl)

    manifest_row(manifest, path.name, "CRLF", path.stat().st_size, desc)


def write_utf8_split_line_abs(f, written: int, split_offset: int, token_bytes: bytes, newline: bytes) -> int:
    """
    Writes a single line such that token_bytes begins at absolute position `split_offset`.
    Returns updated written.
    """
    if split_offset < written:
        raise RuntimeError(f"split_offset {split_offset} is behind current position {written}")

    nl_len = len(newline)
    max_content = MAX_LINE_TOTAL - nl_len
    token_len = len(token_bytes)
    assert token_len <= max_content

    # We need filler_len = split_offset - line_start <= (max_content - token_len)
    # Choose the earliest possible line_start, but never behind current 'written'.
    line_start = max(written, split_offset - (max_content - token_len))

    if line_start > split_offset:
        raise RuntimeError("Cannot place token: computed line_start > split_offset")

    if line_start != written:
        written = advance_to(f, written, line_start, newline)

    filler_len = split_offset - written
    assert 0 <= filler_len <= (max_content - token_len)

    content = fill_bytes(filler_len, b"C") + token_bytes
    written += write_line(f, content, newline)
    return written



def make_utf8_split_file(path: Path, split_offsets: list[int], token: str, desc: str, manifest: list[str]) -> None:
    """
    Create an LF file with one or more lines, each placing `token` at the specified absolute offset.
    """
    ensure_dir(path)
    token_bytes = token.encode("utf-8")
    with path.open("wb") as f:
        written = 0
        for off in split_offsets:
            written = write_utf8_split_line_abs(f, written, off, token_bytes, b"\n")
        write_line(f, b"tail", b"\n")
    manifest_row(manifest, path.name, "LF", path.stat().st_size, desc)


def make_eof_exact_multiple_of_buf_no_newline(path: Path, total_bytes: int, desc: str, manifest: list[str]) -> None:
    """
    File ends exactly at multiple of BUF_SIZE with NO final newline.
    Keeps per-line max constraint by ensuring the final EOF line length <= MAX_LINE_TOTAL.
    """
    assert total_bytes % BUF_SIZE == 0
    ensure_dir(path)
    with path.open("wb") as f:
        written = 0
        newline = b"\n"
        max_line_with_nl = MAX_LINE_TOTAL          # 4096 (content+newline)
        max_content_with_nl = MAX_LINE_TOTAL - 1   # 4095
        max_eof_line = MAX_LINE_TOTAL              # 4096 bytes allowed at EOF with no newline

        # small seeds
        for _ in range(100):
            written += write_line(f, b"seed", newline)

        remaining = total_bytes - written
        assert remaining >= 0

        # Keep writing newline-terminated lines until what's left can be written as the final EOF line (<= 4096 bytes).
        while remaining > max_eof_line:
            # If we can consume a full 4096-byte line and still leave > 4096 for later, do that.
            if remaining - max_line_with_nl >= max_eof_line:
                written += write_line(f, b"D" * max_content_with_nl, newline)  # 4095 + '\n' = 4096
            else:
                # remaining is in (4096, 8191]. Write one shorter line that leaves exactly 4096 bytes for EOF tail.
                consume = remaining - max_eof_line          # 1..4095 bytes INCLUDING '\n'
                content_len = consume - 1                  # 0..4094 bytes of content
                written += write_line(f, b"E" * content_len, newline)

            remaining = total_bytes - written

        # Final EOF line: write remaining bytes with NO newline (remaining <= 4096)
        assert 0 <= remaining <= max_eof_line
        f.write(fill_bytes(remaining, b"F"))
        written += remaining

        assert written == total_bytes

    manifest_row(manifest, path.name, "LF(no-final-nl)", path.stat().st_size, desc)


def make_eof_no_newline_multibyte_end(path: Path, desc: str, manifest: list[str]) -> None:
    """
    EOF without newline, last bytes are a multi-byte UTF-8 codepoint.
    """
    token = "😀".encode("utf-8")  # 4 bytes
    ensure_dir(path)
    with path.open("wb") as f:
        for _ in range(50):
            write_line(f, b"prefix", b"\n")
        content = fill_bytes(200, b"Z") + token
        assert len(content) <= MAX_LINE_TOTAL
        f.write(content)

    manifest_row(manifest, path.name, "LF(no-final-nl)", path.stat().st_size, desc)


# -----------------------------
# Suite generators (35 + optional 36)
# -----------------------------
manifest = ["name\tnewline\tbytes\tdescription"]


def gen_01_empty():
    p = OUT_DIR / "01_empty.txt"
    size = set_file_bytes_exact(p, b"")
    manifest_row(manifest, p.name, "N/A", size, "Empty file")


def gen_02_one_byte():
    p = OUT_DIR / "02_one_byte_no_newline.txt"
    size = set_file_bytes_exact(p, b"a")
    manifest_row(manifest, p.name, "N/A", size, "Single byte, no newline")


def gen_03_only_newlines():
    p = OUT_DIR / "03_only_newlines_lf.txt"
    size = set_file_bytes_exact(p, b"\n" * 32)
    manifest_row(manifest, p.name, "LF", size, "Only newlines (empty lines)")


def gen_04_fixed_len8_1000():
    p = OUT_DIR / "04_fixed_len8_1000_lines_crlf.txt"
    with p.open("wb") as f:
        for _ in range(1000):
            write_line(f, fill_bytes(8, b"abcd"), b"\r\n")
    manifest_row(manifest, p.name, "CRLF", p.stat().st_size, "1000 lines, content length 8 (CRLF)")


def gen_05_mixed_whitespace():
    p = OUT_DIR / "05_mixed_whitespace_lf.txt"
    lines = [
        b"    leading spaces",
        b"\t\tleading tabs",
        b"mix\t tabs \t and  spaces  ",
        b"trailing spaces    ",
        b"\ttrailing tab\t",
        b"",
        b"  \t  ",
        b"end",
    ]
    with p.open("wb") as f:
        for _ in range(200):
            for ln in lines:
                write_line(f, ln, b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Whitespace stress: tabs/leading/trailing/empty lines")


def gen_06_no_final_newline():
    p = OUT_DIR / "06_no_final_newline_lf.txt"
    with p.open("wb") as f:
        for i in range(2000):
            f.write(b"line-" + str(i).encode("ascii") + b"\n")
        f.write(b"last-line-no-newline")  # no final newline
    manifest_row(manifest, p.name, "LF(no-final-nl)", p.stat().st_size, "No final newline (EOF-terminated last line)")


def gen_07_monotonic_increasing():
    p = OUT_DIR / "07_monotonic_increasing_1_to_4095_lf.txt"
    with p.open("wb") as f:
        for L in range(1, 4096):
            write_line(f, fill_bytes(L, b"qwertyuiop"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Monotonic increasing line lengths (1..4095)")


def gen_08_uniform_random_lengths_seeded():
    p = OUT_DIR / "08_uniform_random_lengths_seeded_lf.txt"
    rng = random.Random(0xDEC0DE)
    with p.open("wb") as f:
        for _ in range(20000):
            L = rng.randint(1, 4095)
            write_line(f, fill_bytes(L, b"RANDOM"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Uniform random line lengths in [1..4095] (seeded)")


def gen_09_sawtooth():
    p = OUT_DIR / "09_sawtooth_1_to_4095_repeat_crlf.txt"
    with p.open("wb") as f:
        max_content = 4094
        for _ in range(8):
            for L in range(1, max_content + 1):
                write_line(f, fill_bytes(L, b"SAW"), b"\r\n")
    manifest_row(manifest, p.name, "CRLF", p.stat().st_size, "Sawtooth lengths (1..4094) repeated (CRLF)")


def gen_10_alternating_8_4095():
    p = OUT_DIR / "10_alternating_8_and_4095_lf.txt"
    with p.open("wb") as f:
        for _ in range(5000):
            write_line(f, fill_bytes(8, b"alt8"), b"\n")
            write_line(f, fill_bytes(4095, b"ALT4095"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Alternating line lengths 8 and 4095 (allocation stress)")


def gen_11_bimodal_90_10():
    p = OUT_DIR / "11_bimodal_90pct_8_10pct_4095_lf.txt"
    rng = random.Random(0xC0FFEE)
    with p.open("wb") as f:
        for _ in range(20000):
            if rng.random() < 0.90:
                write_line(f, fill_bytes(8, b"tiny"), b"\n")
            else:
                write_line(f, fill_bytes(4095, b"huge"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Bimodal: 90% len=8, 10% len=4095")


def gen_12_burst():
    p = OUT_DIR / "12_burst_tiny_then_huge_then_tiny_lf.txt"
    with p.open("wb") as f:
        for _ in range(10000):
            write_line(f, b"x" * 8, b"\n")
        for _ in range(200):
            write_line(f, b"y" * 4095, b"\n")
        for _ in range(10000):
            write_line(f, b"z" * 8, b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Burst: 10k tiny, 200 huge, 10k tiny")


def gen_13_single_max_with_newline():
    p = OUT_DIR / "13_single_max_line_with_newline_lf.txt"
    size = set_file_bytes_exact(p, b"A" * 4095 + b"\n")
    manifest_row(manifest, p.name, "LF", size, "Single max line (4095 content) + newline")


def gen_14_single_max_no_newline():
    p = OUT_DIR / "14_single_max_line_no_newline_eof.txt"
    size = set_file_bytes_exact(p, b"B" * 4096)
    manifest_row(manifest, p.name, "LF(no-final-nl)", size, "Single max EOF line (4096 bytes), no newline")


def gen_15_many_tiny_lines():
    p = OUT_DIR / "15_many_tiny_lines_len1_lf.txt"
    with p.open("wb", buffering=1024 * 1024) as f:
        line = b"x\n"
        block = line * 4096
        blocks = 5_000_000 // 4096
        rem = 5_000_000 % 4096
        for _ in range(blocks):
            f.write(block)
        f.write(line * rem)
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Many tiny lines (5,000,000 lines of 'x')")


def gen_16_few_huge_lines():
    p = OUT_DIR / "16_few_huge_lines_10000_lf.txt"
    with p.open("wb", buffering=1024 * 1024) as f:
        line = b"H" * 4095 + b"\n"
        for _ in range(10_000):
            f.write(line)
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Few huge lines (10,000 lines of 4095 bytes)")


def gen_17_utf8_2byte_heavy():
    p = OUT_DIR / "17_utf8_2byte_heavy_lf.txt"
    with p.open("wb") as f:
        for L in range(16, 4096, 17):
            write_line(f, utf8_fill_exact_bytes(L, "é"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "UTF-8 2-byte heavy (é)")


def gen_18_utf8_3byte_heavy():
    p = OUT_DIR / "18_utf8_3byte_heavy_lf.txt"
    with p.open("wb") as f:
        for L in range(16, 4096, 19):
            write_line(f, utf8_fill_exact_bytes(L, "中"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "UTF-8 3-byte heavy (CJK)")


def gen_19_utf8_4byte_heavy():
    p = OUT_DIR / "19_utf8_4byte_heavy_lf.txt"
    with p.open("wb") as f:
        for L in range(16, 4096, 23):
            write_line(f, utf8_fill_exact_bytes(L, "😀"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "UTF-8 4-byte heavy (emoji)")


def gen_20_utf8_mixed_per_line():
    p = OUT_DIR / "20_utf8_mixed_per_line_lf.txt"
    token = "ASCII é 中 😀 | ".encode("utf-8")
    with p.open("wb") as f:
        for L in range(32, 4096, 37):
            reps = max(1, L // len(token))
            content = (token * reps)[:L]
            while True:
                try:
                    content.decode("utf-8")
                    break
                except UnicodeDecodeError:
                    content = content[:-1]
            content = content + fill_bytes(L - len(content), b"_")
            write_line(f, content, b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Mixed UTF-8 in each line")


def gen_21_utf8_combining_marks():
    p = OUT_DIR / "21_utf8_combining_marks_lf.txt"
    with p.open("wb") as f:
        for L in range(32, 4096, 31):
            write_line(f, utf8_fill_exact_bytes(L, "e\u0301"), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "Combining marks (grapheme clusters)")


def gen_22_utf8_zwj_sequences():
    p = OUT_DIR / "22_utf8_zwj_sequences_lf.txt"
    seq = "👨‍👩‍👧‍👦"
    with p.open("wb") as f:
        for L in range(64, 4096, 41):
            write_line(f, utf8_fill_exact_bytes(L, seq), b"\n")
    manifest_row(manifest, p.name, "LF", p.stat().st_size, "ZWJ emoji sequences")


def gen_23_boundary_newline_at_buf_minus_1():
    off = BUF_SIZE - 1
    make_newline_at_offset_lf(
        OUT_DIR / f"23_boundary_newline_at_offset_{off}_lf.txt",
        newline_at=off,
        desc=f"Boundary: '\\n' at absolute offset {off} (chunk end for BUF_SIZE={BUF_SIZE})",
        manifest=manifest,
    )


def gen_24_boundary_newline_at_buf_minus_2():
    off = BUF_SIZE - 2
    make_newline_at_offset_lf(
        OUT_DIR / f"24_boundary_newline_at_offset_{off}_lf.txt",
        newline_at=off,
        desc=f"Boundary: '\\n' at absolute offset {off} (one before chunk end for BUF_SIZE={BUF_SIZE})",
        manifest=manifest,
    )


def gen_25_boundary_newline_at_buf_exact():
    off = BUF_SIZE
    make_newline_at_offset_lf(
        OUT_DIR / f"25_boundary_newline_at_offset_{off}_lf.txt",
        newline_at=off,
        desc=f"Boundary: '\\n' at absolute offset {off} (first byte of next chunk for BUF_SIZE={BUF_SIZE})",
        manifest=manifest,
    )


def gen_26_boundary_crlf_split():
    cr_at = BUF_SIZE - 1
    make_crlf_split_across_boundary(
        OUT_DIR / f"26_boundary_crlf_split_cr_at_{cr_at}.txt",
        cr_at=cr_at,
        desc=f"Boundary: CRLF split across chunks (\\r at {cr_at}, \\n at {cr_at+1}) for BUF_SIZE={BUF_SIZE}",
        manifest=manifest,
    )


def gen_27_boundary_utf8_2byte_split():
    # place 'é' such that it starts at BUF_SIZE-1 (1+1 split)
    split = BUF_SIZE - 1
    make_utf8_split_file(
        OUT_DIR / f"27_boundary_utf8_2byte_split_start_{split}_lf.txt",
        split_offsets=[split],
        token="é",
        desc=f"Boundary: UTF-8 2-byte codepoint split across chunks (é), start at {split} for BUF_SIZE={BUF_SIZE}",
        manifest=manifest,
    )


def gen_28_boundary_utf8_3byte_splits():
    # 1+2 split at BUF_SIZE-1, 2+1 split at 2*BUF_SIZE-2
    split1 = BUF_SIZE - 1
    split2 = 2 * BUF_SIZE - 2
    make_utf8_split_file(
        OUT_DIR / f"28_boundary_utf8_3byte_splits_start_{split1}_and_{split2}_lf.txt",
        split_offsets=[split1, split2],
        token="中",
        desc=f"Boundary: UTF-8 3-byte splits (1+2 at {split1}, 2+1 at {split2}) for BUF_SIZE={BUF_SIZE}",
        manifest=manifest,
    )


def gen_29_boundary_utf8_4byte_splits():
    # 1+3 at BUF_SIZE-1, 2+2 at 2*BUF_SIZE-2, 3+1 at 3*BUF_SIZE-3
    split1 = BUF_SIZE - 1
    split2 = 2 * BUF_SIZE - 2
    split3 = 3 * BUF_SIZE - 3
    make_utf8_split_file(
        OUT_DIR / f"29_boundary_utf8_4byte_splits_start_{split1}_{split2}_{split3}_lf.txt",
        split_offsets=[split1, split2, split3],
        token="😀",
        desc=f"Boundary: UTF-8 4-byte splits (1+3 at {split1}, 2+2 at {split2}, 3+1 at {split3}) for BUF_SIZE={BUF_SIZE}",
        manifest=manifest,
    )


def gen_30_boundary_eof_exact_multiple_of_buf():
    p = OUT_DIR / "30_boundary_eof_exact_multiple_of_buf_no_final_newline.txt"
    make_eof_exact_multiple_of_buf_no_newline(
        p,
        total_bytes=BUF_SIZE * 4,
        desc=f"EOF: file size exactly multiple of BUF_SIZE ({BUF_SIZE*4} bytes), no final newline",
        manifest=manifest,
    )


def gen_31_boundary_eof_no_newline_multibyte_end():
    p = OUT_DIR / "31_boundary_eof_no_newline_multibyte_at_end.txt"
    make_eof_no_newline_multibyte_end(
        p,
        desc="EOF: no final newline, last bytes are a multi-byte UTF-8 codepoint (😀)",
        manifest=manifest,
    )


def gen_32_large_64MiB_medium_lines():
    p = OUT_DIR / "32_large_64MiB_medium_lines_256_lf.txt"
    target = 64 * MiB
    line = fill_bytes(256, b"MED") + b"\n"  # 257 bytes

    def make(rem: int) -> bytes:
        if rem < len(line):
            if rem == 1:
                return b"\n"
            return fill_bytes(rem - 1, b"m") + b"\n"
        nlines = min(4096, rem // len(line))  # ~1 MiB chunks
        return line * nlines

    stream_write_target(
        p, target, make,
        desc="Large: 64 MiB total, ~256-byte lines (LF)",
        newline_label="LF",
        manifest=manifest,
    )


def gen_33_large_256MiB_tiny_lines():
    p = OUT_DIR / "33_large_256MiB_tiny_lines_8_lf.txt"
    target = 256 * MiB
    line = fill_bytes(8, b"TINY") + b"\n"  # 9 bytes

    def make(rem: int) -> bytes:
        if rem < len(line):
            if rem == 1:
                return b"\n"
            return fill_bytes(rem - 1, b"t") + b"\n"
        nlines = min(131072, rem // len(line))  # ~1.1 MiB chunks
        return line * nlines

    stream_write_target(
        p, target, make,
        desc="Large: 256 MiB total, tiny ~8-byte lines (LF)",
        newline_label="LF",
        manifest=manifest,
    )


def gen_34_large_256MiB_huge_lines():
    p = OUT_DIR / "34_large_256MiB_huge_lines_4095_lf.txt"
    target = 256 * MiB
    line = (b"H" * 4095) + b"\n"  # 4096 bytes

    def make(rem: int) -> bytes:
        if rem < len(line):
            if rem == 1:
                return b"\n"
            c = b"H" * min(4095, rem - 1)
            return c + b"\n"
        nlines = min(2048, rem // len(line))  # <= 8 MiB chunks
        return line * nlines

    stream_write_target(
        p, target, make,
        desc="Large: 256 MiB total, huge ~4095-byte lines (LF)",
        newline_label="LF",
        manifest=manifest,
    )


def gen_35_mixed_newlines_lf_crlf():
    p = OUT_DIR / "35_mixed_newlines_lf_and_crlf.txt"
    rng = random.Random(0xBADC0DE)
    with p.open("wb", buffering=1024 * 1024) as f:
        for i in range(200000):
            use_crlf = (i % 7 == 0) or (rng.random() < 0.15)
            nl = b"\r\n" if use_crlf else b"\n"
            max_content = MAX_LINE_TOTAL - len(nl)
            r = rng.random()
            if r < 0.80:
                L = rng.randint(1, min(256, max_content))
            elif r < 0.98:
                L = rng.randint(257, min(2048, max_content))
            else:
                L = rng.randint(max(1, max_content - 64), max_content)
            write_line(f, fill_bytes(L, b"MIXED"), nl)
    manifest_row(manifest, p.name, "MIXED(LF+CRLF)", p.stat().st_size, "Mixed newline styles (LF + CRLF) with varied line lengths")


# Optional 36th file: 1 GiB mixed/bimodal (enable with GEN_1G=1)
def gen_36_huge_1GiB_mixed_bimodal():
    p = OUT_DIR / "36_huge_1GiB_mixed_bimodal_lf.txt"
    target = 1024 * MiB  # 1 GiB
    small = fill_bytes(64, b"s") + b"\n"
    huge = fill_bytes(4095, b"h") + b"\n"
    pair = small + huge

    def make(rem: int) -> bytes:
        if rem < len(small):
            if rem == 1:
                return b"\n"
            return fill_bytes(rem - 1, b"x") + b"\n"

        max_pairs = min(2048, rem // len(pair))  # ~8.5 MiB chunks
        if max_pairs == 0:
            return huge if rem >= len(huge) else small
        return pair * max_pairs

    stream_write_target(
        p, target, make,
        desc="Extra: 1 GiB total, mixed bimodal (64-byte lines interleaved with 4095-byte lines)",
        newline_label="LF",
        manifest=manifest,
    )


def main() -> None:
    if BUF_SIZE < 8:
        raise RuntimeError("BUF_SIZE is unexpectedly small; boundary tests assume at least a few bytes of slack.")

    gens = [
        gen_01_empty,
        gen_02_one_byte,
        gen_03_only_newlines,
        gen_04_fixed_len8_1000,
        gen_05_mixed_whitespace,
        gen_06_no_final_newline,
        gen_07_monotonic_increasing,
        gen_08_uniform_random_lengths_seeded,
        gen_09_sawtooth,
        gen_10_alternating_8_4095,
        gen_11_bimodal_90_10,
        gen_12_burst,
        gen_13_single_max_with_newline,
        gen_14_single_max_no_newline,
        gen_15_many_tiny_lines,
        gen_16_few_huge_lines,
        gen_17_utf8_2byte_heavy,
        gen_18_utf8_3byte_heavy,
        gen_19_utf8_4byte_heavy,
        gen_20_utf8_mixed_per_line,
        gen_21_utf8_combining_marks,
        gen_22_utf8_zwj_sequences,
        gen_23_boundary_newline_at_buf_minus_1,
        gen_24_boundary_newline_at_buf_minus_2,
        gen_25_boundary_newline_at_buf_exact,
        gen_26_boundary_crlf_split,
        gen_27_boundary_utf8_2byte_split,
        gen_28_boundary_utf8_3byte_splits,
        gen_29_boundary_utf8_4byte_splits,
        gen_30_boundary_eof_exact_multiple_of_buf,
        gen_31_boundary_eof_no_newline_multibyte_end,
        gen_32_large_64MiB_medium_lines,
        gen_33_large_256MiB_tiny_lines,
        gen_34_large_256MiB_huge_lines,
        gen_35_mixed_newlines_lf_crlf,
    ]

    # Optional extra
    if os.environ.get("GEN_1G") == "1":
        gens.append(gen_36_huge_1GiB_mixed_bimodal)

    for fn in gens:
        fn()

    manifest_path = OUT_DIR / "MANIFEST.tsv"
    manifest_path.write_text("\n".join(manifest) + "\n", encoding="utf-8")

    print(f"BUF_SIZE={BUF_SIZE}")
    print(f"Generated {len(gens)} files in: {OUT_DIR}")
    print(f"Wrote manifest: {manifest_path}")


if __name__ == "__main__":
    main()


# # Generate suite aligned to BUFFSIZE=4096
# python3 generate_tests.py

# # Generate suite aligned to BUFFSIZE=8192
# BUF_SIZE=8192 python3 generate_tests.py

# # Also generate the optional 1 GiB file
# BUF_SIZE=8192 GEN_1G=1 python3 generate_tests.py
