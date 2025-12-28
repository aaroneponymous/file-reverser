#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------
# Config
# ------------------------------------------
BUF_BYTES="${BUF_BYTES:-4096}"     # program --buf
REPEATS="${REPEATS:-5}"            # perf stat -r
SKIP_1G="${SKIP_1G:-0}"            # set to 1 to skip 36_...1GiB file
CHECK="${CHECK:-1}"               # set to 0 to disable correctness checks
SKIP_CHECK_1G="${SKIP_CHECK_1G:-0}"# set to 1 to skip correctness check for 1GiB file
CXX="${CXX:-g++}"
PYTHON="${PYTHON:-python3}"

# perf: TSV output + extra derived/cache stats
PERF_TSV_OPTS=(--no-big-num -x $'\t' -d -d -r "${REPEATS}")

# ------------------------------------------
# Paths (relative to this script)
# ------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASELINE_DIR="${SCRIPT_DIR}"
TEST_SUITE_DIR="${BASELINE_DIR}/test_suite"
SUITE_DIR="${TEST_SUITE_DIR}/input/testsuite35_utf8_buf${BUF_BYTES}"

SRC="${BASELINE_DIR}/file_reverser.cpp"
BIN_DIR="${BASELINE_DIR}/bin"
BIN="${BIN_DIR}/file_reverser"

if [[ ! -f "${SRC}" ]]; then
  echo "ERROR: source not found: ${SRC}" >&2
  exit 1
fi

if [[ ! -d "${SUITE_DIR}" ]]; then
  echo "ERROR: suite dir not found: ${SUITE_DIR}" >&2
  echo "Expected: ${SUITE_DIR}" >&2
  exit 1
fi

mkdir -p "${BIN_DIR}"

RUN_TAG="$(date +%Y%m%d_%H%M%S)"
RUN_ROOT="${TEST_SUITE_DIR}/runs/buf${BUF_BYTES}/${RUN_TAG}"
OUT_DIR="${RUN_ROOT}/outputs"
PERF_DIR="${RUN_ROOT}/perf"
LOG_DIR="${RUN_ROOT}/logs"
mkdir -p "${OUT_DIR}" "${PERF_DIR}" "${LOG_DIR}"

SUMMARY="${RUN_ROOT}/SUMMARY.tsv"

# ------------------------------------------
# Helpers
# ------------------------------------------
event_val() {
  # Extract numeric value for an event name from perf TSV output.
  # perf -x TSV columns typically: value, unit, event, ...
  local tsv="$1"
  local ev_regex="$2"
  awk -F $'\t' -v re="${ev_regex}" '
    $3 ~ re {
      if ($1 ~ /^[0-9.]+$/) { print $1; exit }
      else { print ""; exit }
    }
  ' "${tsv}"
}

make_pretty_perf() {
  local in_tsv="$1"
  local out_txt="$2"
  if command -v column >/dev/null 2>&1; then
    column -t -s $'\t' "${in_tsv}" > "${out_txt}" || cp -f "${in_tsv}" "${out_txt}"
  else
    cp -f "${in_tsv}" "${out_txt}"
  fi
}

# Correctness check for ONE input/output pair (uses your method, adapted)
check_one_pair() {
  local in_file="$1"
  local out_file="$2"
  local check_log="$3"

  IN_FILE="${in_file}" OUT_FILE="${out_file}" "${PYTHON}" - <<'PY' > "${check_log}" 2>&1
import os
from pathlib import Path

IN_FILE = Path(os.environ["IN_FILE"])
OUT_FILE = Path(os.environ["OUT_FILE"])

def reverse_line_bytes(line: bytes) -> bytes:
    # Preserve EOL exactly, reverse only content before it
    eol = b""
    content = line
    if line.endswith(b"\n"):
        if line.endswith(b"\r\n"):
            eol = b"\r\n"
            content = line[:-2]
        else:
            eol = b"\n"
            content = line[:-1]

    # Reverse by UTF-8 code points; fallback to raw bytes if decode fails
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
    return x[lo:hi].decode("utf-8", errors="replace").replace("\n", "\\n").replace("\r", "\\r")

if not IN_FILE.exists():
    print(f"ERROR: missing input: {IN_FILE}")
    raise SystemExit(2)
if not OUT_FILE.exists():
    print(f"ERROR: missing output: {OUT_FILE}")
    raise SystemExit(2)

with IN_FILE.open("rb") as fin, OUT_FILE.open("rb") as fout:
    line_no = 0
    while True:
        in_line = fin.readline()
        out_line = fout.readline()

        if not in_line and not out_line:
            break
        if not in_line or not out_line:
            where = "input ended early" if not in_line else "output ended early"
            print(f"FAIL: {where} at line {line_no+1}")
            raise SystemExit(1)

        line_no += 1
        exp = reverse_line_bytes(in_line)
        if exp != out_line:
            idx = first_mismatch(exp, out_line)
            print(f"FAIL: line {line_no}, byte {idx}")
            print(f"  exp: ...{preview(exp, idx)}...")
            print(f"  out: ...{preview(out_line, idx)}...")
            raise SystemExit(1)

    extra = fout.readline()
    if extra:
        print(f"FAIL: output has extra data after line {line_no}")
        raise SystemExit(1)

print("PASS")
PY
}

# ------------------------------------------
# Compile (C++23)
# ------------------------------------------
echo "[1/3] Compiling (C++23)..."
"${CXX}" -std=c++23 -O3 -march=native -DNDEBUG \
  -Wall -Wextra -Wpedantic \
  -o "${BIN}" "${SRC}"
echo "Built: ${BIN}"
echo

# ------------------------------------------
# Sudo auth for perf
# ------------------------------------------
echo "[2/3] Sudo auth for perf (you may be prompted once)..."
sudo -v

# Keep sudo alive during long runs
( while true; do sudo -n true; sleep 60; done ) >/dev/null 2>&1 &
KEEPALIVE_PID="$!"
trap 'kill "${KEEPALIVE_PID}" >/dev/null 2>&1 || true' EXIT

# ------------------------------------------
# Summary header
# ------------------------------------------
printf "file\tbytes\tret\tcheck\t" > "${SUMMARY}"
printf "task_clock_msec\tcycles\tinstructions\tipc\tcache_references\tcache_misses\tbranches\tbranch_misses\tpage_faults\n" >> "${SUMMARY}"

echo "[3/3] Running suite with perf..."
echo "Suite:   ${SUITE_DIR}"
echo "Outputs: ${OUT_DIR}"
echo "Perf:    ${PERF_DIR}"
echo "Logs:    ${LOG_DIR}"
echo "Summary: ${SUMMARY}"
echo

# Stable iteration order
mapfile -t FILES < <(find "${SUITE_DIR}" -maxdepth 1 -type f -name "*.txt" -printf "%f\n" | LC_ALL=C sort)

for name in "${FILES[@]}"; do
  if [[ "${SKIP_1G}" == "1" && "${name}" == 36_* ]]; then
    echo "Skipping (SKIP_1G=1): ${name}"
    continue
  fi

  in_file="${SUITE_DIR}/${name}"
  base="${name%.txt}"

  out_file="${OUT_DIR}/${base}.reversed.txt"

  perf_tsv="${PERF_DIR}/${base}.perf.tsv"
  perf_txt="${PERF_DIR}/${base}.perf.txt"

  log_out="${LOG_DIR}/${base}.stdout.txt"
  log_err="${LOG_DIR}/${base}.stderr.txt"
  check_log="${LOG_DIR}/${base}.check.txt"

  bytes="$(stat -c%s "${in_file}")"

  echo "-> ${name} (${bytes} bytes)"

  # Run perf stat ONCE per file. perf stats go to perf_tsv; stdout/stderr go to logs.
  set +e
  sudo perf stat "${PERF_TSV_OPTS[@]}" -o "${perf_tsv}" -- \
    "${BIN}" --in "${in_file}" --out "${out_file}" --buf "${BUF_BYTES}" \
    1> "${log_out}" 2> "${log_err}"
  ret="$?"
  set -e

  # Make a readable perf file from TSV
  make_pretty_perf "${perf_tsv}" "${perf_txt}"

  # Correctness check
  check_status="SKIP"
  if [[ "${CHECK}" == "1" && "${ret}" -eq 0 ]]; then
    if [[ "${SKIP_CHECK_1G}" == "1" && "${name}" == 36_* ]]; then
      check_status="SKIP"
      echo "   (correctness skipped for 1GiB file)"
    else
      set +e
      check_one_pair "${in_file}" "${out_file}" "${check_log}"
      chk_ret="$?"
      set -e
      if [[ "${chk_ret}" -eq 0 ]]; then
        check_status="PASS"
      else
        check_status="FAIL"
        echo "   !! correctness FAIL (see ${check_log})"
      fi
    fi
  fi

  # Pull key metrics
  task_clock="$(event_val "${perf_tsv}" '^task-clock$')"
  cycles="$(event_val "${perf_tsv}" '^cycles$')"
  instr="$(event_val "${perf_tsv}" '^instructions$')"
  cache_ref="$(event_val "${perf_tsv}" '^cache-references$')"
  cache_miss="$(event_val "${perf_tsv}" '^cache-misses$')"
  branches="$(event_val "${perf_tsv}" '^branches$')"
  br_miss="$(event_val "${perf_tsv}" '^branch-misses$')"
  pfaults="$(event_val "${perf_tsv}" '^page-faults$')"

  ipc=""
  if [[ -n "${cycles}" && -n "${instr}" && "${cycles}" != "0" ]]; then
    ipc="$(awk -v i="${instr}" -v c="${cycles}" 'BEGIN{printf "%.4f", (c>0)?(i/c):0}')"
  fi

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "${name}" "${bytes}" "${ret}" "${check_status}" \
    "${task_clock}" "${cycles}" "${instr}" "${ipc}" \
    "${cache_ref}" "${cache_miss}" "${branches}" "${br_miss}" "${pfaults}" \
    >> "${SUMMARY}"

  if [[ "${ret}" -ne 0 ]]; then
    echo "   !! program returned ${ret} (see ${log_err})"
  fi
done

echo
echo "Done."
echo "Summary: ${SUMMARY}"
echo "Perf dir: ${PERF_DIR}"
echo "Output dir: ${OUT_DIR}"
echo "Logs dir: ${LOG_DIR}"
