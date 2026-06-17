#include "timing_engine.h"
#include <algorithm>
#include <iostream>

TimingEngine::TimingEngine(const std::string& r_name,
                           const std::unordered_map<std::string, ClockNode>& tree,
                           const std::unordered_map<std::string, BufferCell>& lib,
                           const std::vector<DataPath>& ss_p,
                           const std::vector<DataPath>& ff_p,
                           double period)
    : root_name(r_name), clock_tree(tree), buf_lib(lib), ss_paths(ss_p), ff_paths(ff_p), clock_period(period) {
    computeTreeDelays();
}

void TimingEngine::updateTiming(const std::unordered_map<std::string, ClockNode>& new_tree) {
    clock_tree = new_tree;
    computeTreeDelays();
}

void TimingEngine::computeTreeDelays() {
    ss_delays.clear();
    ff_delays.clear();
    computeDfs(root_name, 0.0, 0.0);
}

void TimingEngine::computeDfs(const std::string& node_name, double current_ss_delay, double current_ff_delay) {
    ss_delays[node_name] = current_ss_delay;
    ff_delays[node_name] = current_ff_delay;

    const auto& node = clock_tree[node_name];
    int fanout = node.children.size();
    if (fanout == 0) return;

    double cell_ss_delay = 0.0;
    double cell_ff_delay = 0.0;

    if (node.type != "ROOT") {
        if (buf_lib.find(node.type) != buf_lib.end()) {
            const auto& cell = buf_lib[node.type];
            int idx = std::min(fanout, cell.max_fanout) - 1;
            if (idx >= 0) {
                cell_ss_delay = cell.ss_delays[idx];
                cell_ff_delay = cell.ff_delays[idx];
            }
        }
    }

    for (const auto& child : node.children) {
        computeDfs(child, current_ss_delay + cell_ss_delay, current_ff_delay + cell_ff_delay);
    }
}

void TimingEngine::evaluateMetrics(double& tns_ss, double& wns_ss, double& tns_ff, double& wns_ff, double& total_area) const {
    tns_ss = 0.0;
    wns_ss = 0.0;
    tns_ff = 0.0;
    wns_ff = 0.0;
    total_area = 0.0;
    const double setup_time = 0.08 * clock_period;
    const double hold_time = 0.05 * clock_period;

    // 1. Setup Slack Check (SS Corner)
    for (const auto& path : ss_paths) {
        double d_launch = ss_delays.at(path.launch_ff);
        double d_capture = ss_delays.at(path.capture_ff);
        double skew = d_capture - d_launch;
        double slack = clock_period - setup_time - path.delay + skew;
        
        if (slack < 0.0) {
            tns_ss += slack;
            if (slack < wns_ss) wns_ss = slack;
        }
    }

    // 2. Hold Slack Check (FF Corner)
    for (const auto& path : ff_paths) {
        double d_launch = ff_delays.at(path.launch_ff);
        double d_capture = ff_delays.at(path.capture_ff);
        double skew = d_capture - d_launch;
        double slack = path.delay - hold_time - skew;

        if (slack < 0.0) {
            tns_ff += slack;
            if (slack < wns_ff) wns_ff = slack;
        }
    }

    // 3. Compute Area (Excluding Sinks and Root)
    for (const auto& pair : clock_tree) {
        const auto& node = pair.second;
        if (node.type != "ROOT" && !node.is_sink) {
            if (buf_lib.find(node.type) != buf_lib.end()) {
                total_area += buf_lib.at(node.type).area;
            }
        }
    }
}
