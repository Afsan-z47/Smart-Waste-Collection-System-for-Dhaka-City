# Waste Collection System

## Directory layout

```
.
├── waste.cpp
├── run_tests.sh
├── inputs/
│   ├── test1_basic.txt
│   ├── test2_small.txt
│   └── test3_flow.txt
└── outputs/            (created by the script)
```

---

## Test 1 — `inputs/test1_basic.txt`

A realistic 12-node Dhaka road graph, 2 trucks, 8 bins, 2 transfer stations, 1 landfill, and a small flow network.


**Expected key results for Test 1**

| Stage | Expected |
|---|---|
| Unreachable nodes from depot | 0 |
| Urgency ranking (top) | Gulshan 100.0, Karwan_Bazar 80.0, Motijheel 69.9, Tejgaon 60.0 |
| Truck_A DP pick | Gulshan + Karwan_Bazar + Tejgaon = **6.00 t** (urgency value 2400.0) |
| Truck_A route | Depot → Karwan_Bazar → Tejgaon → Gulshan → Transfer_N |
| Truck_A total | travel ≈ 62.0 min + service 18.0 min = **80.0 min** (shift 240 → OK) |
| Truck_B DP pick | Motijheel + Mirpur_10 + Dhanmondi_6 = **4.80 t** (value 1792.0) |
| Truck_B route | Depot → Dhanmondi_6 → Motijheel → Mirpur_10 → Transfer_N |
| Truck_B total | travel ≈ 86.0 min + service 18.0 min = **104.0 min** (OK) |
| Bins served | **6 / 8** |
| Tonnage collected | **10.80 t / 13.50 t** |
| Not collected | Uttara (1.00 t), Mohakhali (1.20 t) |
| Max-flow | **2.00 t/day** |
| Min-cut | `Transfer -> Landfill` (cap 2.00) |

---

## Test 2 — `inputs/test2_small.txt`

Minimal 5-node graph, 1 truck, 2 bins — easy to verify by hand.



**Expected key results**

| Stage | Expected |
|---|---|
| Urgency | Bin_A = 100.0, Bin_B = 65.0 |
| DP pick | Bin_A + Bin_B = **3.50 t** (value 1650.0) |
| Route | Depot → Bin_A → Bin_B → Transfer |
| Total time | travel 17.0 + service 12.0 = **29.0 min** (shift 200 → OK) |
| Bins served | **2 / 2** |
| Tonnage collected | **3.50 / 3.50 t** |
| Max-flow | **5.00 t/day** |
| Min-cut | `Transfer -> Landfill` (cap 5.00) |

---

## Test 3 — `inputs/test3_flow.txt`

Designed so the flow network's bottleneck is a single edge with cap 7, and routing has multiple trucks' worth of bins in one load.


**Expected key results**

| Stage | Expected |
|---|---|
| Urgency | Zone_C_bin 100.0, Zone_A_bin 76.1, Zone_B_bin 59.7, Depot_bin 35.8 |
| DP pick | Zone_C_bin + Zone_B_bin + Depot_bin = **8.00 t exactly** (value 1955.0) |
| Route | Depot → Depot_bin → Zone_B_bin → Zone_C_bin → Transfer |
| Total time | travel 26.0 + service 18.0 = **44.0 min** (shift 480 → OK) |
| Bins served | **4 / 4** |
| Tonnage collected | **11.00 / 11.00 t** |
| Max-flow | **7.00 t/day** |
| Min-cut | `Transfer -> Landfill` (cap 7.00) — clear bottleneck |

The flow network is engineered so that although **9 t/day** can physically arrive at `Transfer`, the single outgoing edge `Transfer → Landfill` (cap 7) caps the system; the min-cut is therefore that one edge.

