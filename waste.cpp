/* ============================================================================
   SMART WASTE COLLECTION SYSTEM FOR DHAKA CITY
   Sensor-driven route optimisation and capacity-aware scheduling
   ----------------------------------------------------------------------------
   Four complementary algorithmic tools working as one city-scale concept:

     1. Dijkstra          -> shortest-path routing on the live road graph
     2. Greedy            -> urgency-based prioritisation of bins
     3. Dynamic Prog.     -> capacity-aware 0/1 knapsack selection per truck
     4. Edmonds-Karp      -> max-flow / min-cut on the disposal network

   ----------------------------------------------------------------------------
   INPUT FORMAT  (all tokens whitespace separated, read from stdin)

     V E
     name_0 name_1 ... name_{V-1}
     u v w                        (E lines, undirected, w = minutes)
     depot
     K
     node capacity                (K lines, transfer stations, tonnes/day)
     M
     node capacity                (M lines, landfills, tonnes/day)
     T
     truckName capacity shift startNode          (T lines)
     B
     id binName node fill hours zone             (B lines)
     F Q
     fname_0 ... fname_{F-1}
     source sink
     u v capacity                 (Q lines, directed flow network)

   Build:  g++ -O2 -std=c++17 -o waste waste.cpp
   Run:    ./waste < dhaka_input.txt
   ============================================================================ */

#include <bits/stdc++.h>
using namespace std;

static const double INF         = 1e18;
static const double EPS         = 1e-9;
static const double SERVICE_MIN = 6.0;   // minutes spent servicing one bin

/* ==========================================================================
   1.  ROAD GRAPH  +  DIJKSTRA'S ALGORITHM
   ========================================================================== */

struct RoadEdge { int to; double w; };

int                              V;         // number of road-graph nodes
vector<string>                   nodeName;
vector<vector<RoadEdge> >        roadAdj;

void addRoad(int u, int v, double w) {
    RoadEdge a; a.to = v; a.w = w;  roadAdj[u].push_back(a);
    RoadEdge b; b.to = u; b.w = w;  roadAdj[v].push_back(b);
}

/*  Dijkstra from a single source.
    Repeatedly expands the currently cheapest known node and relaxes its
    neighbours.  Optionally returns the predecessor array so a full path
    can be reconstructed.                                                */
vector<double> dijkstra(int src, vector<int>* prevOut = NULL) {
    vector<double> dist(V, INF);
    vector<int>    prev(V, -1);

    typedef pair<double,int> P;
    priority_queue<P, vector<P>, greater<P> > pq;

    dist[src] = 0.0;
    pq.push(P(0.0, src));

    while (!pq.empty()) {
        P top = pq.top(); pq.pop();
        double d = top.first;
        int    u = top.second;
        if (d > dist[u] + EPS) continue;          // stale entry

        for (size_t i = 0; i < roadAdj[u].size(); ++i) {
            const RoadEdge& e = roadAdj[u][i];
            double nd = d + e.w;
            if (nd < dist[e.to] - EPS) {          // relaxation step
                dist[e.to] = nd;
                prev[e.to] = u;
                pq.push(P(nd, e.to));
            }
        }
    }
    if (prevOut) *prevOut = prev;
    return dist;
}

vector<int> buildPath(int src, int dst, const vector<int>& prev) {
    vector<int> path;
    if (src == dst) { path.push_back(src); return path; }
    int cur = dst;
    while (cur != -1) {
        path.push_back(cur);
        if (cur == src) break;
        cur = prev[cur];
    }
    if (path.empty() || path.back() != src) return vector<int>(); // unreachable
    reverse(path.begin(), path.end());
    return path;
}

/*  Cache of single-source shortest paths (the road graph is small, and the
    same stop nodes are queried many times while assembling routes).       */
map<int, vector<double> > spCache;

const vector<double>& spFrom(int src) {
    map<int, vector<double> >::iterator it = spCache.find(src);
    if (it == spCache.end())
        it = spCache.insert(make_pair(src, dijkstra(src))).first;
    return it->second;
}

double travel(int a, int b) {
    if (a == b) return 0.0;
    return spFrom(a)[b];
}

/* ==========================================================================
   DOMAIN MODEL
   ========================================================================== */

struct Bin {
    int    id;
    string name;
    int    node;      // road-graph node the bin sits on
    double fill;      // tonnes currently inside
    double hours;     // hours since last collection
    int    zone;
    double urgency;   // computed by the greedy stage
};

struct Truck {
    string name;
    double capacity;  // tonnes
    double shift;     // minutes
    int    start;     // start node (depot)
};

struct Facility { int node; double capacity; };

struct RoutePlan {
    vector<int> stops;        // bin indices in visit order
    vector<int> nodes;        // full node sequence (start ... end facility)
    double travelTime;
    double serviceTime;
    double totalTime;
    double load;
    double value;
    RoutePlan() : travelTime(0), serviceTime(0), totalTime(0),
                  load(0), value(0) {}
};

/* ==========================================================================
   3.  DYNAMIC PROGRAMMING  --  0/1 KNAPSACK
   --------------------------------------------------------------------------
   For one truck:
       weight  = bin fill, quantised to 0.1 t units  (integer arithmetic)
       value   = urgency score, quantised to 0.1 units
       capacity= truck load limit
   The DP table evaluates every combination without exceeding the limit.
   ========================================================================== */

vector<int> knapsackSelect(const vector<Bin>& bins,
                           const vector<int>& cand,
                           double capTonnes)
{
    int W = (int)llround(capTonnes * 10.0);       // capacity in 0.1 t units
    int n = (int)cand.size();
    if (W <= 0 || n == 0) return vector<int>();

    vector<int> wt(n), val(n);
    for (int i = 0; i < n; ++i) {
        wt[i]  = (int)llround(bins[cand[i]].fill    * 10.0);
        val[i] = (int)llround(bins[cand[i]].urgency * 10.0);
    }

    vector<vector<int> > dp(n + 1, vector<int>(W + 1, 0));

    for (int i = 1; i <= n; ++i) {
        for (int w = 0; w <= W; ++w) {
            dp[i][w] = dp[i - 1][w];                       // skip item i
            if (wt[i - 1] <= w)                            // take item i
                dp[i][w] = max(dp[i][w], dp[i - 1][w - wt[i - 1]] + val[i - 1]);
        }
    }

    /* ---- reconstruct the chosen subset ---- */
    vector<int> chosen;
    int w = W;
    for (int i = n; i >= 1; --i) {
        if (dp[i][w] != dp[i - 1][w]) {
            chosen.push_back(cand[i - 1]);
            w -= wt[i - 1];
        }
    }
    return chosen;
}

/* ==========================================================================
   ROUTE CONSTRUCTION  (greedy nearest-neighbour over Dijkstra distances)
   ========================================================================== */

vector<int> nearestNeighbourOrder(int startNode,
                                  const vector<int>& binIdx,
                                  const vector<Bin>& bins)
{
    vector<int>  order;
    vector<char> used(binIdx.size(), 0);
    int cur = startNode;

    for (size_t k = 0; k < binIdx.size(); ++k) {
        int    best = -1;
        double bd   = INF;
        for (size_t i = 0; i < binIdx.size(); ++i) {
            if (used[i]) continue;
            double d = travel(cur, bins[binIdx[i]].node);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best < 0) break;
        used[best] = 1;
        order.push_back(binIdx[best]);
        cur = bins[binIdx[best]].node;
    }
    return order;
}

RoutePlan buildPlan(int startNode,
                    const vector<int>& order,
                    const vector<Bin>& bins,
                    const vector<Facility>& transfers)
{
    RoutePlan p;
    p.stops = order;
    p.nodes.push_back(startNode);

    int cur = startNode;
    for (size_t i = 0; i < order.size(); ++i) {
        int b = order[i];
        p.travelTime += travel(cur, bins[b].node);
        p.nodes.push_back(bins[b].node);
        cur          = bins[b].node;
        p.load      += bins[b].fill;
        p.value     += bins[b].urgency;
        p.serviceTime += SERVICE_MIN;
    }

    /* head for the nearest transfer station (unload / tip) */
    int    bestTS = -1;
    double bd     = INF;
    for (size_t i = 0; i < transfers.size(); ++i) {
        double d = travel(cur, transfers[i].node);
        if (d < bd) { bd = d; bestTS = transfers[i].node; }
    }
    if (bestTS >= 0) {
        p.travelTime += bd;
        p.nodes.push_back(bestTS);
    }

    p.totalTime = p.travelTime + p.serviceTime;
    return p;
}

/* ==========================================================================
   4.  MAX-FLOW  --  EDMONDS-KARP  (+ MIN-CUT)
   ========================================================================== */

struct FEdge { int to, rev; double cap; };
struct OrigEdge { int u, v; double cap; };

void addFEdge(vector<vector<FEdge> >& g, int u, int v, double cap) {
    FEdge a; a.to = v; a.rev = (int)g[v].size(); a.cap = cap;
    FEdge b; b.to = u; b.rev = (int)g[u].size(); b.cap = 0.0;
    g[u].push_back(a);
    g[v].push_back(b);
}

double edmondsKarp(vector<vector<FEdge> >& g, int s, int t) {
    int n = (int)g.size();
    double flow = 0.0;

    while (true) {
        vector<int>  pv(n, -1), pe(n, -1);
        vector<char> vis(n, 0);
        queue<int>   q;
        q.push(s); vis[s] = 1;

        /* BFS to find the shortest augmenting path in the residual graph */
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int i = 0; i < (int)g[u].size(); ++i) {
                FEdge& e = g[u][i];
                if (!vis[e.to] && e.cap > EPS) {
                    vis[e.to] = 1;
                    pv[e.to]  = u;
                    pe[e.to]  = i;
                    q.push(e.to);
                }
            }
        }
        if (!vis[t]) break;                      // no more augmenting paths

        double push = INF;
        for (int v = t; v != s; v = pv[v])
            push = min(push, g[pv[v]][pe[v]].cap);

        for (int v = t; v != s; v = pv[v]) {
            FEdge& e = g[pv[v]][pe[v]];
            e.cap          -= push;
            g[v][e.rev].cap += push;
        }
        flow += push;
    }
    return flow;
}

/* ==========================================================================
   MAIN
   ========================================================================== */

int main() {
    ios::sync_with_stdio(false);
    cin.tie(NULL);

    /* ---------------------------------------------------------------- *
     *  PARSE INPUT                                                     *
     * ---------------------------------------------------------------- */
    if (!(cin >> V)) {
        cerr << "Error: no input supplied.\n";
        return 1;
    }
    int E; cin >> E;

    nodeName.resize(V);
    for (int i = 0; i < V; ++i) cin >> nodeName[i];

    roadAdj.assign(V, vector<RoadEdge>());
    for (int i = 0; i < E; ++i) {
        int u, v; double w;
        cin >> u >> v >> w;
        addRoad(u, v, w);
    }

    int depot;  cin >> depot;

    int K;  cin >> K;
    vector<Facility> transfers(K);
    for (int i = 0; i < K; ++i) cin >> transfers[i].node >> transfers[i].capacity;

    int M;  cin >> M;
    vector<Facility> landfills(M);
    for (int i = 0; i < M; ++i) cin >> landfills[i].node >> landfills[i].capacity;

    int T;  cin >> T;
    vector<Truck> trucks(T);
    for (int i = 0; i < T; ++i)
        cin >> trucks[i].name >> trucks[i].capacity >> trucks[i].shift >> trucks[i].start;

    int B;  cin >> B;
    vector<Bin> bins(B);
    for (int i = 0; i < B; ++i)
        cin >> bins[i].id >> bins[i].name >> bins[i].node
            >> bins[i].fill >> bins[i].hours >> bins[i].zone;

    /* flow network */
    int F, Q;  cin >> F >> Q;
    vector<string> fname(F);
    for (int i = 0; i < F; ++i) cin >> fname[i];
    int fs, ft;  cin >> fs >> ft;

    vector<vector<FEdge> > fg(F);
    vector<OrigEdge>       orig;
    for (int i = 0; i < Q; ++i) {
        int u, v; double c;
        cin >> u >> v >> c;
        addFEdge(fg, u, v, c);
        OrigEdge oe; oe.u = u; oe.v = v; oe.cap = c;
        orig.push_back(oe);
    }

    cout << fixed;
    cout << "================================================================\n";
    cout << "  SMART WASTE COLLECTION SYSTEM  -  DHAKA CITY\n";
    cout << "  Sensor-driven route optimisation & capacity-aware scheduling\n";
    cout << "================================================================\n";

    /* ---------------------------------------------------------------- *
     *  1.  ROAD NETWORK                                                *
     * ---------------------------------------------------------------- */
    cout << "\n--- 1. ROAD NETWORK --------------------------------------------\n";
    cout << "  Nodes : " << V << "      Edges : " << E << "\n";
    cout << "  Depot : " << nodeName[depot] << " (node " << depot << ")\n";

    cout << "  Transfer stations : ";
    for (int i = 0; i < K; ++i) {
        if (i) cout << ", ";
        cout << nodeName[transfers[i].node] << " (node " << transfers[i].node
             << ", cap " << setprecision(1) << transfers[i].capacity << " t/day)";
    }
    cout << "\n  Landfills         : ";
    for (int i = 0; i < M; ++i) {
        if (i) cout << ", ";
        cout << nodeName[landfills[i].node] << " (node " << landfills[i].node
             << ", cap " << setprecision(1) << landfills[i].capacity << " t/day)";
    }
    cout << "\n";

    /* sanity: reachability from the depot */
    {
        vector<double> d = dijkstra(depot);
        int unreachable = 0;
        for (int i = 0; i < V; ++i) if (d[i] >= INF / 2) ++unreachable;
        cout << "  Unreachable nodes from depot : " << unreachable << "\n";
    }

    /* ---------------------------------------------------------------- *
     *  2.  GREEDY  --  urgency prioritisation                          *
     * ---------------------------------------------------------------- */
    double maxFill = 0.0, maxHours = 0.0;
    for (int i = 0; i < B; ++i) {
        maxFill  = max(maxFill,  bins[i].fill);
        maxHours = max(maxHours, bins[i].hours);
    }
    for (int i = 0; i < B; ++i) {
        double nf = (maxFill  > EPS) ? bins[i].fill  / maxFill  : 0.0;
        double nh = (maxHours > EPS) ? bins[i].hours / maxHours : 0.0;
        bins[i].urgency = 100.0 * (0.6 * nf + 0.4 * nh);
    }

    vector<int> priority(B);
    for (int i = 0; i < B; ++i) priority[i] = i;
    sort(priority.begin(), priority.end(), [&](int a, int b) {
        if (fabs(bins[a].urgency - bins[b].urgency) > EPS)
            return bins[a].urgency > bins[b].urgency;
        return bins[a].fill > bins[b].fill;
    });

    cout << "\n--- 2. GREEDY URGENCY PRIORITISATION ---------------------------\n";
    cout << "  urgency = 100 * (0.60 * normalised_fill + 0.40 * normalised_age)\n\n";
    cout << "  " << left  << setw(6)  << "Rank"
                  << setw(16) << "Bin"
         << right << setw(6)  << "Zone"
                  << setw(10) << "Fill(t)"
                  << setw(8)  << "Age(h)"
                  << setw(10) << "Urgency" << "\n";
    cout << "  " << string(56, '-') << "\n";
    for (int r = 0; r < B; ++r) {
        const Bin& b = bins[priority[r]];
        cout << "  " << left  << setw(6)  << (r + 1)
                      << setw(16) << b.name
             << right << setw(6)  << b.zone
                      << setw(10) << setprecision(2) << b.fill
                      << setw(8)  << setprecision(1) << b.hours
                      << setw(10) << setprecision(1) << b.urgency << "\n";
    }

    /* ---------------------------------------------------------------- *
     *  3.  DP KNAPSACK SELECTION  +  ROUTING, truck by truck           *
     * ---------------------------------------------------------------- */
    cout << "\n--- 3. CAPACITY-AWARE SELECTION & ROUTING (per truck) ----------\n";

    vector<char> collected(B, 0);
    double totalCollected = 0.0;
    int    servedBins     = 0;

    for (int ti = 0; ti < T; ++ti) {
        Truck& tr = trucks[ti];

        cout << "\n  Truck " << tr.name
             << "   (capacity " << setprecision(1) << tr.capacity
             << " t, shift " << setprecision(0) << tr.shift
             << " min, start " << nodeName[tr.start] << ")\n";

        /* ---- candidate set: uncollected, reachable from this truck ---- */
        vector<int> cand;
        for (int r = 0; r < B; ++r) {
            int idx = priority[r];
            if (collected[idx]) continue;
            if (travel(tr.start, bins[idx].node) >= INF / 2) continue;
            cand.push_back(idx);
        }

        if (cand.empty()) {
            cout << "    No candidate bins left.\n";
            continue;
        }

        /* ---- dynamic programming: 0/1 knapsack ---- */
        vector<int> selected = knapsackSelect(bins, cand, tr.capacity);

        /* safety: exact-load guard after 0.1 t quantisation */
        double load = 0.0;
        for (size_t i = 0; i < selected.size(); ++i) load += bins[selected[i]].fill;
        while (load > tr.capacity + EPS && !selected.empty()) {
            int worst = selected[0];
            for (size_t i = 1; i < selected.size(); ++i)
                if (bins[selected[i]].urgency < bins[worst].urgency) worst = selected[i];
            load -= bins[worst].fill;
            selected.erase(remove(selected.begin(), selected.end(), worst),
                           selected.end());
        }

        if (selected.empty()) {
            cout << "    DP found no bin that fits this truck.\n";
            continue;
        }

        /* ---- route + shift-time feasibility ---- */
        vector<int> deferred;
        RoutePlan   plan;
        bool        havePlan = false;

        while (!selected.empty()) {
            vector<int> order = nearestNeighbourOrder(tr.start, selected, bins);
            plan = buildPlan(tr.start, order, bins, transfers);
            if (plan.totalTime <= tr.shift + EPS) { havePlan = true; break; }

            /* too long: drop the least urgent stop and retry */
            int worst = selected[0];
            for (size_t i = 1; i < selected.size(); ++i)
                if (bins[selected[i]].urgency < bins[worst].urgency) worst = selected[i];
            selected.erase(remove(selected.begin(), selected.end(), worst),
                           selected.end());
            deferred.push_back(worst);
        }

        if (!havePlan) {
            cout << "    No shift-feasible route could be built.\n";
            for (size_t i = 0; i < deferred.size(); ++i) collected[deferred[i]] = 0;
            continue;
        }

        /* ---- report the knapsack choice ---- */
        cout << "    DP knapsack picked " << plan.stops.size() << " bin(s)"
             << "  ->  load " << setprecision(2) << plan.load
             << " t / " << setprecision(2) << tr.capacity
             << " t,   urgency value " << setprecision(1) << plan.value << "\n";
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            const Bin& b = bins[plan.stops[i]];
            cout << "        - " << left << setw(14) << b.name
                 << right << setprecision(2) << setw(6) << b.fill << " t"
                 << "   urgency " << setprecision(1) << setw(5) << b.urgency << "\n";
        }

        /* ---- report the route with Dijkstra legs ---- */
        cout << "    Route: ";
        for (size_t i = 0; i < plan.nodes.size(); ++i) {
            if (i) cout << " -> ";
            cout << nodeName[plan.nodes[i]];
        }
        cout << "\n";

        for (size_t i = 0; i + 1 < plan.nodes.size(); ++i) {
            int a = plan.nodes[i], b = plan.nodes[i + 1];
            vector<int> prev;
            vector<double> d = dijkstra(a, &prev);
            vector<int> p = buildPath(a, b, prev);

            cout << "      " << left << setw(14) << nodeName[a]
                 << " -> " << setw(14) << nodeName[b]
                 << right << setprecision(1) << setw(7) << d[b] << " min   [";
            for (size_t k = 0; k < p.size(); ++k) {
                if (k) cout << " -> ";
                cout << nodeName[p[k]];
            }
            cout << "]\n";
        }

        cout << "    Travel " << setprecision(1) << plan.travelTime
             << " min + service " << plan.serviceTime
             << " min = " << plan.totalTime << " min"
             << "  (shift " << setprecision(0) << tr.shift << " min)  "
             << (plan.totalTime <= tr.shift + EPS ? "OK" : "OVER") << "\n";

        if (!deferred.empty()) {
            cout << "    Deferred for lack of time: ";
            for (size_t i = 0; i < deferred.size(); ++i) {
                if (i) cout << ", ";
                cout << bins[deferred[i]].name;
            }
            cout << "\n";
        }

        /* ---- commit ---- */
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            collected[plan.stops[i]] = 1;
            totalCollected += bins[plan.stops[i]].fill;
            ++servedBins;
        }
    }

    /* ---------------------------------------------------------------- *
     *  4.  COLLECTION SUMMARY                                          *
     * ---------------------------------------------------------------- */
    double totalWaste = 0.0;
    for (int i = 0; i < B; ++i) totalWaste += bins[i].fill;

    cout << "\n--- 4. COLLECTION SUMMARY --------------------------------------\n";
    cout << "  Bins served          : " << servedBins << " / " << B << "\n";
    cout << "  Tonnage collected    : " << setprecision(2) << totalCollected
         << " t / " << totalWaste << " t\n";
    cout << "  Fleet capacity used  : " << setprecision(1)
         << (totalWaste > EPS ? 100.0 * totalCollected / totalWaste : 0.0) << " %\n";

    if (servedBins < B) {
        cout << "  Not collected (next cycle) : ";
        bool first = true;
        for (int i = 0; i < B; ++i) {
            if (collected[i]) continue;
            if (!first) cout << ", ";
            first = false;
            cout << bins[i].name << " (" << setprecision(2) << bins[i].fill << " t)";
        }
        cout << "\n";
    }

    /* ---------------------------------------------------------------- *
     *  5.  MAX-FLOW / MIN-CUT  (Edmonds-Karp)                          *
     * ---------------------------------------------------------------- */
    cout << "\n--- 5. MAX-FLOW / MIN-CUT ANALYSIS (Edmonds-Karp) -------------\n";

    double maxflow = edmondsKarp(fg, fs, ft);

    cout << "  Flow network : " << F << " nodes, " << Q << " edges\n";
    cout << "  Source       : " << fname[fs] << "\n";
    cout << "  Sink         : " << fname[ft] << "\n\n";

    cout << "  Edge capacities:\n";
    for (size_t i = 0; i < orig.size(); ++i) {
        cout << "      " << left  << setw(12) << fname[orig[i].u]
             << " -> " << setw(12) << fname[orig[i].v]
             << right << setprecision(2) << setw(7) << orig[i].cap << " t/day\n";
    }

    cout << "\n  Maximum feasible throughput : "
         << setprecision(2) << maxflow << " t/day\n";

    /* ---- min cut: nodes reachable from source in the residual graph ---- */
    vector<char> reach(F, 0);
    {
        queue<int> q;
        q.push(fs); reach[fs] = 1;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (size_t i = 0; i < fg[u].size(); ++i) {
                const FEdge& e = fg[u][i];
                if (!reach[e.to] && e.cap > EPS) {
                    reach[e.to] = 1;
                    q.push(e.to);
                }
            }
        }
    }

    cout << "\n  Minimum cut (bottleneck edges):\n";
    double cutCap = 0.0;
    for (size_t i = 0; i < orig.size(); ++i) {
        if (reach[orig[i].u] && !reach[orig[i].v]) {
            cout << "      " << left  << setw(12) << fname[orig[i].u]
                 << " -> " << setw(12) << fname[orig[i].v]
                 << right << setprecision(2) << setw(7) << orig[i].cap
                 << " t/day   <-- SATURATED\n";
            cutCap += orig[i].cap;
        }
    }
    if (cutCap < EPS) cout << "      (none - the network is not saturating)\n";

    cout << "\n  The min-cut value equals the max-flow value ("
         << setprecision(2) << cutCap << " t/day): these edges are the\n"
         << "  infrastructure bottleneck that most constrains sustainable\n"
         << "  throughput and would govern any capacity-expansion decision.\n";

    cout << "\n================================================================\n";
    cout << "  Dijkstra chooses efficient paths.  Greedy prioritises urgent\n";
    cout << "  bins.  Dynamic programming respects truck capacity.  Max-flow\n";
    cout << "  exposes infrastructure bottlenecks.  One coordinated framework.\n";
    cout << "================================================================\n";

    return 0;
}
