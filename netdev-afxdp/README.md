# AF\_XDP vRouter – Memory‑Pool & RX/TX Fast‑Path Notes

> **Status:** draft (2025‑05‑31)

---

## 1. Overview

This document captures the design discussion, issues, and current status of the AF\_XDP‑based datapath integration for vRouter.  Focus areas so far:

* **Memory‑pool (“bpool / bcache”) design**
* AF\_XDP socket bring‑up & queue handling
* Performance validation with `iperf3`

---

## 2. Memory‑Pool Architecture

* **bpool** is global & shared across xsk sockets.

  * `slabs_full`  – full slabs ready for consumers.
  * `slabs_empty` – empty slabs available for producers.
* **bcache** is per‑CPU (or per RX/TX thread) and keeps two private slabs:

  * `slab_cons`: readonly by consumer
  * `slab_prod`: write‑only by producer
* Synchronisation uses a single spin‑lock inside `bpool`;  cache ↔ pool swaps occur slab‑by‑slab, avoiding per‑buffer locks.

## 4. Current Performance (single queue, MTU 1500)

| Metric                  | Value                            |
| ----------------------- | -------------------------------- |
| `iperf3 -c 10.0.0.2 -Z` | **4.7 Gb/s** (single TCP stream) |
| Retransmissions         | 1 in 10 s                        |
| CPU (perf stat)         | \~48 % of one core; IPC ≈ 1.2    |


