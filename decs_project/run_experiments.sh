#!/usr/bin/env bash

############################################
# Flexible Experiment Runner for Phase 2
############################################

# --- USER CONFIGURABLE --- #
HOST=$1              # e.g., localhost or 127.0.0.1
PORT=$2              # e.g., 8080
WORKLOAD=$3          # put_all / get_all / get_popular / mix
DURATION=$4          # seconds (300 for 5 mins)
LOADGEN_CORES=$5     # e.g., 4-7
THREAD_LIST=$6       # e.g., "4 8 16 32 64"

# Additional workload params depending on workload type
PARAM1=$7
PARAM2=$8
PARAM3=$9
PARAM4=${10}
PARAM5=${11}

############################################
# CHECK ARGUMENT COUNT
############################################

if [ $# -lt 6 ]; then
    echo "Usage:"
    echo "./run_experiments.sh <host> <port> <workload> <duration> <cpu_cores> \"thread_list\" [params...]"
    echo ""
    echo "Examples:"
    echo "  ./run_experiments.sh localhost 8080 get_popular 300 4-7 \"4 8 16 32\" 10000 10"
    echo "  ./run_experiments.sh localhost 8080 get_all 300 4-7 \"4 8 16 32\" 50000"
    echo "  ./run_experiments.sh localhost 8080 mix 300 4-7 \"4 8 16 32\" 0.6 0.3 0.1 10000 100"
    exit 1
fi

############################################
# CREATE RESULTS DIRECTORY
############################################
mkdir -p results

echo "======================================="
echo "  Running Experiments for workload: $WORKLOAD"
echo "  Host      : $HOST"
echo "  Port      : $PORT"
echo "  Duration  : $DURATION seconds"
echo "  CPU cores : $LOADGEN_CORES"
echo "  Threads   : $THREAD_LIST"
echo "======================================="

############################################
# MAIN LOOP
############################################

for T in $THREAD_LIST; do
    echo ""
    echo ">>> Running with $T threads..."

    OUTFILE="results/out_${WORKLOAD}_${T}.txt"

    # Reset cache counters on server
    curl -s http://$HOST:$PORT/metrics/reset > /dev/null

    echo ">>> Starting background CPU & Disk monitoring..."

    # Start CPU monitoring (server cores assumed 0-1)
    mpstat -P 0,1 1 $((DURATION/2)) > cpu_tmp.txt &
    MPSTAT_PID=$!

    # Start Disk I/O monitoring
    iostat -dx 1 $((DURATION/2)) > disk_tmp.txt &
    IOSTAT_PID=$!

    # Run the load generator
    case "$WORKLOAD" in

        put_all)
            taskset -c $LOADGEN_CORES ./loadgen $HOST $PORT $T $DURATION put_all \
                > "$OUTFILE"
            ;;

        get_all)
            taskset -c $LOADGEN_CORES ./loadgen $HOST $PORT $T $DURATION get_all "$PARAM1" \
                > "$OUTFILE"
            ;;

        get_popular)
            taskset -c $LOADGEN_CORES ./loadgen $HOST $PORT $T $DURATION get_popular "$PARAM1" "$PARAM2" \
                > "$OUTFILE"
            ;;

        mix)
            taskset -c $LOADGEN_CORES ./loadgen $HOST $PORT $T $DURATION mix "$PARAM1" "$PARAM2" "$PARAM3" "$PARAM4" "$PARAM5" \
                > "$OUTFILE"
            ;;
    esac

    # Append cache metrics AFTER run
    echo "" >> "$OUTFILE"
    echo "# CACHE METRICS" >> "$OUTFILE"
    curl -s http://$HOST:$PORT/metrics >> "$OUTFILE"

    echo "" >> "$OUTFILE"
    echo "# CPU UTILIZATION" >> "$OUTFILE"
    cat cpu_tmp.txt >> "$OUTFILE"

    echo "" >> "$OUTFILE"
    echo "# DISK UTILIZATION" >> "$OUTFILE"
    cat disk_tmp.txt >> "$OUTFILE"

    # Cleanup background monitors
    kill $MPSTAT_PID >/dev/null 2>&1
    kill $IOSTAT_PID >/dev/null 2>&1

    echo "    Finished run with $T threads → output saved to $OUTFILE"

done

rm cpu_tmp.txt disk_tmp.txt >/dev/null 2>&1

echo ""
echo "======================================="
echo "All experiment runs completed!"
echo "======================================="
