# 🧠 CS744 DECS Project – Multi-Tier HTTP Employee Management Server

**Author:** Arnob Deb 
**Course:** CS744 – Design and Engineering of Computing Systems (DECS)  
**Semester:** Autumn 2025  

---

## 📜 Overview

This project implements a **multi-tier, HTTP-based concurrent server** using **C** and **CivetWeb** that emulates a real-world key-value (KV) storage system with a **cache + database backend architecture**.

It demonstrates:
- Multi-threaded HTTP request handling (via CivetWeb thread pool)  
- RESTful CRUD APIs (`POST`, `GET`, `PUT`, `DELETE`)  
- MySQL-backed persistent database  
- In-memory LRU cache for faster repeated access  
- Distinct **memory-bound** (cache-hit) and **I/O-bound** (DB access) request paths  
- Interactive CLI client (`client.sh`) for easy testing  
- SQL setup script for automated database creation  

---

## 🏗️ System Architecture

```
          ┌────────────────────┐
          │  HTTP Clients (via │
          │  curl / client.sh) │
          └─────────┬──────────┘
                    │
                    ▼
        ┌────────────────────────┐
        │   CivetWeb HTTP Server  │
        │ (multi-threaded, C)     │
        │ - REST API handlers     │
        │ - Thread pool           │
        └─────────┬──────────────┘
                  │
     ┌────────────┴───────────────┐
     │ In-Memory LRU Cache (C)    │
     │ - Frequently accessed data │
     │ - Thread-safe via mutex    │
     └────────────┬───────────────┘
                  │
     ┌────────────┴───────────────┐
     │ Persistent MySQL Database  │
     │ - Stores all employee data │
     │ - Access via libmysqlclient│
     └────────────────────────────┘
```

---

## 📁 Directory Structure

```
decs_project/
├── Makefile
├── server.c
├── db.c
├── db.h
├── cache.c
├── cache.h
├── client.sh
├── setup_company.sql
├── civetweb/
│   ├── include/civetweb.h
│   └── libcivetweb.a
└── .vscode/
```

---

## ⚙️ Setup Instructions

### Prerequisites

```bash
sudo apt update
sudo apt install gcc make libmysqlclient-dev mysql-server
```

### Build CivetWeb
```bash
cd civetweb
make lib
cd ..
```

### Database Setup
```bash
sudo mysql < setup_company.sql
```

### Build Server
```bash
make clean
make
```

### Run Server
```bash
./empserver
Enter port number to start server on: 8080
```

---

## 🧩 Interactive Client Usage

Run:
```bash
./client.sh 127.0.0.1 8080
```

Commands:
```
POST <id> <name> <department> <salary>
GET <id>
PUT <id> <department> <salary>
DELETE <id>
LIST
exit
```

---

## 🧠 Demonstrated Features

| Feature | Description |
|----------|-------------|
| **Concurrency** | CivetWeb uses a thread pool to handle multiple HTTP requests concurrently |
| **RESTful API** | Implements CRUD operations on `/employee` endpoints |
| **Persistence** | MySQL database backend |
| **In-memory Cache** | Thread-safe LRU cache for repeated access |
| **Execution Paths** | Cache hits (memory-bound) vs DB fetch (I/O-bound) |
| **Thread Safety** | Mutex-protected cache operations |
| **Dynamic Port** | Port chosen at runtime |
| **Interactive CLI** | Simplifies testing |

---

## 🧩 Example Logs

```
[DB INSERT] Employee 1
[LIST] Request for all employees
[DB FETCH] Employee 1 (via LIST)
[CACHE HIT] Employee 1 (GET)
[DB UPDATE] Employee 2
[DB DELETE] Employee 1 (cache invalidated)
```

---

## 🧠 Phase-wise Relevance

| Phase | Description | Deliverables |
|--------|--------------|---------------|
| **Phase 1** | Functional correctness | Working REST API, caching, concurrency |
| **Phase 2** | Load testing | Throughput, latency, bottlenecks |
| **Final** | Presentation | Architecture & performance graphs |

---

## 📊 API Reference

| Method | Endpoint | Description | Example |
|--------|-----------|-------------|----------|
| POST | `/employee` | Create new employee | `POST 1 Alice HR 50000` |
| GET | `/employee/<id>` | Retrieve employee info | `GET 1` |
| PUT | `/employee/<id>` | Update employee info | `PUT 1 Finance 70000` |
| DELETE | `/employee/<id>` | Remove employee | `DELETE 1` |
| GET | `/employee/all` | Retrieve all employees | `LIST` |

---

## 🧩 Testing Concurrency

```bash
curl http://127.0.0.1:8080/employee/all &
curl http://127.0.0.1:8080/employee/1 &
curl -X PUT -d "department=Finance&salary=70000" http://127.0.0.1:8080/employee/1 &
wait
```

---

## 📈 Future Work

- Load generator for throughput/latency measurement
- Bottleneck analysis: CPU vs I/O
- Metrics: cache hit %, average latency

---

## 🧠 References

- CivetWeb: https://github.com/civetweb/civetweb  
- MySQL C API: https://dev.mysql.com/doc/c-api/en/  

---
