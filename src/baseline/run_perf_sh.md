Here are the exact commands you’ll run (from your `src/baseline/` directory), in a sensible order.

### 1) Go to the baseline folder

```bash
cd ~/Projects/cpp/file-reverser/src/baseline
```

### 2) Create/overwrite the script file

```bash
cat > run_perf_suite.sh <<'SH'
# (paste the full script I gave you here)
SH
```

### 3) Make it executable

```bash
chmod +x run_perf_suite.sh
```

### 4) (Optional) sanity-check the suite directory exists

```bash
ls -1 ./test_suite/input/testsuite35_utf8_buf4096 | head
```

### 5) Run it (perf requires sudo once)

```bash
REPEATS=5 ./run_perf_suite.sh
```

---

## Useful variants

### Skip the 1GiB file completely

```bash
SKIP_1G=1 REPEATS=5 ./run_perf_suite.sh
```

### Run perf on everything, but skip correctness check on the 1GiB file

```bash
SKIP_CHECK_1G=1 REPEATS=5 ./run_perf_suite.sh
```

### Disable correctness checking entirely (perf only)

```bash
CHECK=0 REPEATS=5 ./run_perf_suite.sh
```

### Change buffer size target (if you generated another suite folder)

Example for 8192 (requires that folder exists: `testsuite35_utf8_buf8192`)

```bash
BUF_BYTES=8192 REPEATS=5 ./run_perf_suite.sh
```

---

## After it finishes: where results are

### Find the most recent run folder

```bash
ls -1dt ./test_suite/runs/buf4096/* | head -n 1
```

### Open the summary

```bash
latest="$(ls -1dt ./test_suite/runs/buf4096/* | head -n 1)"
less "$latest/SUMMARY.tsv"
```

### See any failed correctness checks

```bash
latest="$(ls -1dt ./test_suite/runs/buf4096/* | head -n 1)"
grep -R "FAIL" "$latest/logs" || true
```

### View perf for one file (readable)

```bash
latest="$(ls -1dt ./test_suite/runs/buf4096/* | head -n 1)"
less "$latest/perf/07_monotonic_increasing_1_to_4095_lf.perf.txt"
```

### See outputs generated

```bash
latest="$(ls -1dt ./test_suite/runs/buf4096/* | head -n 1)"
ls -1 "$latest/outputs" | head
```

If you tell me which `BUF_BYTES` you’re running next (8192/131072/etc.), I can give you the exact `testsuite...` folder name to generate and the matching one-liner to run the suite.
