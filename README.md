# Smart Waste Collection System - Rigorous Tests

This directory follows the same layout as the project test setup:

    inputs/    test input files
    outputs/   captured program output
    run_tests.sh

Build the program in the parent project directory first:

    g++ -O2 -std=c++17 -Wall -Wextra -pedantic -o waste waste.cpp

Then run:

    bash run_tests.sh

The tests are intentionally small so that each algorithmic property is easy to inspect.

## Tests

### test_normal.txt
Normal integrated case using Dijkstra, greedy urgency, 0/1 knapsack, routing, transfer stations and a user flow network.

### test_knapsack_exact.txt
Three bins with a 5 t truck. A=3 t and B=2 t together give 155 urgency points, while C=4 t gives 96, so the DP stage should prefer A+B.

### test_tight_shift.txt
The truck has enough payload capacity for all three bins, but the full route cannot fit the 30-minute shift. This exercises route-feasibility pruning.

### test_transfer_capacity.txt
Two 4 t trucks share a transfer station with only 4 t/day capacity. The first truck may consume the station's capacity; the second must not push station usage above 4 t/day.

### test_unreachable.txt
One bin lies outside the depot's connected component. It must not become a collection candidate.

### test_zero_values.txt
All bins have zero fill and zero age. Exercises the maxFill/maxHours zero normalization paths without division-by-zero.

### test_no_transfer.txt
There are bins but no transfer station. A collection route should not be considered fully disposed/tipped.

### test_flow_classic.txt
Classic Edmonds-Karp regression network. Expected maximum flow and minimum cut are both 23 t/day.

### test_flow_bottleneck.txt
The generated transfer/landfill network has 7 t/day total landfill capacity, while collection can reach 8 t/day. Expected system max-flow is 7 t/day.

### test_station_exhaustion.txt
Same capacity-exhaustion scenario as test_transfer_capacity, kept separately for quick regression testing when changing station accounting.

### test_invalid_negative_edge.txt
Must exit non-zero because a negative Dijkstra edge weight is rejected.

### test_invalid_node.txt
Must exit non-zero because road endpoint 9 is outside the declared V=3 node range.

## Important parser note

Do not add `// comments` to the input files. The parser uses `operator>>` tokens and does not implement comment syntax.

## What this suite is meant to catch

- shortest-path and reachability mistakes
- incorrect 0/1 knapsack reconstruction
- shift-feasibility failures
- transfer-capacity overflow
- accidental selection of unreachable bins
- zero-normalization / divide-by-zero problems
- routes with no disposal station
- Edmonds-Karp residual-network errors
- incorrect min-cut extraction
- invalid-input handling
