#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------
# Config (override via env vars)
# ------------------------------------------
BUF_BYTES="${BUF_BYTES:-4096}"        # only used to select the suite directory name
RUNS="${RUNS:-5}"                    # number of separate perf-instrumented runs per input file
PERF_REPEATS="${PERF_REPEATS:-1}"    # perf stat -r (kept separate from RUNS)
SKIP_1G="${SKIP_1G:-0}"              # set to 1 to skip 36_...1GiB file
CHECK="${CHECK:-1}"                  # set to 0 to disable correctness checks
SKIP_CHECK_1G="${SKIP_CHECK_1G:-0}"  # set to 1 to skip correctness check for 1GiB file
PROGS="${PROGS:-01_fstream 02_fstream}" # space-separated program basenames (no .cpp)

CXX="${CXX:-g++}"
PYTHON="${PYTHON:-python3}"

# perf: TSV output + extra derived/cache stats
PERF_TSV_OPTS=(--no-big-num -x $'\t' -d -d -r "${PERF_REPEATS}")

# ------------------------------------------
# Paths (relative to this script)
# ------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASELINE_DIR="${SCRIPT_DIR}"
TEST_SUITE_DIR="${BASELINE_DIR}/test_suite"
SUITE_DIR="${TEST_SUITE_DIR}/input/testsuite35_utf8_buf${BUF_BYTES}"

BIN_DIR="${BASELINE_DIR}/bin"
mkdir -p "${BIN_DIR}"

if [[ ! -d "${SUITE_DIR}" ]]; then
  echo "ERROR: suite dir not found: ${SUITE_DIR}" >&2
  echo "Expected: ${SUITE_DIR}" >&2
  exit 1
fi

RUN_TAG="$(date +%Y%m%d_%H%M%S)"

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

float_lt() {
  # true (exit 0) if $1 < $2
  awk -v a="$1" -v b="$2" 'BEGIN{exit !(a<b)}'
}

# Correctness check for ONE input/output pair
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
for prog in ${PROGS}; do
  SRC="${BASELINE_DIR}/${prog}.cpp"
  BIN="${BIN_DIR}/${prog}"

  if [[ ! -f "${SRC}" ]]; then
    echo "ERROR: source not found: ${SRC}" >&2
    exit 1
  fi

  echo "  - Building ${prog} -> ${BIN}"
  "${CXX}" -std=c++23 -O3 -march=native -DNDEBUG \
    -Wall -Wextra -Wpedantic \
    -o "${BIN}" "${SRC}"
done
echo

# ------------------------------------------
# Sudo auth for perf
# ------------------------------------------
echo "[2/3] Sudo auth for perf (you may be prompted once)..."
sudo -v

( while true; do sudo -n true; sleep 60; done ) >/dev/null 2>&1 &
KEEPALIVE_PID="$!"
trap 'kill "${KEEPALIVE_PID}" >/dev/null 2>&1 || true' EXIT

# ------------------------------------------
# Stable iteration order for inputs
# ------------------------------------------
mapfile -t FILES < <(find "${SUITE_DIR}" -maxdepth 1 -type f -name "*.txt" -printf "%f\n" | LC_ALL=C sort)

# ------------------------------------------
# Run suites
# ------------------------------------------
echo "[3/3] Running suite with perf..."
echo "Suite: ${SUITE_DIR}"
echo "RUNS per input: ${RUNS}"
echo "perf -r: ${PERF_REPEATS}"
echo

for prog in ${PROGS}; do
  BIN="${BIN_DIR}/${prog}"

  RUN_ROOT="${TEST_SUITE_DIR}/runs/${prog}/buf${BUF_BYTES}/${RUN_TAG}"
  OUT_DIR="${RUN_ROOT}/outputs"
  PERF_DIR="${RUN_ROOT}/perf"
  LOG_DIR="${RUN_ROOT}/logs"

  PEAK_ROOT="${RUN_ROOT}/peak_run"
  PEAK_OUT="${PEAK_ROOT}/outputs"
  PEAK_PERF="${PEAK_ROOT}/perf"
  PEAK_LOG="${PEAK_ROOT}/logs"

  mkdir -p "${OUT_DIR}" "${PERF_DIR}" "${LOG_DIR}" "${PEAK_OUT}" "${PEAK_PERF}" "${PEAK_LOG}"

  SUMMARY="${RUN_ROOT}/SUMMARY.tsv"
  printf "prog\tfile\tbytes\truns\tbest_run\tbest_ret\tbest_elapsed_sec\tcheck\t" > "${SUMMARY}"
  printf "task_clock_msec\tcycles\tinstructions\tipc\tcache_references\tcache_misses\tbranches\tbranch_misses\tpage_faults\n" >> "${SUMMARY}"

  echo "============================================================"
  echo "Program: ${prog}"
  echo "Outputs: ${OUT_DIR}"
  echo "Peak:    ${PEAK_ROOT}"
  echo "Perf:    ${PERF_DIR}"
  echo "Logs:    ${LOG_DIR}"
  echo "Summary: ${SUMMARY}"
  echo "============================================================"
  echo

  for name in "${FILES[@]}"; do
    if [[ "${SKIP_1G}" == "1" && "${name}" == 36_* ]]; then
      echo "Skipping (SKIP_1G=1): ${name}"
      continue
    fi

    in_file="${SUITE_DIR}/${name}"
    base="${name%.txt}"

    # Single output path for repeated runs; we copy the best one into peak_run/
    out_file="${OUT_DIR}/${base}.reversed.txt"

    bytes="$(stat -c%s "${in_file}")"
    echo "-> ${name} (${bytes} bytes)"

    best_elapsed=""
    best_run="0"
    best_ret="1"

    # Clear any previous peak artifacts for this file name
    rm -f "${PEAK_OUT}/${base}.reversed.txt" \
          "${PEAK_PERF}/${base}.perf.tsv" "${PEAK_PERF}/${base}.perf.txt" \
          "${PEAK_LOG}/${base}.stdout.txt" "${PEAK_LOG}/${base}.stderr.txt"

    for ((run=1; run<=RUNS; run++)); do
      perf_tsv_run="${PERF_DIR}/${base}.run${run}.perf.tsv"
      perf_txt_run="${PERF_DIR}/${base}.run${run}.perf.txt"
      log_out_run="${LOG_DIR}/${base}.run${run}.stdout.txt"
      log_err_run="${LOG_DIR}/${base}.run${run}.stderr.txt"

      # Run perf stat ONCE per run
      set +e
      sudo perf stat "${PERF_TSV_OPTS[@]}" -o "${perf_tsv_run}" -- \
        "${BIN}" "${in_file}" "${out_file}" \
        1> "${log_out_run}" 2> "${log_err_run}"
      ret="$?"
      set -e

      make_pretty_perf "${perf_tsv_run}" "${perf_txt_run}"

      # Elapsed time (best-effort match)
      elapsed="$(event_val "${perf_tsv_run}" 'seconds time elapsed$')"
      if [[ -z "${elapsed}" ]]; then
        elapsed="$(event_val "${perf_tsv_run}" 'time elapsed$')"
      fi

      echo "   run ${run}/${RUNS}: ret=${ret} elapsed=${elapsed:-NA}s"

      # Only consider successful runs with a numeric elapsed
      if [[ "${ret}" -eq 0 && "${elapsed}" =~ ^[0-9.]+$ ]]; then
        if [[ -z "${best_elapsed}" ]] || float_lt "${elapsed}" "${best_elapsed}"; then
          best_elapsed="${elapsed}"
          best_run="${run}"
          best_ret="0"

          # Snapshot "peak run" artifacts immediately (since out_file will be overwritten next run)
          cp -f "${out_file}" "${PEAK_OUT}/${base}.reversed.txt"
          cp -f "${perf_tsv_run}" "${PEAK_PERF}/${base}.perf.tsv"
          make_pretty_perf "${PEAK_PERF}/${base}.perf.tsv" "${PEAK_PERF}/${base}.perf.txt"
          cp -f "${log_out_run}" "${PEAK_LOG}/${base}.stdout.txt"
          cp -f "${log_err_run}" "${PEAK_LOG}/${base}.stderr.txt"
        fi
      fi

      if [[ "${ret}" -ne 0 ]]; then
        echo "      !! program returned ${ret} (see ${log_err_run})"
      fi
    done

    # Correctness check (against peak output)
    check_status="SKIP"
    check_log="${PEAK_LOG}/${base}.check.txt"
    if [[ "${CHECK}" == "1" && "${best_ret}" -eq 0 ]]; then
      if [[ "${SKIP_CHECK_1G}" == "1" && "${name}" == 36_* ]]; then
        check_status="SKIP"
        echo "   (correctness skipped for 1GiB file)"
      else
        set +e
        check_one_pair "${in_file}" "${PEAK_OUT}/${base}.reversed.txt" "${check_log}"
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

    # Pull key metrics from the peak perf TSV (if any)
    peak_tsv="${PEAK_PERF}/${base}.perf.tsv"
    task_clock=""
    cycles=""
    instr=""
    cache_ref=""
    cache_miss=""
    branches=""
    br_miss=""
    pfaults=""

    if [[ -f "${peak_tsv}" ]]; then
      task_clock="$(event_val "${peak_tsv}" '^task-clock$')"
      cycles="$(event_val "${peak_tsv}" '^cycles$')"
      instr="$(event_val "${peak_tsv}" '^instructions$')"
      cache_ref="$(event_val "${peak_tsv}" '^cache-references$')"
      cache_miss="$(event_val "${peak_tsv}" '^cache-misses$')"
      branches="$(event_val "${peak_tsv}" '^branches$')"
      br_miss="$(event_val "${peak_tsv}" '^branch-misses$')"
      pfaults="$(event_val "${peak_tsv}" '^page-faults$')"
    fi

    ipc=""
    if [[ -n "${cycles}" && -n "${instr}" && "${cycles}" != "0" ]]; then
      ipc="$(awk -v i="${instr}" -v c="${cycles}" 'BEGIN{printf "%.4f", (c>0)?(i/c):0}')"
    fi

    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "${prog}" "${name}" "${bytes}" "${RUNS}" "${best_run}" "${best_ret}" "${best_elapsed}" "${check_status}" \
      "${task_clock}" "${cycles}" "${instr}" "${ipc}" \
      "${cache_ref}" "${cache_miss}" "${branches}" "${br_miss}" "${pfaults}" \
      >> "${SUMMARY}"
  done

  echo
  echo "Done for ${prog}."
  echo "Summary:  ${SUMMARY}"
  echo "Peak dir: ${PEAK_ROOT}"
  echo
done

echo "All done."
