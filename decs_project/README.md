# 🧠 CS744 DECS Project – Multi-Tier HTTP Employee Management Server

**Author:** Arnob Deb  
**Roll:** 25M0779  
**Course:** CS744 – Design and Engineering of Computing Systems (DECS)  
**Semester:** Autumn 2025  

---

## 📜 Overview

This project implements a **multi-tier, HTTP-based concurrent server** using **C** and **CivetWeb**, backed by a MySQL database and an in-memory LRU cache.  
In **Phase 1**, the focus was on functional correctness.  
In **Phase 2**, we designed and evaluated load testing infrastructure, identified performance bottlenecks, and collected real metrics.

---

## ⚙️ Phase-wise Implementation

### ✅ Phase 1: Functional System

- Multi-threaded CivetWeb HTTP server
- RESTful CRUD APIs (`POST`, `GET`, `PUT`, `DELETE`)
- Thread-safe LRU cache using mutexes
- Persistent MySQL backend
- Client script (`client.sh`) to interactively test APIs
- SQL setup (`setup_company.sql`) for preloading schema and test data

### 🧪 Phase 2: Load Testing and Evaluation

- ⏱️ Closed-loop `loadgen.c` to simulate concurrent clients
- 🧮 Workloads:
  - `put_all`: pure writes
  - `get_all`: pure reads (DB-bound)
  - `get_popular`: skewed reads (cache-heavy)
  - `mix`: custom GET/PUT/DELETE mixes
- 📊 Metrics:
  - Throughput, latency, cache hits
  - CPU + Disk utilization via `mpstat` and `iostat`
- 📈 `plot_results.py` to visualize graphs
- 🧵 Core pinning with `taskset` for isolation
- 📂 `run_experiments.sh` to automate multiple thread-level test runs
- 📌 Cache metrics endpoint: `/metrics`, `/metrics/reset`

---

## 🏗️ System Architecture

```
    ┌──────────────────────┐
    │    HTTP Clients      │
    └─────────┬────────────┘
              │
      ┌───────▼────────────┐
      │ CivetWeb HTTP Server│
      │  - REST Handlers    │
      │  - Thread Pool      │
      └───────┬────────────┘
              │
     ┌────────▼─────────────┐
     │ In-Memory LRU Cache  │
     │  - Mutex-Protected   │
     └────────┬─────────────┘
              │
     ┌────────▼─────────────┐
     │ MySQL Database       │
     │  - Employee Records  │
     └──────────────────────┘
```

---

## 📁 Directory Structure

```
decs_project/
├── server.c
├── cache.c / cache.h
├── db.c / db.h
├── loadgen.c
├── run_experiments.sh
├── plot_results.py
├── detect_saturation.py
├── Makefile
├── setup_company.sql
├── client.sh
├── results/
├── graphs/
└── civetweb/
```

---

## 🛠️ Setup Instructions

### 1. Prerequisites
```bash
sudo apt update
sudo apt install gcc make libmysqlclient-dev mysql-server sysstat
```

### 2. Build CivetWeb
```bash
cd civetweb
make lib
cd ..
```

### 3. Initialize Database
```bash
sudo mysql < setup_company.sql
```

### 4. Build Everything
```bash
make clean
make
```

---

## 🚀 Running the Server

```bash
taskset -c 0-1 ./empserver
```
> Recommended: pin server to CPU cores 0–1

Enter port (e.g. `8080`) when prompted.

---

## 🤖 Load Generation

### A. Manual Usage of loadgen

```bash
taskset -c 2-3 ./loadgen <host> <port> <threads> <duration> <workload> [args]
```

Examples:
```bash
./loadgen localhost 8080 8 300 put_all
./loadgen localhost 8080 8 300 get_all 50000
./loadgen localhost 8080 8 300 get_popular 10000 100
./loadgen localhost 8080 8 300 mix 0.6 0.3 0.1 10000 100
```

### B. Automated Testing

```bash
./run_experiments.sh <host> <port> <workload> <duration> <cores> "<threads>" [args...]
```

Examples:
```bash
./run_experiments.sh localhost 8080 put_all 300 2-3 "4 8 16 24 32"
./run_experiments.sh localhost 8080 get_all 300 2-3 "4 8 16 24 32" 50000
./run_experiments.sh localhost 8080 get_popular 300 2-3 "4 8 16 24 32" 10000 100
./run_experiments.sh localhost 8080 mix 300 2-3 "4 8 16 24 32" 0.6 0.3 0.1 10000 100
```

> Results are stored under `results/` and graphs go in `graphs/`

---

## 📈 Graph Generation

```bash
python3 plot_results.py
```

This script generates:
- Throughput vs threads
- Latency vs threads
- Cache hit ratio
- CPU & Disk usage

---

## 🧠 API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET    | `/employee/all` | Get all employees |
| GET    | `/employee/<id>` | Get by ID |
| POST   | `/employee` | Create employee |
| PUT    | `/employee/<id>` | Update |
| DELETE | `/employee/<id>` | Delete |
| GET    | `/metrics` | View cache metrics |
| GET    | `/metrics/reset` | Reset cache counters |

---

## 📊 Metrics Output

Each result file `results/out_<workload>_<threads>.txt` contains:

- SUCCESS / FAIL / TOTAL
- THROUGHPUT
- AVG_LATENCY
- Cache hits/misses
- CPU & Disk stats

---

## 📌 Phase-Wise Summary

| Phase | Focus | Deliverables |
|-------|-------|--------------|
| Phase 1 | Functional correctness | REST APIs, DB, Cache |
| Phase 2 | Load testing & analysis | Workloads, metrics, bottlenecks |

---

## 🧠 References

- [CivetWeb](https://github.com/civetweb/civetweb)
- [MySQL C API](https://dev.mysql.com/doc/c-api/en/)
- [Matplotlib](https://matplotlib.org/)
