import re
import glob
import json
import matplotlib.pyplot as plt
import os

# Ensure output folder exists
os.makedirs("graphs", exist_ok=True)

############################################################
# Parse filename
############################################################
def parse_filename(fname):
    m = re.search(r"out_(.*?)_(\d+)\.txt", fname)
    if not m:
        return None, None
    return m.group(1), int(m.group(2))

############################################################
# Parse throughput & latency
############################################################
def parse_metrics(text):
    thr = re.search(r"THROUGHPUT=(.*?) req/s", text)
    lat = re.search(r"AVG_LATENCY=(.*?) ms", text)
    throughput = float(thr.group(1)) if thr else None
    latency = float(lat.group(1)) if lat else None
    return throughput, latency

############################################################
# Parse cachec, cpu & io stats
############################################################
def parse_cache_json(text):
    m = re.search(r"\{.*?cache_hits.*?\}", text)
    if not m:
        return 0, 0
    try:
        j = json.loads(m.group(0))
        return j.get("cache_hits", 0), j.get("cache_misses", 0)
    except json.JSONDecodeError:
        return 0, 0

def parse_cpu_util(text):
    """
    Extract average CPU% for cores 0 and 1 from mpstat output.
    We look for lines like:
       all or 0 or 1 | %idle column
    CPU_util = 100 - %idle
    """
    cpu_vals = []
    for line in text.splitlines():
        if re.search(r"\s+[01]\s+", line):
            parts = line.split()
            idle = float(parts[-1])
            util = 100 - idle
            cpu_vals.append(util)
    return sum(cpu_vals)/len(cpu_vals) if cpu_vals else 0

def parse_disk_util(text):
    """
    Extract %util column from iostat:
    Device   tps   ...   %util
    """
    util_vals = []
    for line in text.splitlines():
        if re.search(r"^\s*\w+\s+\d", line):
            parts = line.split()
            try:
                util_vals.append(float(parts[-1]))
            except:
                pass
    return sum(util_vals)/len(util_vals) if util_vals else 0


############################################################
# Load all experiment files
############################################################
files = sorted(glob.glob("results/out_*.txt"))
if not files:
    print("No result files found.")
    exit(0)

data = {}   # workload -> { threads: (throughput, latency, hit_ratio) }

for fname in files:
    text = open(fname).read()
    wl, threads = parse_filename(fname)
    if wl is None:
        continue

    thr, lat = parse_metrics(text)
    hits, misses = parse_cache_json(text)
    hit_ratio = hits / (hits + misses) if (hits + misses) > 0 else 0

    cpu_section = text.split("# CPU UTILIZATION")[1]
    disk_section = text.split("# DISK UTILIZATION")[1]

    cpu = parse_cpu_util(cpu_section)
    disk = parse_disk_util(disk_section)

    if wl not in data:
        data[wl] = {}
    data[wl][threads] = (thr, lat, hit_ratio, cpu, disk)

############################################################
# Helper to add value labels on graph
############################################################
def annotate_points(xs, ys):
    for x, y in zip(xs, ys):
        plt.annotate(f"({x},{round(y,2)})",
                     xy=(x, y),
                     xytext=(5, 5),
                     textcoords="offset points",
                     fontsize=8)

############################################################
# Plot graphs for each workload
############################################################
for wl, entries in data.items():

    threads = sorted(entries.keys())
    throughputs = [entries[t][0] for t in threads]
    latencies   = [entries[t][1] for t in threads]
    ratios      = [entries[t][2] for t in threads]
    cpu   = [entries[t][3] for t in threads]
    disk  = [entries[t][4] for t in threads]

    # ----------------------------
    # Throughput Graph
    # ----------------------------
    plt.figure()
    plt.plot(threads, throughputs, marker='o')
    annotate_points(threads, throughputs)
    plt.xlabel("Threads")
    plt.ylabel("Throughput (req/s)")
    plt.title(f"Throughput vs Load — {wl}")
    plt.grid()
    plt.savefig(f"graphs/throughput_{wl}.png")

    # ----------------------------
    # Latency Graph
    # ----------------------------
    plt.figure()
    plt.plot(threads, latencies, marker='o')
    annotate_points(threads, latencies)
    plt.xlabel("Threads")
    plt.ylabel("Latency (ms)")
    plt.title(f"Latency vs Load — {wl}")
    plt.grid()
    plt.savefig(f"graphs/latency_{wl}.png")

    # ----------------------------
    # Combined Graph
    # ----------------------------
    fig, ax1 = plt.subplots()

    ax1.set_xlabel("Threads")
    ax1.set_ylabel("Throughput (req/s)", color='tab:blue')
    ax1.plot(threads, throughputs, marker='o', color='tab:blue')
    annotate_points(threads, throughputs)
    ax1.tick_params(axis='y', labelcolor='tab:blue')

    ax2 = ax1.twinx()
    ax2.set_ylabel("Latency (ms)", color='tab:red')
    ax2.plot(threads, latencies, marker='o', color='tab:red')
    annotate_points(threads, latencies)
    ax2.tick_params(axis='y', labelcolor='tab:red')

    plt.title(f"Throughput & Latency vs Load — {wl}")
    plt.savefig(f"graphs/combined_{wl}.png")

    # ----------------------------
    # Cache Hit Ratio Graph
    # ----------------------------
    plt.figure()
    plt.plot(threads, ratios, marker='o')
    annotate_points(threads, ratios)
    plt.xlabel("Threads")
    plt.ylabel("Cache Hit Ratio")
    plt.title(f"Cache Hit Ratio vs Load — {wl}")
    plt.grid()
    plt.savefig(f"graphs/cache_ratio_{wl}.png")

    # CPU UTIL GRAPH
    plt.figure()
    plt.plot(threads, cpu, marker='o')
    annotate_points(threads, cpu)
    plt.xlabel("Threads")
    plt.ylabel("CPU Util (%)")
    plt.title(f"CPU Utilization vs Load — {wl}")
    plt.grid()
    plt.savefig(f"graphs/cpu_util_{wl}.png")

    # DISK UTIL GRAPH
    plt.figure()
    plt.plot(threads, disk, marker='o')
    annotate_points(threads, disk)
    plt.xlabel("Threads")
    plt.ylabel("Disk %util")
    plt.title(f"Disk Utilization vs Load — {wl}")
    plt.grid()
    plt.savefig(f"graphs/disk_util_{wl}.png")

    # CPU TABLE
    with open(f"graphs/cpu_table_{wl}.txt", "w") as fcpu:
        fcpu.write("Threads\tCPU Util (%)\n")
        for t in threads:
            fcpu.write(f"{t}\t{entries[t][3]:.2f}\n")

    # DISK TABLE
    with open(f"graphs/disk_table_{wl}.txt", "w") as fdisk:
        fdisk.write("Threads\tDisk Util (%)\n")
        for t in threads:
            fdisk.write(f"{t}\t{entries[t][4]:.2f}\n")

print("\nAll graphs saved in graphs/ folder.\n")
