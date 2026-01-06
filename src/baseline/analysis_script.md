python3 - <<'PY'
from pathlib import Path
import csv

# change this to your run folder if needed
# e.g. RUN="test_suite/runs/buf4096/20251228_222646"
RUN = sorted(Path("test_suite/runs/buf4096").glob("*"))[-1]
p = RUN / "SUMMARY.tsv"

def f(x):
    try:
        return float(x) if x not in ("", None) else None
    except:
        return None

rows = []
with p.open() as fin:
    r = csv.DictReader(fin, delimiter="\t")
    for row in r:
        bytes_ = int(row["bytes"])
        task = f(row.get("task_clock"))  # perf task-clock is usually in ms in human output; in TSV it can vary
        # if task-clock is in "msec", treat as ms; if it's large like 6e8, treat as nsec
        # heuristic:
        if task is None:
            continue
        if task > 1e7:   # likely nsec
            sec = task / 1e9
        else:            # likely msec
            sec = task / 1e3
        mib_s = (bytes_ / (1024*1024)) / sec if sec > 0 else 0.0

        sys_r = f(row.get("sys_read")) or 0.0
        sys_w = f(row.get("sys_write")) or 0.0
        sys_rv = f(row.get("sys_readv")) or 0.0
        sys_wv = f(row.get("sys_writev")) or 0.0
        sys_total = sys_r + sys_w + sys_rv + sys_wv

        ipc = f(row.get("ipc")) or 0.0
        front = f(row.get("frontend_idle_pct")) or 0.0
        brmp = f(row.get("branch_miss_pct")) or 0.0

        rows.append({
            "file": row["file"],
            "MiB": bytes_/(1024*1024),
            "sec": sec,
            "MiB/s": mib_s,
            "syscalls": sys_total,
            "syscalls/MiB": (sys_total / (bytes_/(1024*1024))) if bytes_ else 0.0,
            "ipc": ipc,
            "frontend_idle%": front,
            "branch_miss%": brmp,
            "check": row.get("check",""),
        })

rows.sort(key=lambda x: x["MiB/s"], reverse=True)

print(f"Run: {RUN}")
print("Top 10 throughput:")
for x in rows[:10]:
    print(f"{x['MiB/s']:8.1f} MiB/s  {x['sec']:6.3f}s  sys/MiB={x['syscalls/MiB']:.1f}  ipc={x['ipc']:.2f}  front%={x['frontend_idle%']:.1f}  {x['file']}")

print("\nBottom 10 throughput:")
for x in rows[-10:]:
    print(f"{x['MiB/s']:8.1f} MiB/s  {x['sec']:6.3f}s  sys/MiB={x['syscalls/MiB']:.1f}  ipc={x['ipc']:.2f}  front%={x['frontend_idle%']:.1f}  {x['file']}")
PY
