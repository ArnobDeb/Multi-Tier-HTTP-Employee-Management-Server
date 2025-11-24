#!/usr/bin/env python3
# detect_saturation.py
#
# Reads results/out_<workload>_<threads>.txt files, detects saturation point per workload,
# determines likely bottleneck (CPU/Disk/Failures) and annotates saved graphs (puts vertical line).
#
# Usage:
#   python3 detect_saturation.py
#
import re, glob, json, os
import numpy as np
import matplotlib.pyplot as plt

RESULT_GLOB = "results/out_*.txt"
GRAPH_DIR = "graphs"
os.makedirs(GRAPH_DIR, exist_ok=True)

def parse_file(path):
    s = open(path).read()
    wl = re.search(r"out_(.*?)_(\d+)\.txt", os.path.basename(path)).group(1)
    threads = int(re.search(r"out_.*?_(\d+)\.txt", os.path.basename(path)).group(1))

    # throughput & latency
    thr_m = re.search(r"THROUGHPUT=(.*?) req/s", s)
    lat_m = re.search(r"AVG_LATENCY=(.*?) ms", s)
    succ_m = re.search(r"SUCCESS=(\d+)", s)
    fail_m = re.search(r"FAIL=(\d+)", s)
    throughput = float(thr_m.group(1)) if thr_m else None
    latency = float(lat_m.group(1)) if lat_m else None
    success = int(succ_m.group(1)) if succ_m else 0
    fail = int(fail_m.group(1)) if fail_m else 0

    # cache JSON
    j_m = re.search(r"\{.*?cache_hits.*?\}", s)
    hits, misses = (0,0)
    if j_m:
        try:
            j = json.loads(j_m.group(0))
            hits = int(j.get("cache_hits",0))
            misses = int(j.get("cache_misses",0))
        except:
            pass

    # CPU section (extract average util of cores 0 & 1)
    cpu_util = 0.0
    cpu_section = ""
    try:
        cpu_section = s.split("# CPU UTILIZATION",1)[1]
    except:
        cpu_section = ""
    if cpu_section:
        # find lines with " 0 " or " 1 " and read %idle as last column
        idle_vals = []
        for line in cpu_section.splitlines():
            if re.search(r"\b0\b|\b1\b", line):
                parts = line.strip().split()
                try:
                    idle = float(parts[-1])
                    idle_vals.append(idle)
                except:
                    pass
        if idle_vals:
            cpu_util = 100.0 - np.mean(idle_vals)

    # Disk section (extract %util average)
    disk_util = 0.0
    disk_section = ""
    try:
        disk_section = s.split("# DISK UTILIZATION",1)[1]
    except:
        disk_section = ""
    if disk_section:
        util_vals = []
        for line in disk_section.splitlines():
            parts = line.strip().split()
            # iostat device lines often have last column %util
            if len(parts) >= 2 and re.match(r"[a-zA-Z]", parts[0]):
                try:
                    util = float(parts[-1])
                    util_vals.append(util)
                except:
                    pass
        if util_vals:
            disk_util = float(np.mean(util_vals))

    return {
        "workload": wl,
        "threads": threads,
        "throughput": throughput,
        "latency": latency,
        "success": success,
        "fail": fail,
        "hits": hits,
        "misses": misses,
        "cpu": cpu_util,
        "disk": disk_util,
        "path": path
    }

# collect results
files = sorted(glob.glob(RESULT_GLOB))
data = {}
for f in files:
    rec = parse_file(f)
    wl = rec["workload"]
    data.setdefault(wl, []).append(rec)

def detect_saturation_for(wl, recs):
    # sort by threads
    recs = sorted(recs, key=lambda r: r["threads"])
    threads = np.array([r["threads"] for r in recs])
    thr = np.array([r["throughput"] if r["throughput"] is not None else 0.0 for r in recs])
    lat = np.array([r["latency"] if r["latency"] is not None else 0.0 for r in recs])
    fail = np.array([r["fail"] for r in recs])
    cpu = np.array([r["cpu"] for r in recs])
    disk = np.array([r["disk"] for r in recs])

    # basic smoothing (optional)
    # Compute relative growth between consecutive throughput points
    rel_growth = np.zeros(len(thr))
    rel_growth[1:] = (thr[1:] - thr[:-1]) / (thr[:-1] + 1e-9)

    # Find first index where throughput growth drops below a small threshold (e.g., 5%)
    # AND latency growth becomes positive & substantial.
    sat_idx = None
    for i in range(1, len(thr)-1):
        if thr[i] <= 0: continue
        # require that growth from i->i+1 is small
        if rel_growth[i+1] < 0.05 and rel_growth[i] < 0.05:
            # Also require latency increased compared to previous
            if lat[i+1] > 1.2 * lat[i] or (fail[i+1] - fail[i]) > 0:
                sat_idx = i+1
                break

    # fallback: if no sat_idx, pick index with max curvature of throughput (elbow)
    if sat_idx is None and len(thr) >= 3:
        # compute second derivative approx
        second = np.zeros(len(thr))
        second[1:-1] = thr[2:] - 2*thr[1:-1] + thr[:-2]
        sat_idx = int(np.argmax(np.abs(second)))

    # ensure index valid
    sat_idx = min(max(0, sat_idx if sat_idx is not None else 0), len(recs)-1)

    # pick likely bottleneck:
    reason = "unknown"
    if cpu[sat_idx] >= 75.0:
        reason = "CPU"
    elif disk[sat_idx] >= 75.0:
        reason = "Disk"
    elif fail[sat_idx] > 0:
        reason = "Failures"
    else:
        # compare relative growth of cpu and disk across range
        if cpu.max() > disk.max() and cpu.max() > 60.0:
            reason = "CPU"
        elif disk.max() > cpu.max() and disk.max() > 60.0:
            reason = "Disk"
        else:
            reason = "mixed/unclear"

    return {
        "sat_threads": threads[sat_idx],
        "sat_index": sat_idx,
        "sat_throughput": float(thr[sat_idx]),
        "sat_latency": float(lat[sat_idx]),
        "cpu": float(cpu[sat_idx]),
        "disk": float(disk[sat_idx]),
        "reason": reason,
        "recs": recs
    }

# run detection and annotate graphs
reports = {}
for wl, recs in data.items():
    rpt = detect_saturation_for(wl, recs)
    reports[wl] = rpt

    # annotate an existing graph if present (throughput graph)
    t_vals = [r["threads"] for r in rpt["recs"]]
    thr_vals = [r["throughput"] for r in rpt["recs"]]
    lat_vals = [r["latency"] for r in rpt["recs"]]

    # if a throughput graph exists, load & replot with vertical line; else create small plot
    plt.figure()
    plt.plot(t_vals, thr_vals, marker='o', label='Throughput')
    plt.plot(t_vals, lat_vals, marker='x', label='Latency (ms)')
    sat_t = rpt["sat_threads"]
    plt.axvline(x=sat_t, color='r', linestyle='--', linewidth=1.5)
    plt.text(sat_t, max(thr_vals)*0.6, f"Saturation @ {sat_t} threads\nReason: {rpt['reason']}", color='r')
    plt.xlabel("Threads")
    plt.ylabel("Throughput/Latency")
    plt.title(f"Saturation detection — {wl}")
    plt.legend()
    out = os.path.join(GRAPH_DIR, f"saturation_{wl}.png")
    plt.grid(True)
    plt.savefig(out)
    plt.close()

# write a small report (txt)
with open("saturation_report.txt", "w") as fh:
    fh.write("Saturation Detection Report\n")
    fh.write("==========================\n\n")
    for wl, rpt in reports.items():
        fh.write(f"Workload: {wl}\n")
        fh.write(f" Saturation threads: {rpt['sat_threads']}\n")
        fh.write(f" Throughput @sat: {rpt['sat_throughput']}\n")
        fh.write(f" Latency  @sat: {rpt['sat_latency']}\n")
        fh.write(f" CPU util @sat: {rpt['cpu']}%\n")
        fh.write(f" Disk util @sat: {rpt['disk']}%\n")
        fh.write(f" Likely bottleneck: {rpt['reason']}\n\n")
print("Saturation detection complete. See saturation_report.txt and graphs/saturation_*.png")
