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

### Design changes since v0

| Issue                                | Earlier behaviour                                                   | Fix/Improvement                                                                |
| ------------------------------------ | ------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| ENOBUFS on `bcache_pop()`            | flush waited for a *full* producer slab; pool ran out of full slabs | split pool into **full / empty** lists & push returns slab instantly           |
| Duplicate ICMP `DUP!`                | double‑submit & buffer reuse                                        | ensured TX‑CQ complete → `bcache_push` → pool path; added debug bitmap         |
| Segfault on `xsk_ring_prod__reserve` | FILL ring pointer was size 0 (dummy)                                | unified all sockets to **shared** `bpool->umem_fq`; submit after every reserve |

---

## 3. Socket Bring‑up Checklist

1. **Create / reuse `umem`** with correct `fill_size/comp_size`.
2. Pass same `umem` handle to every `xsk_socket__create()` and set `XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD`.
3. After socket creation, call `xsk_prime_fill_queue()` **once** to pre‑fill FILL ring.
4. TX path must always:

   * reserve → fill desc → **submit**
   * on CQ completion: `bcache_push(addr)`;
     if `n_buffers_prod >= slab_threshold` → swap slab to pool.

---

## 4. Current Performance (single queue, MTU 1500)

| Metric                  | Value                            |
| ----------------------- | -------------------------------- |
| `iperf3 -c 10.0.0.2 -Z` | **4.7 Gb/s** (single TCP stream) |
| Retransmissions         | 1 in 10 s                        |
| CPU (perf stat)         | \~48 % of one core; IPC ≈ 1.2    |


