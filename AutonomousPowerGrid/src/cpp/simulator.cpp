#include <bits/stdc++.h>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <iomanip>
#include <fstream>
#include <sstream>
#include "raylib.h"
#include "raymath.h"
#include "rl_bridge.h"   // ← RL Bridge
#include <filesystem>
#include <chrono>

std::chrono::steady_clock::time_point training_start_time;
bool training_started = false;
namespace fs = std::filesystem;
using namespace std;

static random_device rd;
static mt19937 g(rd());

// ── Global simulation clock (mutated by RLBridgeServer::execute_step) ────
int sim_hour        = 0;
int sim_day_of_week = 0;
int sim_date        = 1;
int sim_month       = 1;

struct PairHash
{
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &p) const
    {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

class graph{
public:
    class Vertex_A
    {
    public:
        int id;
        string node_type = "";
        float max_limit;
        float current_demand;  
        float per=1;
        int x, y;

        Vertex_A(string type, int id) : id(id),node_type(type), max_limit(0),
                                current_demand(0),
                                x(0), y(0) {}
    };

    class Edge
    {
    public:
        float loss;
        float max_load;
        float control ;   // 1.0 = full strength, 0.0 = off
    
        // Signed load:
        // +ve → Node_1 → Node_2
        // -ve → Node_2 → Node_1
        float current_load;
        bool is_active;
        Vertex_A *Node_1;
        Vertex_A *Node_2;
    
        Edge(Vertex_A *Node_1, Vertex_A *Node_2)
            : loss(0), max_load(0), current_load(0),
              Node_1(Node_1), Node_2(Node_2),is_active(true),control(1.0)
        {
            // FIX: loss here is a rough initial value only.
            // assign_edge_capacities() overrides loss with distance-based reactance
            // for graph-built edges. This constructor value is used only for
            // dynamically added edges (e.g. add_realistic_connections).
            if (Node_1->node_type == "power_plant" && Node_2->node_type == "substation")
                loss = 2 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (5 - 2)));
    
            else if (Node_1->node_type == "substation" && Node_2->node_type == "substation")
                loss = 1 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (3 - 1)));

            else
                loss = 1.5f; // default for plant-plant or any unmatched type
        }
    
        // 🔹 Get absolute load (useful for overload checks)
        float get_magnitude() const
        {
            return fabs(current_load);
        }
    
        // 🔹 Get direction explicitly
        Vertex_A* from() const
        {
            return (current_load >= 0) ? Node_1 : Node_2;
        }
    
        Vertex_A* to() const
        {
            return (current_load >= 0) ? Node_2 : Node_1;
        }
    
        // 🔹 Reset flow (important each timestep)
        void reset_flow()
        {
            current_load = 0.0f;
        }
        
        void disconnect() {is_active =false ; current_load=0;}
        void connect() {is_active =true ;}
        
    };

    unordered_map<Vertex_A *, vector<pair<Vertex_A *, Edge *>>> adj_power_substation;
    unordered_map<Vertex_A *, vector<pair<Vertex_A *, Edge *>>> adj_reverse_power;
    unordered_set<Edge *> overloaded_edges;
    Edge *edge_being_fixed = nullptr;
    unordered_map<Vertex_A *, float> nodes_to_throttle;
    unordered_set<Vertex_A *> node_overloads_visual;
    unordered_set<Vertex_A *> throttled_nodes_visual;
    vector<pair<Vertex_A *, Edge *>> fix_path;
    Vertex_A *last_event_substation = nullptr;
    Color last_event_substation_color = {0, 0, 0, 0};

    // Fixed max-limit table (100 entries for up to 100 substations)
    vector<float> max_limits = {
        118.0f, 51.0f, 33.0f, 108.0f, 21.0f, 118.0f, 26.0f, 35.0f, 72.0f, 13.0f,
          9.0f, 56.0f, 22.0f,  70.0f,  8.0f,  10.0f,115.0f, 80.0f, 27.0f, 15.0f,
         71.0f,114.0f, 91.0f,  71.0f, 10.0f,  82.0f, 90.0f, 25.0f, 36.0f, 50.0f,
         45.0f, 35.0f, 73.0f, 101.0f,120.0f,  25.0f,100.0f, 60.0f, 17.0f, 29.0f,
         67.0f, 23.0f, 50.0f,  55.0f, 54.0f,  91.0f, 50.0f, 46.0f, 15.0f, 28.0f,
        118.0f, 51.0f, 33.0f, 108.0f, 21.0f, 118.0f, 26.0f, 35.0f, 72.0f, 13.0f,
          9.0f, 56.0f, 22.0f,  70.0f,  8.0f,  10.0f,115.0f, 80.0f, 27.0f, 15.0f,
         71.0f,114.0f, 91.0f,  71.0f, 10.0f,  82.0f, 90.0f, 25.0f, 36.0f, 50.0f,
         45.0f, 35.0f, 73.0f, 101.0f,120.0f,  25.0f,100.0f, 60.0f, 17.0f, 29.0f,
         67.0f, 23.0f, 50.0f,  55.0f, 54.0f,  91.0f, 50.0f, 46.0f, 15.0f, 28.0f
    };

    // ── CSV / demand helpers ────────────────────────────────────────────
    //  Full-year CSV loader: returns demand_schedule[row][sub_idx]
    vector<vector<float>> load_full_year_csv(const string& filename) {
        vector<vector<float>> schedule;
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[CSV] Cannot open '" << filename << "'\n";
            return schedule;
        }
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            vector<float> row;
            stringstream ss(line);
            string token;
            getline(ss, token, ','); // skip datetime
            while (getline(ss, token, ',')) {
                try { row.push_back(stof(token)); }
                catch (...) { row.push_back(0.f); }
            }
            schedule.push_back(row);
        }
        cout << "[CSV] Loaded " << schedule.size() << " rows × "
             << (schedule.empty() ? 0 : schedule[0].size()) << " columns\n";
        return schedule;
    }

    //  Old single-row reader kept for backward compatibility
    vector<float> read_demand_csv(const string &filename) {
        auto schedule = load_full_year_csv(filename);
        return schedule.empty() ? vector<float>() : schedule[0];
    }

    void mapping(int powerplant_count, int substation_count)
    {
        int next_id = 0;

        vector<Vertex_A *> powerplants;
        for (int i = 0; i < powerplant_count; i++)
            powerplants.push_back(new Vertex_A("power_plant",next_id++));

        vector<Vertex_A *> substations;
        for (int i = 0; i < substation_count; i++) {
            Vertex_A *s = new Vertex_A("substation",next_id++);
            substations.push_back(s);
        }
        map_powerplants_to_substations(powerplants, substations);
    }

    void map_powerplants_to_substations(vector<Vertex_A *> &powerplants,
                                        vector<Vertex_A *> &substations)
    {
        int num_powerplants = (int)powerplants.size();
        int num_substations = (int)substations.size();
        int substations_for_powerplants = max(1, (int)(0.4 * num_substations));

        vector<int> substation_indices(num_substations);
        iota(substation_indices.begin(), substation_indices.end(), 0);
        shuffle(substation_indices.begin(), substation_indices.end(), g);
        int idx = 0;

        for (int i = 0; i < substations_for_powerplants && idx < num_substations; i++, idx++) {
            Vertex_A *plant = powerplants[i % num_powerplants];
            Vertex_A *sub   = substations[substation_indices[idx]];
            Edge *edge      = new Edge(plant, sub);
            adj_power_substation[plant].push_back({sub, edge});
            adj_reverse_power[sub].push_back({plant, edge});
        }

        while (idx < num_substations) {
            if (idx == 0) break;
            int src_idx = rand() % idx;
            Vertex_A *src  = substations[substation_indices[src_idx]];
            Vertex_A *dest = substations[substation_indices[idx]];
            Edge *edge     = new Edge(src, dest);
            adj_power_substation[src].push_back({dest, edge});
            adj_reverse_power[dest].push_back({src, edge});
            idx++;
        }
        assign_node_capacities();
        assign_edge_capacities();
    }
    void disconnect_edge(Vertex_A* u, Vertex_A* v)
    {
        if (!adj_power_substation.count(u)) return;

        for (auto &[nbr, edge] : adj_power_substation[u])
        {
            if (nbr == v)
            {
                edge->disconnect();
                return;
            }
        }
    }
    void reconnect_edge(Vertex_A* u, Vertex_A* v)
    {
        if (!adj_power_substation.count(u)) return;

        for (auto &[nbr, edge] : adj_power_substation[u])
        {
            if (nbr == v)
            {
                edge->connect();
                return;
            }
        }
    }
    vector<Edge*> get_edges(Vertex_A* node)
    {
        vector<Edge*> res;
        if (!adj_power_substation.count(node)) return res;

        for (auto &[nbr, edge] : adj_power_substation[node])
            res.push_back(edge);

        return res;
    }

    // ══════════════════════════════════════════════════════════════════
    //  recompute_flows  —  DC power-flow (B-θ formulation)
    //
    //  FIXES applied:
    //   1. All nodes (sources AND pure-destination) collected properly.
    //   2. Singular pivot row: zero the row rather than skip-and-corrupt.
    //   3. Plant generation: proportional share clamped to plant max_limit,
    //      with explicit residual correction on the largest plant.
    //   4. Active-edge guard before susceptance assembly.
    //   5. Float→double for entire solve; results written back as float.
    // ══════════════════════════════════════════════════════════════════
    void recompute_flows()
    {
        // ── Step 0: collect ALL unique nodes ──────────────────────────
        vector<Vertex_A*> nodes;
        unordered_map<Vertex_A*, int> id;

        auto ensure = [&](Vertex_A* n) {
            if (!id.count(n)) {
                id[n] = (int)nodes.size();
                nodes.push_back(n);
            }
        };

        for (auto& [node, nbrs] : adj_power_substation) {
            ensure(node);
            for (auto& [nbr, _] : nbrs)
                ensure(nbr);
        }
        // Also pick up nodes that only appear as reverse entries
        for (auto& [node, nbrs] : adj_reverse_power) {
            ensure(node);
            for (auto& [nbr, _] : nbrs)
                ensure(nbr);
        }

        int N = (int)nodes.size();
        if (N == 0) return;

        // ── Step 1: Build net-injection map ───────────────────────────
        unordered_map<Vertex_A*, double> Pmap;
        vector<Vertex_A*> plants;
        double total_demand = 0.0;

        for (auto* n : nodes) {
            if (n->node_type == "substation") {
                double demand = (double)n->current_demand * (double)n->per;
                demand = max(0.0, demand);          // no negative demand
                Pmap[n] = -demand;
                total_demand += demand;
                // cout<<n->per<<endl;
            } else if (n->node_type == "power_plant") {
                plants.push_back(n);
                Pmap[n] = 0.0;                      // initialised below
            }
        }

        // ── Step 2: Proportional generation dispatch ──────────────────
        double total_cap = 0.0;
        for (auto* p : plants) total_cap += (double)p->max_limit;

        if (total_cap > 1e-9) {
            if (total_demand >= total_cap) {
                // Infeasible — saturate every plant
                for (auto* p : plants)
                    Pmap[p] = (double)p->max_limit;
            } else {
                // Proportional share
                for (auto* p : plants) {
                    double share = (double)p->max_limit / total_cap;
                    Pmap[p] = share * total_demand;
                }
                // Correct numerical mismatch on the largest plant
                double gen_sum = 0.0;
                for (auto* p : plants) gen_sum += Pmap[p];
                double mismatch = total_demand - gen_sum;
                if (fabs(mismatch) > 1e-9) {
                    Vertex_A* biggest = *max_element(plants.begin(), plants.end(),
                        [](Vertex_A* a, Vertex_A* b){ return a->max_limit < b->max_limit; });
                    Pmap[biggest] = min(Pmap[biggest] + mismatch,
                                        (double)biggest->max_limit);
                }
            }
        }

        // ── Step 3: P vector ──────────────────────────────────────────
        vector<double> P(N, 0.0);
        for (int i = 0; i < N; i++)
            P[i] = Pmap.count(nodes[i]) ? Pmap[nodes[i]] : 0.0;

        // ── Step 4: Build B matrix (only active edges) ────────────────
        vector<vector<double>> B(N, vector<double>(N, 0.0));
        unordered_set<Edge*> seen;

        for (auto& [u, nbrs] : adj_power_substation) {
            for (auto& [v, e] : nbrs) {
                if (!e->is_active)   continue;
                if (seen.count(e))   continue;
                seen.insert(e);

                int i = id[e->Node_1];
                int j = id[e->Node_2];
                double x = max(0.01, (double)e->loss);   // reactance ≥ 0.01
                double b = 1.0f/ x;                       // susceptance

                B[i][i] += b;  B[j][j] += b;
                B[i][j] -= b;  B[j][i] -= b;
            }
        }

        // ── Step 5: Choose slack bus (first power plant found) ────────
        int slack = -1;
        for (int i = 0; i < N; i++) {
            if (nodes[i]->node_type == "power_plant") { slack = i; break; }
        }
        if (slack == -1) slack = 0;

        // ── Step 6: Reduce system (remove slack row/col) ──────────────
        int M = N - 1;
        vector<vector<double>> A(M, vector<double>(M, 0.0));
        vector<double> rhs(M, 0.0);

        for (int i = 0, r = 0; i < N; i++) {
            if (i == slack) continue;
            rhs[r] = P[i];
            for (int j = 0, c = 0; j < N; j++) {
                if (j == slack) continue;
                A[r][c] = B[i][j];
                c++;
            }
            r++;
        }

        // ── Step 7: Gaussian elimination with partial pivoting ─────────
        //  FIX: when pivot is singular (isolated island node),
        //       zero the row cleanly so downstream rows are not corrupted.
        int n = M;
        for (int i = 0; i < n; i++) {
            // Partial pivot
            int pivot = i;
            for (int j = i + 1; j < n; j++)
                if (fabs(A[j][i]) > fabs(A[pivot][i])) pivot = j;
            swap(A[i], A[pivot]);
            swap(rhs[i], rhs[pivot]);

            double div = A[i][i];
            if (fabs(div) < 1e-10) {
                // Singular row → isolated node; leave theta[i] = 0
                for (int j = i; j < n; j++) A[i][j] = 0.0;
                rhs[i] = 0.0;
                continue;
            }

            // Normalise pivot row
            double inv = 1.0 / div;
            for (int j = i; j < n; j++) A[i][j] *= inv;
            rhs[i] *= inv;

            // Eliminate column i from all other rows
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                double factor = A[j][i];
                if (fabs(factor) < 1e-15) continue;
                for (int k = i; k < n; k++) A[j][k] -= factor * A[i][k];
                rhs[j] -= factor * rhs[i];
            }
        }

        // ── Step 8: Reconstruct theta (slack bus θ = 0) ───────────────
        vector<double> theta(N, 0.0);
        for (int i = 0, r = 0; i < N; i++) {
            if (i == slack) continue;
            theta[i] = rhs[r++];
        }

        // ── Step 9: Reset all flows ───────────────────────────────────
        for (auto& [_, nbrs] : adj_power_substation)
            for (auto& [__, e] : nbrs)
                e->reset_flow();

        // ── Step 10: Compute flows  F_ij = (θ_i − θ_j) / x_ij ───────
        seen.clear();
        for (auto& [u, nbrs] : adj_power_substation) {
            for (auto& [v, e] : nbrs) {
                if (!e->is_active) continue;
                if (seen.count(e)) continue;
                seen.insert(e);

                int i = id[e->Node_1];
                int j = id[e->Node_2];
                double x    = max(0.01, (double)e->loss);
                double flow = (theta[i] - theta[j]) / x;
                // cout<<e->control<<endl;

                // Sanity guard: NaN/Inf from degenerate topology → zero
                if (!isfinite(flow)) flow = 0.0;

                e->current_load = (float)flow;
            }
        }
    }

private:
    // ══════════════════════════════════════════════════════════════════
    //  assign_node_capacities
    //
    //  FIXES applied:
    //   1. Substation index into max_limits[] used a raw node->id which
    //      is offset by powerplant_count and can exceed the table size
    //      (OOB / buffer overread).  Now uses a sequential sub_idx
    //      modulo max_limits.size() — safe for any substation count.
    //   2. Plant capacity was multiplied against an uninitialised
    //      max_limit (always 0), making every plant have 0 capacity.
    //      Now each plant gets (share × total_substation_demand × HEADROOM).
    // ══════════════════════════════════════════════════════════════════
    void assign_node_capacities()
    {
        float total_sub_capacity = 0.0f;
        int   sub_seq            = 0;   // safe sequential index

        // ── Pass 1: assign substation capacities ──────────────────────
        for (auto &[node, _] : adj_power_substation) {
            if (node->node_type != "substation") continue;
            int   idx           = sub_seq++ % (int)max_limits.size();
            node->max_limit     = 2.0f * max_limits[idx];
            total_sub_capacity += node->max_limit;
        }

        // ── Pass 2: collect power plants ─────────────────────────────
        vector<Vertex_A*> plants;
        for (auto &[node, _] : adj_power_substation)
            if (node->node_type == "power_plant")
                plants.push_back(node);

        if (plants.empty()) return;

        // ── Pass 3: degree-weighted plant capacity ────────────────────
        //   Each plant's share is proportional to how many substations
        //   it directly feeds.  A 20 % headroom ensures the grid can
        //   always meet peak demand even with one plant at reduced load.
        float total_degree = 0.0f;
        unordered_map<Vertex_A*, float> degree_map;
        for (auto* p : plants) {
            float deg       = max((float)adj_power_substation[p].size(), 1.0f);
            degree_map[p]   = deg;
            total_degree   += deg;
        }

        constexpr float HEADROOM = 1.20f;   // 20 % reserve above peak demand
        for (auto* p : plants) {
            float share    = degree_map[p] / total_degree;
            float jitter   = 0.90f + 0.20f * ((float)rand() / RAND_MAX); // ±10 %
            p->max_limit   = total_sub_capacity * HEADROOM * share * jitter;
        }
    }

    // ══════════════════════════════════════════════════════════════════
    //  assign_edge_capacities
    //
    //  FIXES / improvements:
    //   1. Distance is computed in screen-pixel space (consistent with
    //      layout_graph); minimum enforced to avoid division by zero.
    //   2. Reactance (loss) is distance-based: x = d/100, min 0.1.
    //      This replaces the constructor's random value for all
    //      graph-built edges.
    //   3. Capacity formula unchanged; max_load floor raised to 15 MW
    //      so no edge becomes trivially small.
    // ══════════════════════════════════════════════════════════════════
    void assign_edge_capacities()
    {
        unordered_set<Edge*> seen;

        for (auto& [src, nbrs] : adj_power_substation) {
            for (auto& [dst, e] : nbrs) {
                if (seen.count(e)) continue;
                seen.insert(e);

                Vertex_A* u = e->Node_1;
                Vertex_A* v = e->Node_2;

                float d = (float)GetDistance(u->x, u->y, v->x, v->y);
                d = max(d, 1.0f);

                // ── Reactance: longer line → higher reactance ─────────
                e->loss = 1.0f;

                // ── Thermal capacity ──────────────────────────────────
                float weak_cap = min(u->max_limit, v->max_limit);

                float type_factor;
                if ((u->node_type == "power_plant") != (v->node_type == "power_plant"))
                    type_factor = 0.90f;   // transmission (plant↔sub)
                else
                    type_factor = 0.65f;   // distribution  (sub↔sub)

                // Longer line → reduced thermal rating
                float dist_factor = 1.0f / (1.0f + d / 300.0f);

                e->max_load = max(15.0f, weak_cap * type_factor * dist_factor);
            }
        }
    }

    double GetDistance(int x1, int y1, int x2, int y2)
    { return sqrt(pow(x1-x2,2.0)+pow(y1-y2,2.0)); }

    int GetRandom(int a, int b) {
        if (a > b) swap(a,b);
        return (rand()%(b-a+1))+a;
    }
    using GridKey = pair<int,int>;

    bool is_position_ok_A(Vertex_A *n, int x, int y,
                          const unordered_map<GridKey,vector<Vertex_A*>,PairHash>& grid, int CS) {
        float md = (n->node_type=="power_plant") ? 400.f : 50.f;
        int gx=x/CS, gy=y/CS;
        for (int dx=-1;dx<=1;dx++) for (int dy=-1;dy<=1;dy++) {
            GridKey k={gx+dx,gy+dy};
            if (grid.count(k)) for (auto* o : grid.at(k))
                if (o->node_type==n->node_type && GetDistance(x,y,o->x,o->y)<md) return false;
        }
        return true;
    }

    bool DoesEdgeExist(Vertex_A *a, Vertex_A *b) {
        auto check=[&](Vertex_A* s,Vertex_A* t){
            if(adj_power_substation.count(s)) for(auto& p:adj_power_substation[s]) if(p.first==t) return true;
            return false;
        };
        return check(a,b)||check(b,a);
    }

    struct PathState {
        float cap; vector<pair<Vertex_A*,Edge*>> path;
        PathState(float c,vector<pair<Vertex_A*,Edge*>> p):cap(c),path(move(p)){}
        bool operator<(const PathState& o)const{return cap<o.cap;}
    };


public:
    // ══════════════════════════════════════════════════════════════════
    //  save_graph / load_graph
    //
    //  FIX: load_graph was reading 3 floats per NODE (maxL, csvD, currD)
    //       but save_graph only writes 2 (max_limit, current_demand).
    //       The third `in >> currD` consumed the next tag token ("NODE"
    //       or "EDGE"), silently corrupting all subsequent parsing.
    //       Both functions are now aligned: 2 floats per NODE record.
    // ══════════════════════════════════════════════════════════════════
    void save_graph(const string& filename) {
        ofstream out(filename);
        if (!out.is_open()) {
            cerr << "Cannot open file for saving\n";
            return;
        }

        unordered_set<Vertex_A*> seen;
        for (auto& [src, nbrs] : adj_power_substation) {
            auto write_node = [&](Vertex_A* n) {
                if (seen.count(n)) return;
                seen.insert(n);
                out << "NODE "
                    << n->id           << " "
                    << n->node_type    << " "
                    << n->x            << " "
                    << n->y            << " "
                    << n->max_limit    << " "
                    << n->current_demand << "\n";
            };
            write_node(src);
            for (auto& [dst, _] : nbrs) write_node(dst);
        }

        unordered_set<Edge*> seen_edges;
        for (auto& [src, nbrs] : adj_power_substation) {
            for (auto& [dst, e] : nbrs) {
                if (seen_edges.count(e)) continue;
                seen_edges.insert(e);
                out << "EDGE "
                    << src->id        << " "
                    << dst->id        << " "
                    << e->loss        << " "
                    << e->max_load    << " "
                    << e->current_load << "\n";
            }
        }
        out.close();
    }

    void load_graph(const string& filename) {
        ifstream in(filename);
        if (!in.is_open()) {
            cerr << "Cannot open file for loading\n";
            return;
        }

        adj_power_substation.clear();
        adj_reverse_power.clear();
        overloaded_edges.clear();
        edge_being_fixed = nullptr;
        fix_path.clear();
        nodes_to_throttle.clear();
        node_overloads_visual.clear();
        throttled_nodes_visual.clear();

        unordered_map<int, Vertex_A*> id_to_node;
        vector<tuple<int,int,float,float,float>> edge_data;

        string tag;
        while (in >> tag) {
            if (tag == "NODE") {
                int    id, x, y;
                string type;
                float  maxL, currD;
                // FIX: read exactly 2 floats — matches save_graph output
                in >> id >> type >> x >> y >> maxL >> currD;

                Vertex_A* n       = new Vertex_A(type, id);
                n->x              = x;
                n->y              = y;
                n->max_limit      = maxL;
                n->current_demand = currD;
                id_to_node[id]    = n;
            }
            else if (tag == "EDGE") {
                int   src, dst;
                float loss, max_load, current_load;
                in >> src >> dst >> loss >> max_load >> current_load;
                edge_data.emplace_back(src, dst, loss, max_load, current_load);
            }
        }

        for (auto& [src_id, dst_id, loss, max_load, current_load] : edge_data) {
            Vertex_A* src = id_to_node[src_id];
            Vertex_A* dst = id_to_node[dst_id];
            if (!src || !dst) continue;   // guard against missing nodes

            Edge* e        = new Edge(src, dst);
            e->loss        = loss;
            e->max_load    = max_load;
            e->current_load = current_load;

            adj_power_substation[src].push_back({dst, e});
            adj_reverse_power[dst].push_back({src, e});
        }

        in.close();
    }

    vector<pair<string,pair<int,int>>> layout_graph() {
        vector<pair<string,pair<int,int>>> coords;
        unordered_set<Vertex_A*> vis;
        queue<Vertex_A*> q;
        vector<Vertex_A*> plants;
        unordered_map<GridKey,vector<Vertex_A*>,PairHash> grid;
        const int CS=50, RANGE=200;

        for(auto&[n,_]:adj_power_substation) if(n->node_type=="power_plant") plants.push_back(n);

        for(auto* pl:plants){
            if(vis.count(pl)) continue;
            bool placed=false;
            for(int t=0;t<1000;t++){
                int x=GetRandom(0,1200),y=GetRandom(0,800);
                if(is_position_ok_A(pl,x,y,grid,CS)){
                    pl->x=x;pl->y=y;vis.insert(pl);q.push(pl);
                    coords.push_back({"power_plant",{x,y}});
                    grid[{x/CS,y/CS}].push_back(pl); placed=true; break;
                }
            }
            if(!placed){
                pl->x=GetRandom(0,2000);pl->y=GetRandom(0,2000);
                vis.insert(pl);q.push(pl);coords.push_back({"power_plant",{pl->x,pl->y}});
                grid[{pl->x/CS,pl->y/CS}].push_back(pl);
            }
        }

        while(!q.empty()){
            Vertex_A* par=q.front();q.pop();
            if(!adj_power_substation.count(par)) continue;
            for(auto&[ch,_]:adj_power_substation[par]){
                if(ch->node_type!="substation"||vis.count(ch)) continue;
                bool placed=false;
                for(int t=0;t<1000;t++){
                    int x=GetRandom(par->x-RANGE,par->x+RANGE);
                    int y=GetRandom(par->y-RANGE,par->y+RANGE);
                    if(is_position_ok_A(ch,x,y,grid,CS)){
                        ch->x=x;ch->y=y;vis.insert(ch);q.push(ch);
                        coords.push_back({"substation",{x,y}});
                        grid[{x/CS,y/CS}].push_back(ch); placed=true; break;
                    }
                }
                if(!placed){
                    ch->x=par->x;ch->y=par->y;vis.insert(ch);q.push(ch);
                    coords.push_back({"substation",{ch->x,ch->y}});
                    grid[{ch->x/CS,ch->y/CS}].push_back(ch);
                }
            }
        }
        return coords;
    }

    void add_realistic_connections(int k_nearest=4) {
        unordered_set<Vertex_A*> sub_set;
        for(auto&entry:adj_power_substation){
            if(entry.first->node_type=="substation") sub_set.insert(entry.first);
            for(auto&p:entry.second) if(p.first->node_type=="substation") sub_set.insert(p.first);
        }
        vector<Vertex_A*> subs(sub_set.begin(),sub_set.end());
        if((int)subs.size()<k_nearest+1) return;
        for(auto* s1:subs){
            priority_queue<pair<double,Vertex_A*>> pq;
            for(auto* s2:subs){
                if(s1==s2) continue;
                double d=GetDistance(s1->x,s1->y,s2->x,s2->y);
                if((int)pq.size()<k_nearest) pq.push({d,s2});
                else if(d<pq.top().first){pq.pop();pq.push({d,s2});}
            }
            while(!pq.empty()){
                auto* s2=pq.top().second; pq.pop();
                if(!DoesEdgeExist(s1,s2)){
                    Edge* e=new Edge(s1,s2);
                    e->current_load=0;
                    // FIX: derive capacity/reactance consistently with
                    //      assign_edge_capacities rather than using max_limit directly
                    double d = max(1.0, GetDistance(s1->x, s1->y, s2->x, s2->y));
                    e->loss     = 1.0f;
                    float wc    = min(s1->max_limit, s2->max_limit);
                    float df    = 1.0f / (1.0f + (float)d / 300.0f);
                    e->max_load = max(15.0f, wc * 0.65f * df);
                    adj_power_substation[s1].push_back({s2,e});
                    adj_reverse_power[s2].push_back({s1,e});
                }
            }
        }
    }
    // ══════════════════════════════════════════════════════════════════
//  calibrate_edge_capacities
//
//  Runs DC flow once with each substation at its CSV-observed peak
//  demand, then sets max_load = max(structural_limit, peak_flow × k).
//  This makes bridge edges automatically receive high capacity and
//  lightly-used edges receive low capacity — topology-aware, not
//  endpoint-aware.
//
//  Call ONCE after load_full_year_csv(), before saving the graph.
// ══════════════════════════════════════════════════════════════════
    void calibrate_edge_capacities(float safety_factor = 6.0f)
    {
        // if (demand_schedule.empty()) {
        // cerr << "[Calibrate] No demand data — skipping calibration.\n";
        // return;
        // }

        // ── Step 1: find per-substation peak demand across all CSV rows ──
        // Collect substations in stable order (same as build_substations_list)
        vector<Vertex_A*> subs;
        unordered_set<Vertex_A*> seen_s;
        for (auto& [u, nbrs] : adj_power_substation) {
        auto add = [&](Vertex_A* n) {
        if (n->node_type == "substation" && seen_s.insert(n).second)
        subs.push_back(n);
        };
        add(u);
        for (auto& [v, _] : nbrs) add(v);
        }

        int n_subs = (int)subs.size();
            // Fixed max-limit table (100 entries for up to 100 substations)
        vector<float> peak = {
            118.0f, 51.0f, 33.0f, 108.0f, 21.0f, 118.0f, 26.0f, 35.0f, 72.0f, 13.0f,
            9.0f, 56.0f, 22.0f,  70.0f,  8.0f,  10.0f,115.0f, 80.0f, 27.0f, 15.0f,
            71.0f,114.0f, 91.0f,  71.0f, 10.0f,  82.0f, 90.0f, 25.0f, 36.0f, 50.0f,
            45.0f, 35.0f, 73.0f, 101.0f,120.0f,  25.0f,100.0f, 60.0f, 17.0f, 29.0f,
            67.0f, 23.0f, 50.0f,  55.0f, 54.0f,  91.0f, 50.0f, 46.0f, 15.0f, 28.0f,
            118.0f, 51.0f, 33.0f, 108.0f, 21.0f, 118.0f, 26.0f, 35.0f, 72.0f, 13.0f,
            9.0f, 56.0f, 22.0f,  70.0f,  8.0f,  10.0f,115.0f, 80.0f, 27.0f, 15.0f,
            71.0f,114.0f, 91.0f,  71.0f, 10.0f,  82.0f, 90.0f, 25.0f, 36.0f, 50.0f,
            45.0f, 35.0f, 73.0f, 101.0f,120.0f,  25.0f,100.0f, 60.0f, 17.0f, 29.0f,
            67.0f, 23.0f, 50.0f,  55.0f, 54.0f,  91.0f, 50.0f, 46.0f, 15.0f, 28.0f
        };
        // vector<float> peak(n_subs, 0.0f);

        // for (auto& row : demand_schedule)
        // for (int i = 0; i < n_subs && i < (int)row.size(); i++)
        // peak[i] = max(peak[i], row[i]);

        // ── Step 2: load peak demands and run DC flow ────────────────────
        for (int i = 0; i < n_subs; i++)
        subs[i]->current_demand = peak[i];
        for (int i = 0; i < n_subs; i++)
        subs[i]->per = 1.0f;

        recompute_flows();

        // ── Step 3: set max_load from actual peak flow ───────────────────
        float max_seen = 0.f, min_seen = 1e9f;
        unordered_set<Edge*> seen_e;
        for (auto& [u, nbrs] : adj_power_substation) {
        for (auto& [v, e] : nbrs) {
        if (!seen_e.insert(e).second) continue;
        float peak_flow = fabs(e->current_load);
        // Keep whichever is larger: structural limit or flow-based limit
        float flow_based = peak_flow * safety_factor;
        e->max_load = max(e->max_load, flow_based);
        // Hard floor: no edge below 10 MW
        e->max_load = max(e->max_load, 10.0f);
        max_seen = max(max_seen, e->max_load);
        min_seen = min(min_seen, e->max_load);
        }
        }

        // ── Step 4: reset demands and flows ─────────────────────────────
        for (auto* s : subs) s->current_demand = 0.0f;
        for (auto& [_, nbrs] : adj_power_substation)
        for (auto& [__, e] : nbrs)
        e->reset_flow();

        printf("[Calibrate] Done. safety=%.1fx  max_load range [%.1f, %.1f] MW\n",
        safety_factor, min_seen, max_seen);
    }
        
};

// ── Raylib helpers ────────────────────────────────────────────────────────
void DrawNodeA(graph::Vertex_A *node) {
    if(node->node_type=="power_plant"){DrawCircle(node->x,node->y,15,PINK);DrawCircleLines(node->x,node->y,15,MAROON);}
    else{DrawCircle(node->x,node->y,10,DARKBLUE);DrawCircleLines(node->x,node->y,12,BLACK);}
}

void DrawArrow(Vector2 s,Vector2 e,float r,float th,Color c) {
    Vector2 d=Vector2Normalize(Vector2Subtract(e,s));
    Vector2 ap=Vector2Subtract(e,Vector2Scale(d,r));
    DrawLineEx(s,ap,th,c);
    float as=8;
    DrawLineEx(ap,Vector2Add(ap,Vector2Scale(Vector2Rotate(d,-150*DEG2RAD),as/(th>2?1.5f:1.f))),th,c);
    DrawLineEx(ap,Vector2Add(ap,Vector2Scale(Vector2Rotate(d, 150*DEG2RAD),as/(th>2?1.5f:1.f))),th,c);
}

enum GameState { STATE_INPUT, STATE_VISUALIZATION };

int main(int argc, char** argv)
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    srand((unsigned int)time(0));

    bool is_headless = false;
    for (int i = 1; i < argc; i++) {
        if (string(argv[i]) == "--headless") {
            is_headless = true;
        }
    }

    InitWindow(1280, 720, "Power Grid — DQN Bridge");
    ToggleFullscreen();

    int W = GetScreenWidth();
    int H = GetScreenHeight();
    GameState currentState = STATE_INPUT;
    const int inputFieldCount = 2;
    const char* labels[2] = {"Power Plants:", "Substations:"};
    Rectangle textBoxes[2];
    std::string inputStrings[2] = {"5","100"};
    int activeTextBox = -1;

    Rectangle startButton;
    bool showCursor=false; int framesCounter=0;
    int startY=200,inputH=40,inputW=200,labelW=250,padding=15,fSize=20;
    for(int i=0;i<2;i++)
        textBoxes[i]={(float)W/2-inputW/2,(float)startY+i*(inputH+padding),(float)inputW,(float)inputH};
    startButton={(float)W/2-inputW/2,(float)startY+2*(inputH+padding)+20,(float)inputW,(float)inputH+10};

    graph G;
    vector<vector<float>> demand_schedule;

    // ZMQ bridge (created after graph is built)
    RLBridgeServer* bridge = nullptr;
    ObsLayout obs_layout;
    StepResult last_result;

    Camera2D camera={};
    const float ZOOM_LOD=0.5f;
    graph::Vertex_A* sel_node=nullptr;
    graph::Edge* sel_edge=nullptr;
    unordered_set<void*> vis_nodes;
    vector<graph::Vertex_A*> draw_nodes;

    enum FVS{VIZ_IDLE,VIZ_OVERLOAD,VIZ_FIX};
    FVS fvs=VIZ_IDLE; double fvt=0;
    bool rl_mode=true;

    SetTargetFPS(60);

    if (is_headless) {
        cout << "[Headless] Auto-starting simulation...\n";
        int np=stoi(inputStrings[0]), ns=stoi(inputStrings[1]);
        if (fs::exists("saved_graph.txt")) {
            G.load_graph("saved_graph.txt");
        } else {
            G.mapping(np, ns);
            G.layout_graph();
            G.add_realistic_connections(4);
            G.calibrate_edge_capacities(1.5f);
            G.save_graph("saved_graph.txt");
        }
        demand_schedule = G.load_full_year_csv("../src/cpp/demand_data.csv");
        bridge = new RLBridgeServer("tcp://*:5556");
        training_start_time = std::chrono::steady_clock::now();
        training_started = true;
        camera.target={(1000,1000)};
        camera.offset={(float)W/2,(float)H/2};
        camera.zoom=0.25f;
        currentState = STATE_VISUALIZATION;
    }

    while(!WindowShouldClose())
    {
        switch(currentState)
        {
        // ── INPUT STATE ────────────────────────────────────────────────
        case STATE_INPUT:
        {
            framesCounter++; showCursor=((framesCounter/30)%2==0);
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
                activeTextBox=-1;
                for(int i=0;i<2;i++)
                    if(CheckCollisionPointRec(GetMousePosition(),textBoxes[i])){activeTextBox=i;framesCounter=0;break;}
            }
            if(activeTextBox!=-1){
                int key=GetCharPressed();
                while(key>0){
                    if(key>='0'&&key<='9'&&inputStrings[activeTextBox].length()<9)
                        inputStrings[activeTextBox]+=(char)key;
                    key=GetCharPressed();
                }
                if(IsKeyPressed(KEY_BACKSPACE)&&!inputStrings[activeTextBox].empty())
                    inputStrings[activeTextBox].pop_back();
            }

            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(GetMousePosition(),startButton)){
                try {
                    int np=stoi(inputStrings[0]), ns=stoi(inputStrings[1]);
                    if(np>0&&ns>0){
                        cout<<"Building graph...\n";
                        if (fs::exists("saved_graph.txt")) {
                            G.load_graph("saved_graph.txt");
                        } else {
                            G.mapping(np, ns);
                            G.layout_graph();
                            G.add_realistic_connections(4);
                            // Calibrate BEFORE saving — saved graph stores correct max_load
                            G.calibrate_edge_capacities(1.5f);
                            G.save_graph("saved_graph.txt");
                        }
                        cout<<"Network built.\n";

                        // Load full-year CSV
                        demand_schedule=G.load_full_year_csv("../src/cpp/demand_data.csv");

                        // Start ZMQ bridge
                        bridge=new RLBridgeServer("tcp://*:5556");
                        cout<<"[Bridge] Ready. Start Python training.\n";

                        training_start_time = std::chrono::steady_clock::now();
                        training_started = true;

                        camera.target={(1000,1000)};
                        camera.offset={(float)W/2,(float)H/2};
                        camera.zoom=0.25f;
                        currentState=STATE_VISUALIZATION;
                    }
                } catch(...){ cout<<"Invalid input\n"; }
            }

            BeginDrawing(); ClearBackground(DARKGRAY);
            DrawText("Power Grid DQN Setup",W/2-MeasureText("Power Grid DQN Setup",30)/2,30,30,WHITE);
            DrawText("CSV: demand_data.csv",
                     W/2-MeasureText("CSV: demand_data.csv",18)/2,80,18,LIGHTGRAY);
            for(int i=0;i<2;i++){
                DrawText(labels[i],(int)(textBoxes[i].x-labelW),(int)(textBoxes[i].y+(inputH-fSize)/2.f),fSize,LIGHTGRAY);
                DrawRectangleRec(textBoxes[i],LIGHTGRAY);
                DrawRectangleLinesEx(textBoxes[i],activeTextBox==i?2:1,activeTextBox==i?RED:DARKGRAY);
                DrawText(inputStrings[i].c_str(),(int)(textBoxes[i].x+5),(int)(textBoxes[i].y+(inputH-fSize)/2.f),fSize,BLACK);
                if(activeTextBox==i&&showCursor){
                    int tw=MeasureText(inputStrings[i].c_str(),fSize);
                    DrawRectangle((int)(textBoxes[i].x+5+tw),(int)(textBoxes[i].y+4),2,inputH-8,MAROON);
                }
            }
            DrawRectangleRec(startButton,MAROON);
            DrawText("START SIMULATION",(int)(startButton.x+startButton.width/2-MeasureText("START SIMULATION",fSize)/2),
                     (int)(startButton.y+(startButton.height-fSize)/2),fSize,WHITE);
            EndDrawing();
        } break;

        // ── VISUALIZATION STATE ────────────────────────────────────────
        case STATE_VISUALIZATION:
        {   
            
            // Non-blocking ZMQ poll every frame
            if(bridge)
                bridge->poll(G, demand_schedule,
                             sim_hour, sim_month, sim_day_of_week, sim_date,
                             obs_layout, last_result);

            // Rebuild draw list
            vis_nodes.clear(); draw_nodes.clear();
            for(auto&[p,nb]:G.adj_power_substation){
                if(!vis_nodes.count(p)){draw_nodes.push_back(p);vis_nodes.insert(p);}
                for(auto&[c,e]:nb) if(!vis_nodes.count(c)){draw_nodes.push_back(c);vis_nodes.insert(c);}
            }

            // Camera
            camera.zoom=clamp(camera.zoom+(float)GetMouseWheelMove()*0.05f,0.1f,3.f);
            if(IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                camera.target=Vector2Add(camera.target,Vector2Scale(GetMouseDelta(),-1.f/camera.zoom));

            // Keys
            if(IsKeyPressed(KEY_R)) rl_mode=!rl_mode;

            // Selection
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
                Vector2 wm=GetScreenToWorld2D(GetMousePosition(),camera);
                sel_node=nullptr; sel_edge=nullptr; bool hit=false;
                for(auto* n:draw_nodes){
                    float r=(n->node_type=="power_plant")?15.f:10.f;
                    if(CheckCollisionPointCircle(wm,{(float)n->x,(float)n->y},r)){sel_node=n;hit=true;break;}
                }
                if(!hit) for(auto&[p,nb]:G.adj_power_substation)
                    for(auto&[c,e]:nb)
                        if(CheckCollisionPointLine(wm,{(float)p->x,(float)p->y},
                           {(float)c->x,(float)c->y},5/camera.zoom)){sel_edge=e;hit=true;break;}
            }
            if(fvs==VIZ_OVERLOAD&&GetTime()-fvt>=0.5) fvs=VIZ_FIX;

            long long elapsed_sec = 0;
            if (training_started) {
                auto now = std::chrono::steady_clock::now();
                elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - training_start_time).count();
            }
            int hours = elapsed_sec / 3600;
            int minutes = (elapsed_sec % 3600) / 60;
            int seconds = elapsed_sec % 60;

            char time_str[50];
            sprintf(time_str, "Train Time: %02d:%02d:%02d", hours, minutes, seconds);
            // ── DRAW ──────────────────────────────────────────────────
            BeginDrawing(); ClearBackground(DARKGRAY);
            BeginMode2D(camera);

            unordered_set<graph::Edge*> path_edges;
            if(fvs==VIZ_FIX) for(size_t i=1;i<G.fix_path.size();i++)
                if(G.fix_path[i].second) path_edges.insert(G.fix_path[i].second);

            for(auto&[par,nb]:G.adj_power_substation)
                for(auto&[chi,e]:nb){
                    float ratio=(e->max_load>0.01f)?e->current_load/e->max_load:0.f;
                    Color ec=(!e->is_active)?WHITE:(abs(ratio)>=1.00f)?BLACK:(abs(ratio)>=0.90f)?RED:(abs(ratio)>=0.70f)?ORANGE:(abs(ratio)>=0.40f)?GREEN:(abs(ratio)>=0.10f)?BLUE:SKYBLUE;
                    Vector2 sv={(float)par->x,(float)par->y};
                    Vector2 ev={(float)chi->x,(float)chi->y};
                    float er=(chi->node_type=="power_plant")?15.f:10.f;

                    bool gfx=(fvs==VIZ_FIX)&&(e==G.edge_being_fixed||path_edges.count(e));
                    bool ofx=(fvs==VIZ_OVERLOAD)&&e==G.edge_being_fixed;
                    bool red=G.overloaded_edges.count(e);

                    if(camera.zoom>=ZOOM_LOD){
                        if(gfx)       DrawArrow(sv,ev,er,4/camera.zoom,GREEN);
                        else if(ofx)  DrawArrow(sv,ev,er,4/camera.zoom,ORANGE);
                        else if(red)  {float th=3.f+sin(GetTime()*10)*1.5f;DrawArrow(sv,ev,er,th/camera.zoom,RED);}
                        else          DrawLineEx(sv,ev,1/camera.zoom,ec);
                        if(e==sel_edge) DrawLineEx(sv,ev,3/camera.zoom,YELLOW);
                    }
                }

            for(auto* n:draw_nodes){
                if(n->node_type=="substation"&&camera.zoom<ZOOM_LOD) continue;
                DrawNodeA(n);
                if(G.throttled_nodes_visual.count(n)){DrawCircle(n->x,n->y,10,RAYWHITE);DrawCircleLines(n->x,n->y,10,WHITE);}
                if(G.node_overloads_visual.count(n)) {DrawCircle(n->x,n->y,10,BLACK);DrawCircleLines(n->x,n->y,10,DARKGRAY);}
                if(n==G.last_event_substation){
                    float pr=10.f+2.f+sin(GetTime()*10)*1.5f;
                    DrawRingLines({(float)n->x,(float)n->y},pr-2/camera.zoom,pr,0,360,36,Fade(YELLOW,0.8f));
                }
                if(n==sel_node) DrawCircleLines(n->x,n->y,12,YELLOW);
            }
            EndMode2D();

            // UI overlay
            DrawRectangle(0,0,W,30,Fade(BLACK,0.8f));
            string mode_str=rl_mode?"[RL MODE — Python DQN Driving]":"[MANUAL MODE — Press R to enable RL]";
            DrawText(mode_str.c_str(),10,5,20,rl_mode?GREEN:ORANGE);
            DrawFPS(W-80,5);
            DrawText(time_str, W - 260, 180, 20, YELLOW);

            // Clock
            string day_names[]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
            string clk="Sim: "+day_names[sim_day_of_week]+", "+to_string(sim_date)+"/"+
                       to_string(sim_month)+" | "+(sim_hour<10?"0":"")+to_string(sim_hour)+":00";
            int cw=MeasureText(clk.c_str(),20)+40;
            DrawRectangle(W/2-cw/2,35,cw,35,Fade(BLACK,0.8f));
            DrawText(clk.c_str(),W/2-MeasureText(clk.c_str(),20)/2,43,20,GREEN);

            // RL stats
            if(rl_mode){
                DrawRectangle(W-270,35,260,140,Fade(BLACK,0.75f));
                string act_names[]={"No Action","Reroute","Throttle 10%","Throttle 20%"};
                DrawText(("Reward:   "+to_string(last_result.reward)).c_str(),         W-265,40,17,WHITE);
                DrawText(("Overloads:"+to_string(last_result.n_overloaded)).c_str(),    W-265,60,17,last_result.n_overloaded>0?RED:GREEN);
                DrawText(("MaxUtil:  "+to_string((int)(last_result.max_severity*100))+"%").c_str(),W-265,80,17,last_result.max_severity>=0.9f?ORANGE:WHITE);
                DrawText(("AvgUtil:  "+to_string((int)(last_result.avg_util*100))+"%").c_str(),  W-265,100,17,WHITE);
                DrawText(("Shed:     "+to_string((int)last_result.power_shed)+" MW").c_str(),     W-265,120,17,last_result.power_shed>0?ORANGE:WHITE);
                DrawText(("OBS dim:  "+to_string(obs_layout.total())).c_str(),          W-265,140,17,GRAY);
            }

            // Inspector
            if(sel_node){
                DrawRectangle(10,140,270,130,Fade(BLACK,0.8f));
                DrawText(("Type: "+sel_node->node_type).c_str(),15,145,16,YELLOW);
                DrawText(("Current Demand: "+to_string((int)sel_node->current_demand*sel_node->per)).c_str(),15,165,18,WHITE);
                DrawText(("Max Limit:  "+to_string((int)sel_node->max_limit)).c_str(),15,185,18,WHITE);
                if(G.node_overloads_visual.count(sel_node)) DrawText("OVERLOADED",15,205,18,RED);
            } else if(sel_edge){
                DrawRectangle(10,140,260,80,Fade(BLACK,0.8f));
                DrawText(("Load: "+to_string((int)sel_edge->current_load)+"/"+
                          to_string((int)sel_edge->max_load)).c_str(),15,145,18,WHITE);
                float u=sel_edge->current_load/max(sel_edge->max_load,0.01f);
                DrawText(("Util: "+to_string((int)(u*100))+"%").c_str(),15,165,18,u>=0.9f?RED:u>=0.7f?ORANGE:GREEN);
                DrawText(("Loss: "+to_string(sel_edge->loss)).c_str(),15,185,18,GRAY);
            }

            EndDrawing();
        } break;
        }
    }

    delete bridge;
    CloseWindow();
    return 0;
}