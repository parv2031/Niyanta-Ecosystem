#pragma once
#include <bits/stdc++.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────
// Step Result
// ─────────────────────────────────────────────────────────────
struct StepResult {
    std::vector<float> obs;
    float reward = 0.f;
    bool done = false;

    int n_overloaded = 0;
    float max_severity = 0.f;
    float avg_util = 0.f;
    float total_demand = 0.f;
    float power_shed = 0.f;

    int sim_hour = 0;
    int sim_month = 0;
    int csv_row = 0;
};

// ─────────────────────────────────────────────────────────────
// Obs Layout
// ─────────────────────────────────────────────────────────────
struct ObsLayout {
    int n_edges = 0;
    int n_subs = 50;
    int n_time = 8;
    int total() const { return n_edges + n_subs + n_time; }
};

// ─────────────────────────────────────────────────────────────
// RL Bridge
// ─────────────────────────────────────────────────────────────
class RLBridgeServer {
public:
    RLBridgeServer(const std::string& endpoint = "tcp://*:5556")
        : ctx_(1), socket_(ctx_, zmq::socket_type::rep)
    {
        socket_.bind(endpoint);
        std::cout << "[Bridge] Running on " << endpoint << "\n";
    }

    template<typename G_t>
    bool poll(G_t& G,
              const std::vector<std::vector<float>>& demand_schedule,
              int& sim_hour, int& sim_month,
              int& sim_day, int& sim_date,
              ObsLayout& layout,
              StepResult& result)
    {
        if (substations_list_.empty())
            build_substations_list(G);

        zmq::message_t msg;
        if (!socket_.recv(msg, zmq::recv_flags::dontwait))
            return false;

        json req = json::parse(msg.to_string_view());
        json reply;

        if (req["cmd"] == "reset")
        {
            csv_row_ = 0;
            reset_dynamic_state(G);

            if (!demand_schedule.empty())
                update_demands(G, demand_schedule[0]);

            G.recompute_flows();

            reply["obs"] = build_obs(G, 0, 1, layout);
            reply["reward"] = 0.0f;
            reply["done"] = false;
        }
        else
        {
            auto p_vec = req["p"].get<std::vector<float>>();
            auto e_vec = req["edge"].get<std::vector<float>>();

            result = execute_step(G, demand_schedule,
                                  sim_hour, sim_month,
                                  sim_day, sim_date,
                                  p_vec, e_vec, layout);

            reply["obs"] = result.obs;
            reply["reward"] = result.reward;
            reply["done"] = result.done;
        }

        socket_.send(zmq::buffer(reply.dump()), zmq::send_flags::none);
        return true;
    }

private:
    std::ofstream log_file_;
    bool log_initialized_ = false;
    zmq::context_t ctx_;
    zmq::socket_t socket_;
    int csv_row_ = 0;

    std::vector<void*> substations_list_;

    // ─────────────────────────────────────────────────────────────
    template<typename G_t>
    void build_substations_list(G_t& G)
    {
        std::unordered_set<typename G_t::Vertex_A*> seen;

        for (auto& [u, nbrs] : G.adj_power_substation)
        {
            if (u->node_type == "substation" && !seen.count(u))
            {
                seen.insert(u);
                substations_list_.push_back(u);
            }

            for (auto& [v, _] : nbrs)
            {
                if (v->node_type == "substation" && !seen.count(v))
                {
                    seen.insert(v);
                    substations_list_.push_back(v);
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────
    template<typename G_t>
    void reset_dynamic_state(G_t& G)
    {
        using VA = typename G_t::Vertex_A;

        G.overloaded_edges.clear();
        G.edge_being_fixed = nullptr;
        G.fix_path.clear();
        G.nodes_to_throttle.clear();
        G.node_overloads_visual.clear();
        G.throttled_nodes_visual.clear();

        for (auto& [u, nbrs] : G.adj_power_substation)
            for (auto& [v, e] : nbrs)
                e->reset_flow();

        for (void* vp : substations_list_)
        {
            VA* sub = static_cast<VA*>(vp);
            sub->per = 1.0f;
        }
    }

    // ─────────────────────────────────────────────────────────────
    template<typename G_t>
    void update_demands(G_t& G, const std::vector<float>& new_demands)
    {
        using VA = typename G_t::Vertex_A;

        for (int i = 0; i < static_cast<int>(substations_list_.size()); i++)
        {
            VA* sub = static_cast<VA*>(substations_list_[i]);
            if (i < static_cast<int>(new_demands.size()))
                sub->current_demand = new_demands[i];
        }
    }

    // ─────────────────────────────────────────────────────────────
    template<typename G_t>
    void apply_action(G_t& G,
                      const std::vector<float>& p_vec,
                      const std::vector<float>& e_vec)
    {
        using VA = typename G_t::Vertex_A;
        using E = typename G_t::Edge;

        // 🔹 Node control
        for (int i = 0; i < (int)substations_list_.size(); i++)
        {
            VA* sub = static_cast<VA*>(substations_list_[i]);
            float p = (i < (int)p_vec.size()) ? p_vec[i] : 1.0f;
            p = std::clamp(p, 0.0f, 1.0f);
            sub->per = p;
        }

        // 🔹 Edge control
        std::vector<E*> edges;
        std::unordered_set<E*> seen;

        for (auto& [u, nbrs] : G.adj_power_substation)
            for (auto& [v, e] : nbrs)
                if (seen.insert(e).second)
                    edges.push_back(e);

        for (int i = 0; i < (int)edges.size(); i++)
        {
            float s = (i < (int)e_vec.size()) ? e_vec[i] : 1.0f;
            if (s > 0.5f) edges[i]->connect();
            else          edges[i]->disconnect();
        }
        // fix_islands(G);
    }

    template<typename G_t>
    void fix_islands(G_t& G)
    {
        using VA = typename G_t::Vertex_A;
        using E  = typename G_t::Edge;

        std::unordered_set<VA*> visited;
        std::vector<std::vector<VA*>> components;

        for (auto& [start, _] : G.adj_power_substation)
        {
            if (visited.count(start)) continue;

            std::vector<VA*> comp;
            std::queue<VA*> q;
            q.push(start);
            visited.insert(start);

            while (!q.empty())
            {
                VA* u = q.front(); q.pop();
                comp.push_back(u);

                for (auto& [v, e] : G.adj_power_substation[u])
                {
                    if (!e->is_active) continue;
                    if (!visited.count(v))
                    {
                        visited.insert(v);
                        q.push(v);
                    }
                }
            }
            components.push_back(comp);
        }

        std::vector<int> has_plant(components.size(), 0);
        for (int i = 0; i < (int)components.size(); i++)
            for (VA* n : components[i])
                if (n->node_type == "power_plant") { has_plant[i] = 1; break; }

        for (int i = 0; i < (int)components.size(); i++)
        {
            if (has_plant[i]) continue;

            int target = -1;
            for (int j = 0; j < (int)components.size(); j++)
                if (has_plant[j]) { target = j; break; }

            if (target == -1) continue;

            for (VA* u : components[i])
                for (VA* v : components[target])
                    for (auto& [nbr, e] : G.adj_power_substation[u])
                        if (nbr == v) { e->connect(); goto next_component; }

        next_component:;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  execute_step  —  advance one hour and compute the reward
    // ═══════════════════════════════════════════════════════════════
    template<typename G_t>
    StepResult execute_step(G_t& G,
                            const std::vector<std::vector<float>>& demand_schedule,
                            int& sim_hour, int& sim_month,
                            int& sim_day, int& sim_date,
                            const std::vector<float>& p_vec,
                            const std::vector<float>& e_vec,
                            ObsLayout& layout)
    {
        if (!log_initialized_) {
            log_file_.open("step_log.csv");
            log_file_ << "row,hour,month,reward,"
                         "served_ratio,total_demand,avg_util,"
                         "n_trip,n_danger,n_healthy,n_disc,"
                         "power_shed,"
                         "r_serve,r_overload,r_danger,r_cascade,"
                         "r_health,r_shed,r_topology\n";
            log_initialized_ = true;
        }

        using VA = typename G_t::Vertex_A;
        using E  = typename G_t::Edge;

        // ── Advance simulation clock ──────────────────────────────────
        csv_row_++;
        sim_hour  = csv_row_ % 24;
        sim_day   = (csv_row_ / 24)  % 7;
        sim_month = (csv_row_ / 720) % 12 + 1;

        StepResult r;
        r.sim_hour  = sim_hour;
        r.sim_month = sim_month;
        r.csv_row   = csv_row_;

        if (csv_row_ >= static_cast<int>(demand_schedule.size()))
        {
            r.done   = true;
            r.obs    = build_obs(G, sim_hour, sim_month, layout);
            r.reward = 0.0f;
            return r;
        }

        // ── Load new demand profile, apply action, recompute flows ────
        update_demands(G, demand_schedule[csv_row_]);

        float total_demand = 0.0f;
        for (void* vp : substations_list_)
            total_demand += static_cast<VA*>(vp)->current_demand;

        apply_action(G, p_vec, e_vec);
        G.recompute_flows();
        // Diagnostic: print demand vs capacity balance once
        float tot_cap = 0, tot_dem = 0;
        for (void* vp : substations_list_) {
        auto* s = static_cast<VA*>(vp);
        tot_dem += s->current_demand;
        }
        for (auto& [u,nbrs] : G.adj_power_substation)
        if (u->node_type=="power_plant") tot_cap += u->max_limit;
        if (csv_row_ == 1)
        printf("[BAL] total_demand=%.1f plant_capacity=%.1f ratio=%.2f\n",
                tot_dem, tot_cap, tot_dem/tot_cap);

        // ── Served demand ─────────────────────────────────────────────
        float served = 0.0f;
        for (void* vp : substations_list_)
        {
            VA* sub = static_cast<VA*>(vp);
            served += sub->current_demand * sub->per;
        }

        // ═══════════════════════════════════════════════════════════════
        //  Per-edge statistics
        //
        //  Utilisation zones (based on |flow| / max_load):
        //
        //    [0.00, 0.30) → idle       (capacity wasted, not harmful)
        //    [0.30, 0.85) → healthy    (sweet-spot: efficient and safe)
        //    [0.85, 1.00) → danger     (near-limit, warn the agent)
        //    [1.00, ∞   ) → overload   (thermal trip risk — primary hazard)
        //
        //  Inactive edges are tracked separately; their flow = 0 by
        //  construction, so they would always score as "idle" if included —
        //  instead we count them as disconnected and penalise separately.
        // ═══════════════════════════════════════════════════════════════
        std::unordered_set<E*> seen;
        float overload_sq_sum = 0.f;   // Σ (util − 1.0)²  for overloaded edges
        float danger_sq_sum   = 0.f;   // Σ (util − 0.85)² for danger-zone edges
        float util_sum        = 0.f;   // for display / logging (not reward)
        float max_util        = 0.f;

        int edge_count    = 0;         // all unique edges (active + inactive)
        int n_trip        = 0;         // util > 1.0   (actual thermal overload)
        int n_danger      = 0;         // util ∈ (0.85, 1.0]
        int n_healthy     = 0;         // util ∈ (0.30, 0.85]
        int n_disconnected = 0;        // inactive edges

        for (auto& [u, nbrs] : G.adj_power_substation)
        {
            for (auto& [v, e] : nbrs)
            {
                if (!seen.insert(e).second) continue;
                edge_count++;

                if (!e->is_active)
                {
                    n_disconnected++;
                    continue;   // disconnected: util = 0 by definition, skip zones
                }

                // Clamp to 3.0 to guard against DC-flow solver runaway
                float util = std::fabs(e->current_load) / (e->max_load + 1e-6f);
                util = std::min(util, 3.0f);

                util_sum += util;                          // accumulate ONCE (bug fix)
                max_util  = std::max(max_util, util);

                if (util > 1.0f)
                {
                    overload_sq_sum += (util - 1.0f) * (util - 1.0f);
                    n_trip++;
                }
                else if (util > 0.85f)
                {
                    danger_sq_sum += (util - 0.85f) * (util - 0.85f);
                    n_danger++;
                }
                else if (util > 0.30f)
                {
                    n_healthy++;
                }
                // else: idle zone — no penalty, no bonus
            }
        }

        // ── Normalised scalars ────────────────────────────────────────
        float E_f         = (float)std::max(1, edge_count);
        float trip_ratio  = (float)n_trip        / E_f;
        float disc_ratio  = (float)n_disconnected/ E_f;
        float hlth_ratio  = (float)n_healthy     / E_f;
        float avg_ovl_sq  = overload_sq_sum      / E_f;  // mean overload severity²
        float avg_dng_sq  = danger_sq_sum        / E_f;  // mean danger severity²
        float avg_util    = util_sum              / E_f;  // mean utilisation (display)

        float served_ratio = (total_demand > 1e-6f)
                             ? std::clamp(served / (total_demand + 1e-6f), 0.0f, 1.0f)
                             : 1.0f;
        float power_shed   = std::max(0.0f, total_demand - served);
        float shed_ratio   = (total_demand > 1e-6f)
                             ? std::clamp(power_shed / (total_demand + 1e-6f), 0.0f, 1.0f)
                             : 0.0f;

        // ═══════════════════════════════════════════════════════════════
        //  Reward function
        //
        //  Design goals (in priority order):
        //    1. HIGH SERVING  — maximise fraction of real demand met
        //    2. LOW TRIPPING  — zero overloaded edges above thermal limit
        //    3. SAFE MARGIN   — operate in the healthy utilisation band
        //    4. TARGETED SHED — only throttle when overloads actually exist
        //
        //  Component breakdown:
        //
        //  r_serve    [0.0,  4.0]
        //    Linear in served_ratio.  Primary dense positive signal.
        //    Gives the agent a smooth gradient to serve more at every step.
        //
        //  r_overload  (−∞,  0.0]  — TWO-PART penalty per tripped edge
        //    Part A: −10 × trip_ratio
        //      Flat cost per overloaded edge.  Even a single trip at
        //      util = 1.001 costs 10/E — larger than the service gain
        //      from serving the marginal MW that caused it.
        //    Part B: −15 × avg_ovl_sq
        //      Severity term.  Quadratic beyond the threshold gives the
        //      agent a continuous gradient to pull util back under 1.0,
        //      not just push it to exactly 1.0.
        //
        //  r_danger   (−∞,  0.0]
        //    Smooth quadratic ramp starting at util = 0.85.
        //    The agent learns there is a safe approach speed into
        //    the overload region (−2 × avg_dng_sq is small at 0.86
        //    but grows fast toward 1.0).
        //
        //  r_cascade  [−6.0, 0.0]
        //    Superlinear penalty: −6 × trip_ratio².
        //    One trip in 150 edges → −0.0003 (negligible).
        //    30% of edges tripped   → −0.54.
        //    60% of edges tripped   → −2.16.
        //    Mirrors the real-world cascade dynamic where successive
        //    relay trips rapidly accelerate toward blackout.
        //
        //  r_health   [0.0,  0.3]
        //    Fraction of edges in the healthy zone gets a small bonus.
        //    Encourages the agent to spread load across the network
        //    and avoid starving some lines while saturating others.
        //
        //  r_shed     [−1.5, 0.0]
        //    Penalises unnecessary demand curtailment.
        //    Critically, it scales with (1 − trip_ratio):
        //      · No trips   → full shed penalty (throttling is unjustified)
        //      · All tripped → zero shed penalty (throttling is correct)
        //    This removes the conflicting gradient where the agent was
        //    punished both for overloads AND for throttling to fix them.
        //
        //  r_topology [−0.5, 0.0]
        //    Small cost per disconnected edge.
        //    Discourages the agent from using aggressive topology changes
        //    (edge disconnection) as a lazy way to suppress overloads.
        //
        //  Total range (before clamp): approximately [−10, 4.3]
        //  Clamped to [−10, 10] to bound critic targets.
        // ═══════════════════════════════════════════════════════════════

        // ── Component computations ────────────────────────────────────

        // 1. Service: primary positive signal
        float r_serve    =  4.0f * served_ratio;

        // 2. Overload: flat cost per trip  +  quadratic severity
        //    Calibrated so that 1 trip in 150 edges costs more than
        //    the gain from serving the marginal demand that caused it.
        float r_overload = -10.0f * trip_ratio
                         - 15.0f * avg_ovl_sq;

        // 3. Danger zone: smooth early warning to decelerate before 1.0
        float r_danger   = - 2.0f * avg_dng_sq;

        // 4. Cascade: superlinear risk as trip fraction grows
        float r_cascade  = - 6.0f * trip_ratio * trip_ratio;

        // 5. Healthy zone bonus: reward balanced, efficient operation
        float r_health   =   0.3f * hlth_ratio;

        // 6. Shedding: penalise curtailment only when the grid is safe
        //    (zero penalty when trip_ratio = 1 → shedding is fully justified)
        float r_shed     = - 4.0f * shed_ratio * (1.0f - trip_ratio);

        // 7. Topology: slight cost for each disconnected edge
        float r_topology = - 0.5f * disc_ratio;

        float reward = r_serve   + r_overload + r_danger   + r_cascade
                     + r_health  + r_shed     + r_topology;

        reward = std::clamp(reward, -10.0f, 10.0f);

        // ── Fill result struct ────────────────────────────────────────
        r.reward       = reward;
        r.done         = false;
        r.n_overloaded = n_trip;          // what the visualiser calls "overloads"
        r.max_severity = max_util;
        r.avg_util     = avg_util;
        r.total_demand = total_demand;
        r.power_shed   = power_shed;
        r.obs = build_obs(G, sim_hour, sim_month, layout);

        // ── Extended CSV log (all components for debugging) ───────────
        log_file_
            << r.csv_row    << ","
            << r.sim_hour   << ","
            << r.sim_month  << ","
            << r.reward     << ","
            << served_ratio << ","
            << total_demand << ","
            << avg_util     << ","
            << n_trip       << ","
            << n_danger     << ","
            << n_healthy    << ","
            << n_disconnected << ","
            << power_shed   << ","
            << r_serve      << ","
            << r_overload   << ","
            << r_danger     << ","
            << r_cascade    << ","
            << r_health     << ","
            << r_shed       << ","
            << r_topology   << "\n";
        log_file_.flush();

        return r;
    }

    // ─────────────────────────────────────────────────────────────
    template<typename G_t>
    std::vector<float> build_obs(G_t& G, int hour, int month, ObsLayout& layout)
    {
        std::vector<float> obs;

        for (auto& [u, nbrs] : G.adj_power_substation)
            for (auto& [v, e] : nbrs)
                obs.push_back(e->current_load / (e->max_load + 1e-6f));

        using VA = typename G_t::Vertex_A;
        for (void* vp : substations_list_)
        {
            VA* sub = static_cast<VA*>(vp);
            obs.push_back(sub->current_demand / (sub->max_limit + 1e-6f));
        }

        return obs;
    }
};