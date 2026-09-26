/* ============================================================================
   SMART WASTE COLLECTION SYSTEM FOR DHAKA CITY
   Sensor-driven route optimisation and capacity-aware scheduling
   ----------------------------------------------------------------------------
   Four complementary algorithmic tools working as one city-scale concept:
     1. Dijkstra          -> shortest-path routing on the live road graph
     2. Greedy            -> urgency-based prioritisation of bins
     3. Dynamic Prog.     -> capacity-aware 0/1 knapsack selection per truck
     4. Edmonds-Karp      -> max-flow / min-cut on the disposal network

   Capacity enforcement:
     - trucks respect load AND shift window (feasibility pruning by detour cost)
     - transfer stations have a daily intake that is actually consumed
     - landfills enter the system flow network, exposed as a throughput ceiling
   ============================================================================ */

#include <bits/stdc++.h>
using namespace std;

static const double INF         = 1e18;
static const double EPS         = 1e-9;
static const double SERVICE_MIN = 6.0;

/* ==========================================================================
   1.  ROAD GRAPH  +  DIJKSTRA'S ALGORITHM
   ========================================================================== */

int                              V;
vector<string>                   nodeName;
struct RoadEdge { int to; double w; };
vector<vector<RoadEdge> >        roadAdj;

void addRoad(int u, int v, double w) {
    RoadEdge a; a.to = v; a.w = w;  roadAdj[u].push_back(a);
    RoadEdge b; b.to = u; b.w = w;  roadAdj[v].push_back(b);
}

vector<double> dijkstra(int src, vector<int>* prevOut = nullptr) {
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
        if (d > dist[u] + EPS) continue;                 // stale entry

        for (size_t i = 0; i < roadAdj[u].size(); ++i) {
            const RoadEdge& e = roadAdj[u][i];
            double nd = d + e.w;
            if (nd < dist[e.to] - EPS) {                 // relaxation
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
    if (path.empty() || path.back() != src) return vector<int>();
    reverse(path.begin(), path.end());
    return path;
}

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
    int    node;
    double fill;      // tonnes
    double hours;     // hours since last collection
    int    zone;
    double urgency;   // set by the greedy stage
};

struct Truck {
    string name;
    double capacity;  // tonnes
    double shift;     // minutes
    int    start;     // node
};

struct Facility {
    int    node;
    double capacity;                                    // tonnes/day
    double used;                                        // consumed so far
    Facility() : node(0), capacity(0.0), used(0.0) {}
    double remaining() const { return capacity - used; }
};

struct RoutePlan {
    vector<int> stops;        // bin indices in visit order
    vector<int> nodes;        // full node sequence: start ... depot
    double travelTime;
    double serviceTime;
    double totalTime;
    double load;
    double value;
    int    tsIndex;           // chosen transfer station index, -1 if none
    bool   tsOverflow;
    RoutePlan() : travelTime(0), serviceTime(0), totalTime(0),
                  load(0), value(0), tsIndex(-1), tsOverflow(false) {}
};

/* ==========================================================================
   3.  DYNAMIC PROGRAMMING  --  0/1 KNAPSACK
   --------------------------------------------------------------------------
   weight = bin fill   (quantised to 0.1 t units)
   value  = urgency    (quantised to 0.1 units)
   capacity = truck load limit
   ========================================================================== */

vector<int> knapsackSelect(const vector<Bin>& bins,
                           const vector<int>& cand,
                           double capTonnes)
{
    int W = (int)llround(capTonnes * 10.0);
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
            dp[i][w] = dp[i - 1][w];
            if (wt[i - 1] <= w)
                dp[i][w] = max(dp[i][w],
                               dp[i - 1][w - wt[i - 1]] + val[i - 1]);
        }
    }

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
   ROUTE CONSTRUCTION  (nearest-neighbour over Dijkstra distances)
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
                    const vector<Facility>& transfers,
                    int depot)
{
    RoutePlan p;
    p.stops = order;
    p.nodes.push_back(startNode);

    int cur = startNode;
    for (size_t i = 0; i < order.size(); ++i) {
        int b = order[i];
        p.travelTime  += travel(cur, bins[b].node);
        p.nodes.push_back(bins[b].node);
        cur            = bins[b].node;
        p.load        += bins[b].fill;
        p.value       += bins[b].urgency;
        p.serviceTime += SERVICE_MIN;
    }

    /* Require one transfer station to accept the COMPLETE truck load.
       Partial dumping would violate the stated daily intake constraint. */
    int    bestTS = -1;
    double bd     = INF;
    for (size_t i = 0; i < transfers.size(); ++i) {
        if (transfers[i].remaining() + EPS < p.load) continue;
        double d = travel(cur, transfers[i].node);
        if (d >= INF / 2) continue;
        if (d < bd) { bd = d; bestTS = (int)i; }
    }

    /* No legal disposal station means this route is infeasible. */
    if (bestTS < 0) {
        p.totalTime = INF;
        return p;
    }

    p.travelTime += bd;
    p.nodes.push_back(transfers[bestTS].node);
    p.tsIndex = bestTS;
    p.tsOverflow = false;
    cur = transfers[bestTS].node;

    /* return to depot */
    if (cur != depot) {
        double back = travel(cur, depot);
        if (back >= INF / 2) {
            p.totalTime = INF;
            return p;
        }
        p.travelTime += back;
        p.nodes.push_back(depot);
    }

    p.totalTime = p.travelTime + p.serviceTime;
    return p;
}

/* ==========================================================================
   4.  MAX-FLOW  --  EDMONDS-KARP
   ========================================================================== */

struct FEdge { int to, rev; double cap; };

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
        if (!vis[t]) break;

        double push = INF;
        for (int v = t; v != s; v = pv[v])
            push = min(push, g[pv[v]][pe[v]].cap);

        for (int v = t; v != s; v = pv[v]) {
            FEdge& e = g[pv[v]][pe[v]];
            e.cap           -= push;
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

    auto die = [](const string& msg) {
        cerr << "Input error: " << msg << "\n";
        exit(1);
    };

    /* ---------------------------------------------------------------- *
     *  PARSE INPUT                                                     *
     * ---------------------------------------------------------------- */
    if (!(cin >> V)) die("empty input");
    int E; if (!(cin >> E)) die("missing E");
    if (V <= 0 || V > 100000) die("V out of range");
    if (E < 0  || E > 500000) die("E out of range");

    nodeName.resize(V);
    for (int i = 0; i < V; ++i) if (!(cin >> nodeName[i])) die("short read on names");

    roadAdj.assign(V, vector<RoadEdge>());
    for (int i = 0; i < E; ++i) {
        int u, v; double w;
        if (!(cin >> u >> v >> w)) die("short read on edge " + to_string(i));
        if (u < 0 || u >= V || v < 0 || v >= V) die("edge endpoint out of range");
        if (w < 0) die("negative edge weight");
        addRoad(u, v, w);
    }

    int depot; if (!(cin >> depot)) die("missing depot");
    if (depot < 0 || depot >= V) die("depot out of range");

    int K; if (!(cin >> K)) die("missing K");
    if (K < 0 || K > 100000) die("K out of range");
    vector<Facility> transfers(K);
    for (int i = 0; i < K; ++i) {
        if (!(cin >> transfers[i].node >> transfers[i].capacity))
            die("short read on transfer " + to_string(i));
        if (transfers[i].node < 0 || transfers[i].node >= V)
            die("transfer node out of range");
        if (transfers[i].capacity < 0) die("negative transfer capacity");
    }

    int M; if (!(cin >> M)) die("missing M");
    if (M < 0 || M > 100000) die("M out of range");
    vector<Facility> landfills(M);
    for (int i = 0; i < M; ++i) {
        if (!(cin >> landfills[i].node >> landfills[i].capacity))
            die("short read on landfill " + to_string(i));
        if (landfills[i].node < 0 || landfills[i].node >= V)
            die("landfill node out of range");
        if (landfills[i].capacity < 0) die("negative landfill capacity");
    }

    int T; if (!(cin >> T)) die("missing T");
    if (T < 0 || T > 10000) die("T out of range");
    vector<Truck> trucks(T);
    for (int i = 0; i < T; ++i) {
        if (!(cin >> trucks[i].name >> trucks[i].capacity
                  >> trucks[i].shift  >> trucks[i].start))
            die("short read on truck " + to_string(i));
        if (trucks[i].start < 0 || trucks[i].start >= V)
            die("truck start node out of range");
        if (trucks[i].capacity <= 0) die("truck capacity <= 0");
        if (trucks[i].shift    <= 0) die("truck shift <= 0");
    }

    int B; if (!(cin >> B)) die("missing B");
    if (B < 0 || B > 1000000) die("B out of range");
    vector<Bin> bins(B);
    for (int i = 0; i < B; ++i) {
        if (!(cin >> bins[i].id >> bins[i].name >> bins[i].node
                  >> bins[i].fill >> bins[i].hours >> bins[i].zone))
            die("short read on bin " + to_string(i));
        if (bins[i].node  < 0 || bins[i].node >= V) die("bin node out of range");
        if (bins[i].fill  < 0) die("bin fill < 0");
        if (bins[i].hours < 0) die("bin hours < 0");
    }

    /* optional user-supplied flow network */
    int F = 0, Q = 0;
    cin >> F >> Q;
    if (!cin) { F = 0; Q = 0; cin.clear(); }

    vector<string>            fname;
    vector<vector<FEdge> >    fg;
    vector<pair<int,int> >    origEdges;
    vector<double>            origCaps;
    int fs = 0, ft = 0;

    if (F > 0) {
        fname.resize(F);
        for (int i = 0; i < F; ++i) if (!(cin >> fname[i])) die("short read on flow names");
        if (!(cin >> fs >> ft)) die("short read on source/sink");
        if (fs < 0 || fs >= F || ft < 0 || ft >= F) die("source/sink out of range");
        fg.assign(F, vector<FEdge>());
        for (int i = 0; i < Q; ++i) {
            int u, v; double c;
            if (!(cin >> u >> v >> c)) die("short read on flow edge");
            if (u < 0 || u >= F || v < 0 || v >= F) die("flow edge endpoint out of range");
            addFEdge(fg, u, v, c);
            origEdges.push_back(make_pair(u, v));
            origCaps.push_back(c);
        }
    }

    cout << fixed;
    cout << "================================================================\n";
    cout << "  SMART WASTE COLLECTION SYSTEM  -  DHAKA CITY\n";
    cout << "  Sensor-driven route optimisation & capacity-aware scheduling\n";
    cout << "================================================================\n";

    /* ---------------------------------------------------------------- *
     *  1. ROAD NETWORK                                                 *
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

    {
        vector<double> d = dijkstra(depot);
        int unreachable = 0;
        for (int i = 0; i < V; ++i) if (d[i] >= INF / 2) ++unreachable;
        cout << "  Unreachable nodes from depot : " << unreachable << "\n";
    }

    /* ---------------------------------------------------------------- *
     *  2. GREEDY URGENCY PRIORITISATION                                *
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
     *  3. DP KNAPSACK SELECTION + ROUTING, TRUCK BY TRUCK              *
     * ---------------------------------------------------------------- */
    cout << "\n--- 3. CAPACITY-AWARE SELECTION & ROUTING (per truck) ----------\n";

    vector<char> collected(B, 0);
    double       totalCollected = 0.0;
    int          servedBins     = 0;

    for (int ti = 0; ti < T; ++ti) {
        Truck& tr = trucks[ti];

        cout << "\n  Truck " << tr.name
             << "   (capacity " << setprecision(1) << tr.capacity
             << " t, shift "    << setprecision(0) << tr.shift
             << " min, start "  << nodeName[tr.start] << ")\n";

        /* candidate set: uncollected and reachable from this truck */
        vector<int> cand;
        for (int r = 0; r < B; ++r) {
            int idx = priority[r];
            if (collected[idx]) continue;
            if (travel(tr.start, bins[idx].node) >= INF / 2) continue;
            cand.push_back(idx);
        }
        if (cand.empty()) { cout << "    No candidate bins left.\n"; continue; }

        /* DP knapsack */
        vector<int> selected = knapsackSelect(bins, cand, tr.capacity);

        /* post-DP safety: exact-load guard after 0.1 t quantisation */
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

        /* route + shift-feasibility pruning (detour / urgency ratio) */
        vector<int> deferred;
        RoutePlan   plan;
        bool        havePlan = false;

        while (!selected.empty()) {
            vector<int> order = nearestNeighbourOrder(tr.start, selected, bins);
            plan = buildPlan(tr.start, order, bins, transfers, depot);
            if (plan.totalTime <= tr.shift + EPS) { havePlan = true; break; }

            const vector<int>& routeOrder = plan.stops;
            int    n          = (int)routeOrder.size();
            int    worst      = -1;
            double worstRatio = -1.0;

            for (int i = 0; i < n; ++i) {
                int prevNode = (i == 0)
                             ? tr.start
                             : bins[routeOrder[i - 1]].node;
                int nextNode;
                if (i + 1 < n)
                    nextNode = bins[routeOrder[i + 1]].node;
                else if (plan.tsIndex >= 0)
                    nextNode = transfers[plan.tsIndex].node;
                else
                    nextNode = depot;

                int here = bins[routeOrder[i]].node;

                double detour = travel(prevNode, here)
                              + travel(here, nextNode)
                              - travel(prevNode, nextNode);
                if (detour < 0) detour = 0;

                double ratio = detour / (bins[routeOrder[i]].urgency + EPS);
                if (ratio > worstRatio) {
                    worstRatio = ratio;
                    worst = i;
                }
            }

            if (worst < 0) { selected.pop_back(); continue; }
            deferred.push_back(routeOrder[worst]);
            selected.erase(remove(selected.begin(), selected.end(), routeOrder[worst]),
                           selected.end());
        }

        if (!havePlan) {
            cout << "    No shift-feasible route could be built.\n";
            continue;
        }

        /* report the knapsack pick */
        cout << "    DP knapsack picked " << plan.stops.size() << " bin(s)"
             << "  ->  load " << setprecision(2) << plan.load
             << " t / "       << setprecision(2) << tr.capacity
             << " t,   urgency value " << setprecision(1) << plan.value << "\n";
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            const Bin& b = bins[plan.stops[i]];
            cout << "        - " << left << setw(14) << b.name
                 << right << setprecision(2) << setw(6) << b.fill << " t"
                 << "   urgency " << setprecision(1) << setw(5) << b.urgency << "\n";
        }

        /* route with Dijkstra legs */
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
            vector<int>    p = buildPath(a, b, prev);
            cout << "      " << left  << setw(14) << nodeName[a]
                 << " -> "       << setw(14) << nodeName[b]
                 << right << setprecision(1) << setw(7) << d[b] << " min   [";
            for (size_t k = 0; k < p.size(); ++k) {
                if (k) cout << " -> ";
                cout << nodeName[p[k]];
            }
            cout << "]\n";
        }

        cout << "    Travel "  << setprecision(1) << plan.travelTime
             << " min + service " << plan.serviceTime
             << " min = "     << plan.totalTime << " min"
             << "  (shift "   << setprecision(0) << tr.shift << " min)  "
             << (plan.totalTime <= tr.shift + EPS ? "OK" : "OVER") << "\n";

        if (!deferred.empty()) {
            cout << "    Deferred for lack of time: ";
            for (size_t i = 0; i < deferred.size(); ++i) {
                if (i) cout << ", ";
                cout << bins[deferred[i]].name;
            }
            cout << "\n";
        }

        /* commit: mark bins collected and consume transfer-station capacity */
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            collected[plan.stops[i]] = 1;
            totalCollected += bins[plan.stops[i]].fill;
            ++servedBins;
        }
        if (plan.tsIndex >= 0) {
            transfers[plan.tsIndex].used += plan.load;
            cout << "    Tipped " << setprecision(2) << plan.load
                 << " t at "   << nodeName[transfers[plan.tsIndex].node]
                 << "   (station now at "  << setprecision(2)
                 << transfers[plan.tsIndex].used << " / "
                 << setprecision(2) << transfers[plan.tsIndex].capacity
                 << " t/day)\n";
        }
    }

    /* ---------------------------------------------------------------- *
     *  4. COLLECTION SUMMARY                                           *
     * ---------------------------------------------------------------- */
    double totalWaste = 0.0;
    for (int i = 0; i < B; ++i) totalWaste += bins[i].fill;

    cout << "\n--- 4. COLLECTION SUMMARY --------------------------------------\n";
    cout << "  Bins served          : " << servedBins << " / " << B << "\n";
    cout << "  Tonnage collected    : " << setprecision(2) << totalCollected
         << " t / " << totalWaste << " t\n";

    double totalFleetCap = 0.0;
    for (int i = 0; i < T; ++i)
        totalFleetCap += trucks[i].capacity;

    cout << "  Collection coverage  : " << setprecision(1)
         << (totalWaste    > EPS ? 100.0 * totalCollected / totalWaste    : 0.0)
         << " %\n";
    cout << "  Fleet payload usage  : " << setprecision(1)
         << (totalFleetCap > EPS ? 100.0 * totalCollected / totalFleetCap : 0.0)
         << " %   (collected / aggregate truck payload)\n";

    double totalTransferCap = 0.0, totalTransferUsed = 0.0;
    for (int i = 0; i < K; ++i) {
        totalTransferCap  += transfers[i].capacity;
        totalTransferUsed += transfers[i].used;
    }
    double totalLandfillCap = 0.0;
    for (int i = 0; i < M; ++i) totalLandfillCap += landfills[i].capacity;

    cout << "  Transfer capacity    : " << setprecision(2) << totalTransferCap
         << " t/day   used "        << setprecision(2) << totalTransferUsed
         << " t/day\n";
    cout << "  Landfill capacity    : " << setprecision(2) << totalLandfillCap
         << " t/day\n";

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
     *  5a. SYSTEM THROUGHPUT (derived from the actual collection)      *
     * ---------------------------------------------------------------- */
    cout << "\n--- 5a. SYSTEM THROUGHPUT (built from collection result) -------\n";
    if (K + M == 0) {
        cout << "  No transfer stations or landfills defined.\n";
    } else {
        int FS = 0;
        int FT = K + M + 1;
        vector<vector<FEdge> > sys(K + M + 2);

        for (int i = 0; i < K; ++i)
            addFEdge(sys, FS, 1 + i, transfers[i].capacity);

        for (int i = 0; i < K; ++i)
            for (int j = 0; j < M; ++j)
                if (travel(transfers[i].node, landfills[j].node) < INF / 2)
                    addFEdge(sys, 1 + i, 1 + K + j, INF);

        for (int j = 0; j < M; ++j)
            addFEdge(sys, 1 + K + j, FT, landfills[j].capacity);

        double sysFlow  = edmondsKarp(sys, FS, FT);
        double headroom = sysFlow - totalCollected;

        cout << "  Stations modelled    : " << K << "\n";
        cout << "  Landfills modelled   : " << M << "\n";
        cout << "  Total collected      : " << setprecision(2)
             << totalCollected << " t/day\n";
        cout << "  System max-flow      : " << setprecision(2)
             << sysFlow << " t/day\n";
        cout << "  Infrastructure headroom : " << setprecision(2)
             << headroom << " t/day\n";

        if (headroom < -EPS)
            cout << "  >>> BOTTLENECK: " << setprecision(2) << (-headroom)
                 << " t/day cannot be disposed with current\n"
                 << "      transfer / landfill capacity.\n";
        else
            cout << "  Collection fits within disposal infrastructure.\n";
    }

    /* ---------------------------------------------------------------- *
     *  5b. USER-SUPPLIED FLOW NETWORK (optional)                       *
     * ---------------------------------------------------------------- */
    if (F > 0) {
        cout << "\n--- 5b. USER FLOW NETWORK (Edmonds-Karp) -----------------------\n";
        double maxflow = edmondsKarp(fg, fs, ft);

        cout << "  Flow network : " << F << " nodes, " << Q << " edges\n";
        cout << "  Source       : " << fname[fs] << "\n";
        cout << "  Sink         : " << fname[ft] << "\n\n";

        cout << "  Edge capacities:\n";
        for (size_t i = 0; i < origEdges.size(); ++i) {
            cout << "      " << left  << setw(12) << fname[origEdges[i].first]
                 << " -> "       << setw(12) << fname[origEdges[i].second]
                 << right << setprecision(2) << setw(7) << origCaps[i]
                 << " t/day\n";
        }

        cout << "\n  Maximum feasible throughput : "
             << setprecision(2) << maxflow << " t/day\n";

        /* residual reachability -> min-cut edges */
        vector<char> reach(F, 0);
        {
            queue<int> q; q.push(fs); reach[fs] = 1;
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
        for (size_t i = 0; i < origEdges.size(); ++i) {
            if (reach[origEdges[i].first] && !reach[origEdges[i].second]) {
                cout << "      " << left  << setw(12) << fname[origEdges[i].first]
                     << " -> "       << setw(12) << fname[origEdges[i].second]
                     << right << setprecision(2) << setw(7) << origCaps[i]
                     << " t/day   <-- SATURATED\n";
                cutCap += origCaps[i];
            }
        }
        if (cutCap < EPS) cout << "      (none - the network is not saturating)\n";

        cout << "\n  Maximum flow : " << setprecision(2) << maxflow << " t/day\n";
        cout << "  Minimum cut  : " << setprecision(2) << cutCap  << " t/day\n";
        if (fabs(maxflow - cutCap) < 1e-6)
            cout << "  Verified: max-flow = min-cut (max-flow/min-cut theorem holds).\n";
        else
            cout << "  *** WARNING: mismatch, max-flow != min-cut. Bug. ***\n";
    }

    cout << "\n================================================================\n";
    cout << "  Dijkstra chooses efficient paths.  Greedy prioritises urgent\n";
    cout << "  bins.  Dynamic programming respects truck capacity.  Max-flow\n";
    cout << "  exposes infrastructure bottlenecks.  One coordinated framework.\n";
    cout << "================================================================\n";

    return 0;
}
