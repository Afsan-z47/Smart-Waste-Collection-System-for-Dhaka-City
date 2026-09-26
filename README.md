# Smart Waste Collection System for Dhaka City


> Assignment 4
> CSE 4403 (Algorithms)

#### Group ID: 4
 - Student 1 ID: 230041132
 - Student 2 ID: 230041149
 - Student 3 ID: 230041154

> Video: https://youtu.be/PsrVigAg2Vw?si=rgChVIaiCZV8gzL

> Source Code / GitHub: https://github.com/Afsan-z47/Smart-Waste-Collection-System-for-Dhaka-City



## 1. Problem Selection and Justification

### 1.1 Selected Problem

The selected problem is the design of a **sensor-driven smart waste collection system for Dhaka City**.

The system models a waste-management operation in which bins continuously provide information about their current fill level and the amount of time since their previous collection. A fleet of waste-collection trucks must then determine which bins should be collected, which bins can fit within each truck's capacity, how the trucks should travel between locations, and whether the downstream disposal infrastructure can handle the collected waste.

Other options were:

 - Intelligent Cybersecurity Network Attack Detection and Response System
 > Rejected as there were to many detection subsystems, which are being actively updated to cover new exploits. Very difficult to maintain and develop within short time.

 - Register Allocation
 > Rejected as this required substantial amount precomputation before reaching to allocating registers for a compiler.

### 1.2 Why This Problem Is Appropriate for the Course

The implementation is not based on a single algorithm. Instead, each algorithm solves a different part of the same operational problem.

The overall relationship is:

```text
Sensor / city data
        |
        v
+------------------+
| Greedy           |
| Bin prioritizing |
+--------+---------+
         |
         v
+------------------+
| Dynamic          |
| Programming      |
| Truck selection  |
+--------+---------+
         |
         v
+------------------+
| Dijkstra         |
| Travel times     |
+--------+---------+
         |
         v
+------------------+
| Route construction|
| + feasibility     |
+--------+---------+
         |
         v
    Collected waste
         |
         v
+------------------+
| Edmonds-Karp      |
| Disposal capacity |
+--------+---------+
         |
         v
   System throughput
```

This makes the problem a strong fit for an algorithms-focused implementation.

> **Note:** The final report should replace this subsection with the group's actual comparison against the other Assignment 3 proposals if the instructor expects the names and details of those proposals.

---

# 2. Problem Description

Dhaka City's waste collection process involves multiple interacting constraints.

A waste-management system must consider:

1. **Where waste is located.**
2. **How urgent each bin is.**
3. **How much waste a truck can carry.**
4. **How long a truck's shift lasts.**
5. **Where the collected waste can be transferred.**
6. **How much waste transfer stations can accept per day.**
7. **How much waste landfills can process per day.**
8. **Whether the disposal network becomes a bottleneck.**

A simple rule such as "always visit the fullest bin first" is insufficient because the truck may not have enough capacity, the route may exceed the shift duration, or the disposal facilities may not have enough remaining capacity.

The implemented system therefore treats waste collection as a sequence of algorithmic decisions.

---

# 3. System Objectives

The implementation has the following objectives:

### Primary objectives

* Model the city road network as a weighted graph.
* Calculate shortest travel times between collection locations.
* Assign an urgency score to every waste bin.
* Prioritize bins using a greedy ordering.
* Select bins for each truck using 0/1 knapsack dynamic programming.
* Construct a route through the selected bins.
* Ensure that a truck's payload and shift constraints are respected.
* Respect transfer-station capacity while processing multiple trucks.
* Model transfer stations and landfills as a flow network.
* Calculate the maximum disposal throughput using Edmonds-Karp.
* Identify the relationship between collected waste and infrastructure capacity.

### Secondary objectives

* Validate input data.
* Handle unreachable locations.
* Report intermediate results so that the operation of each algorithm can be observed.
* Support an optional user-supplied flow network for independent max-flow/min-cut demonstration.

---

# 4. Overall Architecture

The program is implemented as a single C++17 application.

Its processing pipeline is:

```text
Input
 |
 +--> Road graph
 |
 +--> Transfer stations
 |
 +--> Landfills
 |
 +--> Trucks
 |
 +--> Waste bins
 |
 +--> Optional flow network
 |
 v
Greedy urgency calculation
 |
 v
0/1 Knapsack
 |
 v
Nearest-neighbour route construction
 |
 v
Dijkstra travel-time evaluation
 |
 v
Shift + transfer-capacity feasibility
 |
 v
Collection summary
 |
 v
System disposal network
 |
 v
Edmonds-Karp
 |
 v
Maximum throughput / headroom
 |
 v
Output report
```

The implementation is deliberately divided into functional stages so that the algorithms can be demonstrated independently while still contributing to the same final result.

---

# 5. Input Model

All input is whitespace separated.

The input begins with the road network.

```text
V E
node_0 node_1 ... node_(V-1)
u v weight
...
```

Where:

* `V` = number of road-graph nodes.
* `E` = number of road edges.
* `u` and `v` = endpoint node IDs.
* `weight` = travel time in minutes.

The road graph is undirected.

The road graph is represented by:

```cpp
struct RoadEdge {
    int to;
    double w;
};

vector<vector<RoadEdge>> roadAdj;
```

Each road is inserted in both directions by `addRoad()`.

This produces an adjacency-list representation:

```text
roadAdj[u]
    |
    +--> destination node + travel time
    +--> destination node + travel time
    +--> destination node + travel time
```

---

# 6. Domain Data Structures

The system uses C++ structures to represent the real-world entities.

## 6.1 Waste Bin

```cpp
struct Bin {
    int    id;
    string name;
    int    node;
    double fill;
    double hours;
    int    zone;
    double urgency;
};
```

Each `Bin` contains:

| Field     | Meaning                        |
| --------- | ------------------------------ |
| `id`      | Unique bin identifier          |
| `name`    | Human-readable name            |
| `node`    | Road-graph location            |
| `fill`    | Current waste amount in tonnes |
| `hours`   | Time since last collection     |
| `zone`    | Geographic/service zone        |
| `urgency` | Calculated priority score      |

The first six properties come from the input. `urgency` is calculated by the greedy stage.

---

## 6.2 Truck

```cpp
struct Truck {
    string name;
    double capacity;
    double shift;
    int    start;
};
```

A truck is defined by:

```text
Truck
├── name
├── capacity
├── shift
└── start
```

where:

* `capacity` is the maximum payload in tonnes.
* `shift` is the maximum available route time in minutes.
* `start` is the initial road-graph node.

---

## 6.3 Facility

Transfer stations and landfills use:

```cpp
struct Facility {
    int node;
    double capacity;
    double used;

    double remaining() const {
        return capacity - used;
    }
};
```

The important distinction is that transfer stations have both:

```text
capacity
used
```

so their available capacity changes as trucks finish their routes.

---

## 6.4 RoutePlan

The complete route produced for a truck is represented by:

```cpp
struct RoutePlan {
    vector<int> stops;
    vector<int> nodes;

    double travelTime;
    double serviceTime;
    double totalTime;
    double load;
    double value;

    int  tsIndex;
    bool tsOverflow;
};
```

This structure acts as the output record of the route-planning stage.

It stores:

```text
RoutePlan
 |
 +-- selected bin stops
 +-- road-node sequence
 +-- travel time
 +-- service time
 +-- total time
 +-- collected tonnage
 +-- urgency value
 +-- selected transfer station
```

---

# 7. Stage 1 — Dijkstra's Shortest-Path Algorithm

## 7.1 Purpose

Dijkstra's algorithm determines the shortest travel time between two road-network nodes.

Each road is represented as:

```text
u ---- weight ----> v
```

where the weight is measured in minutes.

For example:

```text
Depot --10--> Karwan_Bazar
Depot --15--> Mirpur_10
Depot -- 8--> Dhanmondi_6
```

The algorithm allows the system to determine the fastest known route between collection points and facilities.

---

## 7.2 Data Structures

The implementation uses:

```cpp
vector<double> dist(V, INF);
vector<int>    prev(V, -1);
```

and a priority queue:

```cpp
priority_queue<pair<double,int>,
               vector<pair<double,int>>,
               greater<pair<double,int>>> pq;
```

The main concepts are:

```text
dist[v]
    = shortest currently known distance to v

prev[v]
    = predecessor of v on that shortest path
```

---

## 7.3 Relaxation

For each outgoing road edge:

```cpp
double nd = d + e.w;
```

The algorithm checks whether the new path is better:

```cpp
if (nd < dist[e.to] - EPS) {
    dist[e.to] = nd;
    prev[e.to] = u;
}
```

This is the standard relaxation operation.

---

## 7.4 Path Reconstruction

Dijkstra also optionally produces a predecessor array.

The function:

```cpp
buildPath(src, dst, prev)
```

walks backward from the destination using `prev` and reverses the result.

For example:

```text
Depot -> Dhanmondi_6 -> Motijheel -> Tejgaon
```

can be reconstructed from predecessor information.

---

## 7.5 Shortest-Path Caching

The implementation uses:

```cpp
map<int, vector<double>> spCache;
```

and:

```cpp
const vector<double>& spFrom(int src);
```

Once Dijkstra has been run from a source node, all distances from that source are cached.

Therefore:

```text
First:
travel(0, 5)
    |
    +--> Dijkstra(0)
    |
    +--> cache all distances

Later:
travel(0, 8)
    |
    +--> reuse cached distances
```

This avoids repeatedly running Dijkstra from the same source.

---

# 8. Stage 2 — Greedy Urgency Prioritisation

## 8.1 Purpose

Before deciding which bins a truck should collect, the system gives every bin an urgency score.

The score combines two properties:

* current fill level;
* time since the previous collection.

The implementation uses:

```cpp
urgency =
    100 * (
        0.60 * normalized_fill +
        0.40 * normalized_age
    );
```

The fill and age values are normalized against the maximum values observed among the current bins.

---

## 8.2 Why a Greedy Algorithm Is Used

The greedy stage answers:

> **Which bins should receive priority?**

It does not attempt to solve the complete truck-routing problem.

The bins are sorted in descending urgency:

```cpp
vector<int> priority(B);
```

The vector contains bin indices rather than copying entire `Bin` objects.

The sorting rule is:

1. higher urgency first;
2. if urgency is effectively equal, higher fill first.

Thus:

```text
Raw bin data
     |
     v
+------------------+
| urgency function |
+--------+---------+
         |
         v
Sorted bin indices
```

---

# 9. Stage 3 — Dynamic Programming with 0/1 Knapsack

## 9.1 Purpose

After prioritisation, a truck still cannot collect every candidate bin because it has limited carrying capacity.

This creates a 0/1 knapsack problem.

For every candidate bin:

```text
weight = waste quantity
value  = urgency
```

For the truck:

```text
capacity = maximum payload
```

The desired result is:

```text
maximum total urgency
subject to total waste <= truck capacity
```

---

## 9.2 Integer Quantisation

The input uses floating-point tonnes, while the DP table requires integer states.

Therefore the program converts tonnes to units of `0.1 t`.

For example:

```text
5.0 t -> 50 units
2.4 t -> 24 units
```

The implementation uses:

```cpp
wt[i] = llround(bins[cand[i]].fill * 10.0);
```

and:

```cpp
W = llround(capTonnes * 10.0);
```

Urgency is similarly scaled by ten.

This avoids floating-point values inside the dynamic-programming table.

---

## 9.3 DP State

The table is:

```cpp
vector<vector<int>> dp(n + 1,
                       vector<int>(W + 1, 0));
```

The interpretation is:

```text
dp[i][w]
=
maximum urgency achievable using
the first i candidate bins
with capacity w
```

For each bin, there are two choices.

### Skip the bin

```cpp
dp[i][w] = dp[i-1][w];
```

### Take the bin

```cpp
dp[i-1][w-wt[i-1]] + val[i-1]
```

The larger value is selected.

---

## 9.4 Reconstruction

After the DP table is complete, the program walks backward through the table.

Whenever:

```cpp
dp[i][w] != dp[i-1][w]
```

the corresponding bin was selected.

The result is:

```cpp
vector<int> chosen;
```

containing the selected bin indices.

Therefore the complete transformation is:

```text
Candidate bins
      |
      v
+---------------------+
|   0/1 Knapsack      |
|                     |
| weight = fill       |
| value  = urgency    |
| capacity = truck    |
+----------+----------+
           |
           v
     selected bins
```

---

# 10. Stage 4 — Route Construction

The knapsack algorithm determines **which bins** should be collected, but not their visiting order.

The selected bins are therefore passed to:

```cpp
nearestNeighbourOrder(...)
```

The algorithm begins at the truck's starting node.

At each step it evaluates:

```cpp
travel(current, candidate)
```

and chooses the closest remaining bin.

The result is an ordered list:

```text
Selected:
[A, B, C, D]

       |
       v

Route:
[A, C, B, D]
```

Travel between nodes is measured using the shortest-path information supplied by Dijkstra.

This means the nearest-neighbour stage is not using direct Euclidean distance. It is using **shortest travel time through the road graph**.

---

# 11. Stage 5 — Route Construction and Feasibility

For each truck, the program creates a `RoutePlan`.

The route starts at:

```text
truck.start
```

then visits:

```text
selected bins
```

then goes to a suitable transfer station and finally returns to the depot.

The travel time is calculated from all route legs.

Service time is calculated by assigning a fixed:

```cpp
SERVICE_MIN = 6.0;
```

minutes to each collected bin.

Therefore:

```text
total time
=
travel time
+
number of collected bins × 6 minutes
```

The final condition is:

```text
totalTime <= truck.shift
```

A route satisfying this condition is considered shift-feasible.

---

# 12. Feasibility Pruning

Knapsack only considers the truck's payload capacity.

It does not directly account for:

* travel distance;
* service time;
* transfer-station capacity;
* return-to-depot travel.

Therefore the implementation performs a second feasibility stage.

The procedure is:

```text
Knapsack selection
       |
       v
Build route
       |
       v
Is route within shift?
     /       \
   YES        NO
    |          |
    v          v
 accept     remove a costly
            stop and retry
```

When the route is too long, the implementation evaluates the additional detour caused by each stop relative to its urgency.

A stop with a relatively large:

```text
detour / urgency
```

ratio becomes a candidate for removal.

The route is then rebuilt.

This process continues until:

```text
route is feasible
```

or no bins remain.

This stage should be understood as a **heuristic feasibility adjustment**, not as a globally optimal vehicle-routing algorithm.

---

# 13. Transfer-Station Capacity

Each transfer station maintains:

```cpp
capacity
used
```

and computes:

```cpp
remaining() = capacity - used
```

A route can only use a station when the station has enough remaining capacity for the complete truck load.

After a successful collection:

```cpp
transfers[plan.tsIndex].used += plan.load;
```

This means the capacity is persistent across trucks.

For example:

```text
Transfer Station
capacity = 50 t/day

Truck A tips 6 t
used = 6 t

Truck B tips 5 t
used = 11 t
```

The next truck therefore sees only:

```text
39 t remaining
```

This prevents different trucks from independently assuming that the station still has its original capacity.

---

# 14. Depot Return

After the transfer station, the route returns to the depot.

Conceptually:

```text
Depot
  |
  v
Bin 1
  |
  v
Bin 2
  |
  v
Bin 3
  |
  v
Transfer Station
  |
  v
Depot
```

The return journey is included in `travelTime` and therefore affects shift feasibility.

This makes the reported shift time represent the complete planned trip rather than just the outbound collection portion.

---

# 15. Stage 6 — Collection Summary

Once all trucks have been processed, the program aggregates their results.

The principal outputs include:

```text
Bins served
Tonnage collected
Collection coverage
Fleet utilisation
Transfer capacity
Landfill capacity
Uncollected bins
```

### Collection coverage

```text
collected waste / total available waste × 100%
```

This indicates how much of the currently available waste was collected during the cycle.

### Fleet utilisation

The final implementation compares collected tonnes against the aggregate payload capacity of the trucks used in the input.

---

# 16. Stage 7 — Disposal Network

Collection is only one part of the waste-management problem.

After the trucks collect the waste, the waste must eventually move through transfer stations and into landfills.

This is modelled as a directed flow network:

```text
                   +----------------+
                   | Transfer A     |
                   +-------+--------+
                           |
                           v
SOURCE ----------------> Landfill A ----+
                           ^             |
                           |             v
                   +-------+--------+   SINK
                   | Transfer B     |   |
                   +-------+--------+   |
                           |             |
                           v             |
                        Landfill B ------+
```

The actual implementation uses:

```cpp
vector<vector<FEdge>> sys;
```

with:

```cpp
struct FEdge {
    int to;
    int rev;
    double cap;
};
```

The `rev` field is used to locate the reverse residual edge.

---

# 17. Flow-Network Capacity Model

The generated disposal network has three types of edges.

## 17.1 Source to Transfer Station

```text
SOURCE -> transfer station
```

Capacity:

```text
transfer station daily capacity
```

This represents how much waste a transfer station can accept.

## 17.2 Transfer Station to Landfill

A transfer station is connected to a landfill when the road graph shows that the landfill is reachable.

These edges represent the ability to transport waste between facilities.

## 17.3 Landfill to Sink

```text
landfill -> SINK
```

Capacity:

```text
landfill daily processing capacity
```

The overall network is therefore:

```text
Source
  |
  +--> Transfer stations
          |
          +--> Reachable landfills
                    |
                    +--> Sink
```

---

# 18. Stage 8 — Edmonds-Karp Maximum Flow

## 18.1 Purpose

The maximum-flow calculation answers:

> What is the maximum amount of waste that the modelled disposal infrastructure could process per day?

The algorithm used is Edmonds-Karp, which is an implementation of the Ford-Fulkerson method using BFS to find augmenting paths.

---

## 18.2 Residual Graph

Every directed flow edge creates:

```text
forward edge
    capacity = C

reverse edge
    capacity = 0
```

During augmentation, the program:

```cpp
e.cap -= push;
g[v][e.rev].cap += push;
```

Thus capacity can be redistributed through the residual network.

---

## 18.3 Augmenting Path

The BFS stores:

```cpp
vector<int> pv;
vector<int> pe;
```

where:

* `pv[v]` = parent node;
* `pe[v]` = edge index used to reach the node.

After reaching the sink, the algorithm finds the bottleneck:

```cpp
push = min(residual capacities on the path)
```

and adds that amount to the total flow.

This continues until no augmenting path remains.

---

# 19. Maximum Flow and Infrastructure Headroom

After computing:

```cpp
double sysFlow = edmondsKarp(sys, FS, FT);
```

the program compares the result with the collected waste:

```cpp
double headroom = sysFlow - totalCollected;
```

This gives three possible interpretations.

### Positive headroom

```text
maximum disposal throughput
>
current collected waste
```

The modelled disposal infrastructure has unused capacity.

### Approximately zero headroom

```text
maximum disposal throughput
≈
current collected waste
```

The system is operating close to its disposal limit.

### Negative headroom

```text
maximum disposal throughput
<
current collected waste
```

The disposal infrastructure is a bottleneck relative to the current collection amount.

The program reports this explicitly.

---

# 20. Min-Cut Interpretation

The implementation can also identify the minimum cut.

After max-flow finishes, the algorithm performs a reachability traversal on the residual graph starting from the source.

Nodes that remain reachable belong to one side of the cut.

An original edge satisfying:

```text
reachable[u] == true
reachable[v] == false
```

crosses the cut.

The capacities of these edges are added to obtain the cut capacity.

The program then verifies:

```text
maximum flow = minimum cut
```

within a floating-point tolerance.

This demonstrates the max-flow/min-cut relationship in the implementation rather than simply calculating maximum flow.

---

# 21. Optional User-Supplied Flow Network

The input format also supports an optional independently supplied flow network.

This allows an additional demonstration of Edmonds-Karp using:

```text
F Q
node names
source sink
Q directed edges
```

The program runs the same max-flow algorithm on this network and prints:

```text
Maximum flow
Minimum cut
Verification
```

This is separate from the automatically generated disposal network.

Thus the system contains two flow demonstrations:

```text
5a
Generated system disposal network
       |
       v
Transfer stations + landfills

5b
Optional user-defined flow network
       |
       v
Direct Edmonds-Karp demonstration
```

---

# 22. Complete Data Flow

The entire application can be summarized as follows:

```text
                        INPUT FILE
                            |
       +--------------------+---------------------+
       |                    |                     |
       v                    v                     v
   Road graph             Bins                  Trucks
       |                    |                     |
       |                    v                     |
       |               Greedy urgency             |
       |                    |                     |
       |                    v                     |
       |              Priority order              |
       |                    |                     |
       +--------------------+---------------------+
                            |
                            v
                    0/1 Knapsack DP
                            |
                            v
                     Selected bins
                            |
                            v
                    Nearest-neighbour
                            |
                            v
                     Dijkstra travel
                            |
                            v
               Shift/facility feasibility
                            |
                            v
                      Truck route
                            |
                            v
                   Collected waste
                            |
               +------------+------------+
               |                         |
               v                         v
       Collection summary       Transfer/Landfill network
                                         |
                                         v
                                  Edmonds-Karp
                                         |
                                         v
                                Maximum throughput
                                         |
                                         v
                                 Infrastructure
                                     headroom
```

---

# 23. Complexity Analysis

Let:

* `V` = number of road-network vertices;
* `E` = number of road-network edges;
* `B` = number of bins;
* `T` = number of trucks;
* `W` = quantised truck capacity;
* `F` = number of nodes in a flow network;
* `Q` = number of flow edges.

## Dijkstra

Using an adjacency list and binary heap:

```text
O((V + E) log V)
```

for one source.

Because shortest paths are cached by source node, repeated requests from the same source do not require another Dijkstra execution.

---

## Greedy sorting

Calculating urgency:

```text
O(B)
```

Sorting the bins:

```text
O(B log B)
```

---

## 0/1 Knapsack

For `n` candidate bins and capacity `W` in quantised units:

```text
O(nW)
```

space:

```text
O(nW)
```

because the implementation stores the complete two-dimensional DP table for reconstruction.

---

## Nearest-neighbour routing

For `n` selected bins, the nearest-neighbour search requires approximately:

```text
O(n²)
```

distance lookups.

The actual shortest-path work is supplied by the cached Dijkstra results.

---

## Edmonds-Karp

For a flow network containing:

```text
F vertices
Q edges
```

the standard Edmonds-Karp bound is:

```text
O(FQ²)
```

The practical runtime depends on the size and structure of the flow network.

---

# 24. Input Validation and Robustness

The program performs a number of validation checks before processing.

Examples include:

```text
V > 0
edge endpoints within range
no negative road weights
valid depot ID
valid transfer-station node
valid landfill node
positive truck capacity
positive truck shift
valid bin node
non-negative bin fill
non-negative bin age
valid flow-network endpoints
```

A central helper is:

```cpp
auto die = [](const string& msg) {
    cerr << "Input error: " << msg << "\n";
    exit(1);
};
```

This makes malformed input fail immediately instead of producing undefined behaviour.

The test suite also includes invalid-input cases such as negative road weights and invalid node IDs.

---

# 25. Testing Strategy

The implementation was tested using multiple focused input files instead of relying only on one normal example.

The tests include:

| Test category         | Purpose                                                  |
| --------------------- | -------------------------------------------------------- |
| Normal case           | Verify complete system operation                         |
| Exact knapsack        | Verify DP chooses the highest-value feasible combination |
| Tight shift           | Verify route feasibility pruning                         |
| Transfer capacity     | Verify station limits                                    |
| Station exhaustion    | Verify capacity is consumed across multiple trucks       |
| Unreachable node      | Verify disconnected bins are not selected                |
| Zero values           | Verify safe normalization for zero fill/age              |
| No transfer station   | Verify missing disposal infrastructure                   |
| Classic max-flow      | Verify Edmonds-Karp                                      |
| Flow bottleneck       | Verify disposal throughput limits                        |
| Invalid negative edge | Verify input validation                                  |
| Invalid node ID       | Verify input validation                                  |

The test runner compiles the source with:

```bash
g++ -O2 -std=c++17 -Wall -o waste waste.cpp
```

and executes all test inputs automatically.

This allows regressions in individual algorithmic stages to be detected without manually running each case.

---

# 26. Example System Behaviour

A typical input describes:

```text
Road network
    12 nodes
    17 roads

Transfer stations
    Transfer_N: 50 t/day
    Transfer_S: 40 t/day

Landfill
    100 t/day

Trucks
    Truck_A: 6 t
    Truck_B: 5 t

Bins
    8 active bins
```

The program then produces output containing:

```text
1. Road Network
   - graph size
   - depot
   - facilities
   - unreachable nodes

2. Greedy Urgency Prioritisation
   - rank
   - bin
   - fill
   - age
   - urgency

3. Capacity-Aware Selection and Routing
   - selected bins
   - truck load
   - urgency value
   - route
   - Dijkstra path for each leg
   - travel time
   - service time
   - total time
   - deferred bins

4. Collection Summary
   - bins served
   - total collected waste
   - collection coverage
   - fleet utilisation
   - transfer capacity usage
   - landfill capacity

5. System Throughput
   - modelled facilities
   - collected waste
   - maximum flow
   - infrastructure headroom
   - bottleneck status
```

The output is therefore not simply a final answer. It provides a trace of the decisions made by each algorithm.

---

# 27. Strengths of the Implementation

The major strength of the system is that the algorithms are assigned different responsibilities.

```text
Greedy
    -> determines urgency

Dynamic Programming
    -> determines capacity-constrained selection

Dijkstra
    -> determines shortest travel times

Nearest Neighbour
    -> determines a practical visit order

Feasibility stage
    -> enforces operational constraints

Edmonds-Karp
    -> determines disposal-network throughput
```

This avoids forcing one algorithm to solve a problem it was not designed for.

Another strength is that the program uses structured data models instead of keeping every piece of information in independent variables.

For example:

```cpp
Bin
Truck
Facility
RoutePlan
RoadEdge
FEdge
```

represent the major entities directly.

The result is easier to extend and easier to inspect during the demonstration.

---

# 28. Limitations

The implementation is a practical educational model rather than a complete production waste-management optimizer.

### 28.1 Routing heuristic

The nearest-neighbour method does not guarantee the globally shortest multi-stop truck route.

It provides a practical route using the shortest-path distances supplied by Dijkstra.

### 28.2 Separate selection and routing objectives

The knapsack stage maximizes urgency subject to payload capacity.

Shift feasibility is checked afterward.

Therefore the final system is a staged heuristic rather than one globally optimized mathematical program combining every constraint.

### 28.3 Fixed service time

Every collected bin is assigned:

```text
6 minutes
```

of service time.

Real collection time would depend on factors such as bin type, accessibility, congestion, and waste quantity.

### 28.4 Disposal model

The system-level flow graph determines network throughput based on transfer-station capacity, landfill capacity, and road reachability between facilities.

It does not perform a detailed operational schedule for every physical truck-to-landfill movement.

### 28.5 Daily model

The current model treats bin contents and facility capacities as a single collection cycle/day rather than simulating a continuous stream of waste over many days.

---

# 29. Possible Future Improvements

Several extensions could make the system more realistic.

### Dynamic traffic

Instead of fixed road weights, road travel times could change according to traffic conditions.

### Multiple trips per truck

A truck could return to a transfer station during the same shift, unload, and perform another collection route.

### More advanced vehicle routing

The nearest-neighbour heuristic could be replaced or improved with a vehicle-routing algorithm or additional local-search procedures.

### Time-dependent urgency

Urgency could incorporate service-level thresholds, population density, environmental constraints, or expected overflow.

### Better disposal scheduling

Collected waste could be explicitly allocated to individual transfer stations and landfills instead of only comparing total collected waste against maximum network throughput.

### Real sensor integration

The current text input could eventually be replaced by data from an IoT or city-monitoring system.

---

# 30. Video Demonstration Plan

The recorded demonstration can follow the same stages as the program.

## Scene 1 — Problem

Show the idea:

```text
Bins → Trucks → Transfer Stations → Landfills
```

Explain that the challenge is not simply collecting waste, but doing so under capacity, time, and infrastructure constraints.

## Scene 2 — Input

Show `test_basic.txt`.

Explain the input groups:

```text
road network
transfer stations
landfills
trucks
bins
flow network
```

## Scene 3 — Greedy

Show the urgency table.

Explain:

```text
urgency =
60% fill
+
40% age
```

and demonstrate that bins are ranked.

## Scene 4 — Dynamic Programming

Show truck capacity and candidate bins.

Explain:

```text
weight = waste
value = urgency
capacity = truck capacity
```

Then show the selected subset.

## Scene 5 — Dijkstra and Route

Show a route such as:

```text
Depot
   -> Bin A
   -> Bin C
   -> Bin B
   -> Transfer Station
   -> Depot
```

Then show the individual shortest-path legs and their travel times.

## Scene 6 — Feasibility

Show:

```text
travel time
+
service time
=
total route time
```

and compare it to the truck's shift.

## Scene 7 — Disposal Network

Show:

```text
Source
   |
Transfer stations
   |
Landfills
   |
Sink
```

and explain the capacity on each stage.

## Scene 8 — Edmonds-Karp

Show:

```text
Maximum flow
Minimum cut
Headroom
```

Then explain what happens when the disposal network becomes the bottleneck.

## Scene 9 — Final Output

End with the collection summary and the sentence:

> **The system combines four major algorithmic ideas into one workflow: priority selection, capacity-aware loading, shortest-path routing, and disposal-network capacity analysis.**

---

# 31. Conclusion

The implemented Smart Waste Collection System demonstrates how classical algorithms can be combined to address a realistic engineering problem.

The system begins with a model of the city road network, waste bins, collection trucks, transfer stations, and landfills. A greedy algorithm determines bin urgency, dynamic programming selects a feasible set of bins for each truck, Dijkstra's algorithm supplies shortest travel times, and a routing heuristic produces a visit order. Route feasibility is then checked against the truck's shift and transfer-station capacity.

After collection, the downstream infrastructure is represented as a flow network. Edmonds-Karp calculates the maximum disposal throughput and the corresponding minimum cut exposes capacity bottlenecks.

The central design idea is therefore:

```text
Prioritise
    ↓
Select
    ↓
Route
    ↓
Collect
    ↓
Dispose
    ↓
Measure infrastructure capacity
```

Rather than treating sorting, greedy algorithms, dynamic programming, shortest paths, and max-flow as unrelated course exercises, this project demonstrates how they can be combined into a single computational workflow for a practical engineering problem.

---

## Appendix A — Main C++ Structures

```cpp
struct RoadEdge {
    int to;
    double w;
};

struct Bin {
    int    id;
    string name;
    int    node;
    double fill;
    double hours;
    int    zone;
    double urgency;
};

struct Truck {
    string name;
    double capacity;
    double shift;
    int    start;
};

struct Facility {
    int node;
    double capacity;
    double used;
};

struct RoutePlan {
    vector<int> stops;
    vector<int> nodes;
    double travelTime;
    double serviceTime;
    double totalTime;
    double load;
    double value;
    int tsIndex;
    bool tsOverflow;
};

struct FEdge {
    int to;
    int rev;
    double cap;
};
```

These structures form the main internal representation of the system.

---

## Appendix B — Main Algorithms

```text
Dijkstra
    Input:  road graph + source
    Output: shortest distances + predecessors

Greedy
    Input:  bin fill + bin age
    Output: urgency ranking

0/1 Knapsack
    Input:  candidate bins + truck capacity
    Output: selected bin subset

Nearest Neighbour
    Input:  selected bins + shortest-path distances
    Output: visit order

Route Feasibility
    Input:  route + shift + facility capacities
    Output: accepted route or pruned route

Edmonds-Karp
    Input:  directed capacity network
    Output: maximum flow

Min-Cut
    Input:  residual graph after max-flow
    Output: bottleneck cut edges
```

---

