#include "timing_engine.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

TimingEngine::TimingEngine(const std::string& r_name,
                           const std::unordered_map<std::string, ClockNode>& tree,
                           const std::unordered_map<std::string, BufferCell>& lib,
                           const std::vector<DataPath>& ss_p,
                           const std::vector<DataPath>& ff_p,
                           double period,
                           double hold_guard)
    : root_name(r_name),
      clock_tree(tree),
      buf_lib(lib),
      ss_paths(ss_p),
      ff_paths(ff_p),
      clock_period(period),
      setup_time(0.08 * period),
      hold_time(0.05 * period + hold_guard) {
    rebuild();
}

void TimingEngine::updateTiming(const std::unordered_map<std::string, ClockNode>& new_tree) {
    clock_tree = new_tree;
    rebuild();
}

const TimingMetrics& TimingEngine::metrics() const {
    return current_metrics;
}

TimingMetrics TimingEngine::computeCurrentMetrics() const {
    return current_metrics;
}

ScoreTerms TimingEngine::computeScoreTerms(const TimingMetrics& metrics,
                                           const TimingMetrics& original) {
    const auto term = [](double current, double baseline) {
        return baseline < -1e-12 ? 1.0 - current / baseline : 0.0;
    };
    ScoreTerms score;
    score.ss_tns = term(metrics.tns_ss, original.tns_ss);
    score.ss_wns = term(metrics.wns_ss, original.wns_ss);
    score.ff_tns = term(metrics.tns_ff, original.tns_ff);
    score.ff_wns = term(metrics.wns_ff, original.wns_ff);
    score.area = original.area > 1e-12 ? 1.0 - metrics.area / original.area : 0.0;
    score.timing_score = 4.0 * score.ss_wns + 3.0 * score.ss_tns +
                         2.0 * score.ff_wns + 1.5 * score.ff_tns +
                         0.5 * score.area;
    const double alphas[] = {0.50, 0.60, 0.70, 0.80, 0.90};
    for (double alpha : alphas) {
        const double beta = (1.0 - alpha) / 2.0;
        score.robust_score += alpha * (score.ss_tns + score.ss_wns) +
                              beta * (score.ff_tns + score.ff_wns) +
                              beta * score.area;
    }
    score.robust_score /= 5.0;
    return score;
}

void TimingEngine::evaluateMetrics(double& tns_ss,
                                   double& wns_ss,
                                   double& tns_ff,
                                   double& wns_ff,
                                   double& total_area) const {
    tns_ss = current_metrics.tns_ss;
    wns_ss = current_metrics.wns_ss;
    tns_ff = current_metrics.tns_ff;
    wns_ff = current_metrics.wns_ff;
    total_area = current_metrics.area;
}

void TimingEngine::rebuild() {
    ss_delays.clear();
    ff_delays.clear();
    sink_names.clear();
    sink_id.clear();
    sink_ss_delay.clear();
    sink_ff_delay.clear();
    node_descendant_sinks.clear();
    setup_path_states.clear();
    hold_path_states.clear();
    setup_launch_paths.clear();
    setup_capture_paths.clear();
    hold_launch_paths.clear();
    hold_capture_paths.clear();
    setup_negative_slacks.clear();
    hold_negative_slacks.clear();
    current_metrics = TimingMetrics();

    computeTreeDelays();
    buildSinkMaps();
    buildPathStates();
    computeArea();
    refreshWns();
}

double TimingEngine::nodeDelay(const std::string& type, int fanout, bool ss_corner) const {
    if (type == "ROOT" || fanout <= 0) return 0.0;
    std::unordered_map<std::string, BufferCell>::const_iterator it = buf_lib.find(type);
    if (it == buf_lib.end()) return 0.0;
    const BufferCell& cell = it->second;
    int idx = std::min(fanout, cell.max_fanout) - 1;
    if (idx < 0 || idx >= static_cast<int>(cell.ss_delays.size()) ||
        idx >= static_cast<int>(cell.ff_delays.size())) {
        return 0.0;
    }
    return ss_corner ? cell.ss_delays[idx] : cell.ff_delays[idx];
}

double TimingEngine::cellArea(const std::string& type) const {
    std::unordered_map<std::string, BufferCell>::const_iterator it = buf_lib.find(type);
    return it == buf_lib.end() ? 0.0 : it->second.area;
}

bool TimingEngine::fanoutLegal(const std::string& type, int fanout) const {
    if (type == "ROOT") return true;
    std::unordered_map<std::string, BufferCell>::const_iterator it = buf_lib.find(type);
    return it != buf_lib.end() && fanout <= it->second.max_fanout;
}

void TimingEngine::computeTreeDelays() {
    computeDfs(root_name, 0.0, 0.0);
}

void TimingEngine::computeDfs(const std::string& node_name,
                              double current_ss_delay,
                              double current_ff_delay) {
    ss_delays[node_name] = current_ss_delay;
    ff_delays[node_name] = current_ff_delay;

    std::unordered_map<std::string, ClockNode>::const_iterator it = clock_tree.find(node_name);
    if (it == clock_tree.end()) return;

    const ClockNode& node = it->second;
    int fanout = static_cast<int>(node.children.size());
    double next_ss = current_ss_delay + nodeDelay(node.type, fanout, true);
    double next_ff = current_ff_delay + nodeDelay(node.type, fanout, false);

    for (std::size_t i = 0; i < node.children.size(); ++i) {
        computeDfs(node.children[i], next_ss, next_ff);
    }
}

void TimingEngine::buildSinkMaps() {
    for (std::unordered_map<std::string, ClockNode>::const_iterator it = clock_tree.begin();
         it != clock_tree.end(); ++it) {
        if (!it->second.is_sink) continue;
        int id = static_cast<int>(sink_names.size());
        sink_id[it->first] = id;
        sink_names.push_back(it->first);
        sink_ss_delay.push_back(ss_delays[it->first]);
        sink_ff_delay.push_back(ff_delays[it->first]);
    }

    setup_launch_paths.resize(sink_names.size());
    setup_capture_paths.resize(sink_names.size());
    hold_launch_paths.resize(sink_names.size());
    hold_capture_paths.resize(sink_names.size());

    buildDescendantSinkMapDfs(root_name);
}

void TimingEngine::collectDescendantSinks(const std::string& node_name, std::vector<int>& sinks) {
    std::unordered_map<std::string, ClockNode>::const_iterator node_it = clock_tree.find(node_name);
    if (node_it == clock_tree.end()) return;
    if (node_it->second.is_sink) {
        std::unordered_map<std::string, int>::const_iterator sink_it = sink_id.find(node_name);
        if (sink_it != sink_id.end()) sinks.push_back(sink_it->second);
        return;
    }
    for (std::size_t i = 0; i < node_it->second.children.size(); ++i) {
        collectDescendantSinks(node_it->second.children[i], sinks);
    }
}

const std::vector<int>& TimingEngine::buildDescendantSinkMapDfs(const std::string& node_name) {
    std::unordered_map<std::string, std::vector<int> >::const_iterator cached =
        node_descendant_sinks.find(node_name);
    if (cached != node_descendant_sinks.end()) return cached->second;

    std::vector<int>& sinks = node_descendant_sinks[node_name];
    std::unordered_map<std::string, ClockNode>::const_iterator node_it = clock_tree.find(node_name);
    if (node_it == clock_tree.end()) return sinks;

    if (node_it->second.is_sink) {
        std::unordered_map<std::string, int>::const_iterator sink_it = sink_id.find(node_name);
        if (sink_it != sink_id.end()) sinks.push_back(sink_it->second);
        return sinks;
    }

    for (std::size_t i = 0; i < node_it->second.children.size(); ++i) {
        const std::vector<int>& child_sinks = buildDescendantSinkMapDfs(node_it->second.children[i]);
        sinks.insert(sinks.end(), child_sinks.begin(), child_sinks.end());
    }
    return sinks;
}

void TimingEngine::buildPathStates() {
    setup_path_states.reserve(ss_paths.size());
    for (std::size_t i = 0; i < ss_paths.size(); ++i) {
        const DataPath& path = ss_paths[i];
        std::unordered_map<std::string, int>::const_iterator launch = sink_id.find(path.launch_ff);
        std::unordered_map<std::string, int>::const_iterator capture = sink_id.find(path.capture_ff);
        if (launch == sink_id.end() || capture == sink_id.end()) continue;

        PathState state;
        state.name = path.name;
        state.launch_sink = launch->second;
        state.capture_sink = capture->second;
        state.data_delay = path.delay;
        state.slack = clock_period - setup_time - path.delay
                    + sink_ss_delay[state.capture_sink]
                    - sink_ss_delay[state.launch_sink];

        int id = static_cast<int>(setup_path_states.size());
        setup_path_states.push_back(state);
        setup_launch_paths[state.launch_sink].push_back(id);
        setup_capture_paths[state.capture_sink].push_back(id);
        addSetupSlack(state.slack);
    }

    hold_path_states.reserve(ff_paths.size());
    for (std::size_t i = 0; i < ff_paths.size(); ++i) {
        const DataPath& path = ff_paths[i];
        std::unordered_map<std::string, int>::const_iterator launch = sink_id.find(path.launch_ff);
        std::unordered_map<std::string, int>::const_iterator capture = sink_id.find(path.capture_ff);
        if (launch == sink_id.end() || capture == sink_id.end()) continue;

        PathState state;
        state.name = path.name;
        state.launch_sink = launch->second;
        state.capture_sink = capture->second;
        state.data_delay = path.delay;
        state.slack = path.delay - hold_time
                    - sink_ff_delay[state.capture_sink]
                    + sink_ff_delay[state.launch_sink];

        int id = static_cast<int>(hold_path_states.size());
        hold_path_states.push_back(state);
        hold_launch_paths[state.launch_sink].push_back(id);
        hold_capture_paths[state.capture_sink].push_back(id);
        addHoldSlack(state.slack);
    }
}

void TimingEngine::computeArea() {
    for (std::unordered_map<std::string, ClockNode>::const_iterator it = clock_tree.begin();
         it != clock_tree.end(); ++it) {
        const ClockNode& node = it->second;
        if (node.type == "ROOT" || node.is_sink) continue;
        current_metrics.area += cellArea(node.type);
    }
}

void TimingEngine::removeSetupSlack(double slack) {
    if (slack >= 0.0) return;
    current_metrics.tns_ss -= slack;
    --current_metrics.nvp_ss;
    std::multiset<double>::iterator it = setup_negative_slacks.find(slack);
    if (it != setup_negative_slacks.end()) setup_negative_slacks.erase(it);
}

void TimingEngine::addSetupSlack(double slack) {
    if (slack >= 0.0) return;
    current_metrics.tns_ss += slack;
    ++current_metrics.nvp_ss;
    setup_negative_slacks.insert(slack);
}

void TimingEngine::removeHoldSlack(double slack) {
    if (slack >= 0.0) return;
    current_metrics.tns_ff -= slack;
    --current_metrics.nvp_ff;
    std::multiset<double>::iterator it = hold_negative_slacks.find(slack);
    if (it != hold_negative_slacks.end()) hold_negative_slacks.erase(it);
}

void TimingEngine::addHoldSlack(double slack) {
    if (slack >= 0.0) return;
    current_metrics.tns_ff += slack;
    ++current_metrics.nvp_ff;
    hold_negative_slacks.insert(slack);
}

void TimingEngine::refreshWns() {
    current_metrics.wns_ss = setup_negative_slacks.empty() ? 0.0 : *setup_negative_slacks.begin();
    current_metrics.wns_ff = hold_negative_slacks.empty() ? 0.0 : *hold_negative_slacks.begin();
}

bool TimingEngine::moveDelta(const TimingMove& move,
                             std::vector<int>& affected_sinks,
                             double& delta_ss,
                             double& delta_ff,
                             double& delta_area) const {
    delta_ss = 0.0;
    delta_ff = 0.0;
    delta_area = 0.0;
    affected_sinks.clear();

    if (move.kind == TimingMoveKind::ResizeBuffer) {
        std::unordered_map<std::string, ClockNode>::const_iterator node_it = clock_tree.find(move.node);
        std::unordered_map<std::string, BufferCell>::const_iterator cell_it = buf_lib.find(move.new_type);
        if (node_it == clock_tree.end() || cell_it == buf_lib.end()) return false;

        const ClockNode& node = node_it->second;
        if (node.type == "ROOT" || node.is_sink) return false;
        int fanout = static_cast<int>(node.children.size());
        if (!fanoutLegal(move.new_type, fanout)) return false;

        delta_ss = nodeDelay(move.new_type, fanout, true) - nodeDelay(node.type, fanout, true);
        delta_ff = nodeDelay(move.new_type, fanout, false) - nodeDelay(node.type, fanout, false);
        delta_area = cellArea(move.new_type) - cellArea(node.type);

        std::unordered_map<std::string, std::vector<int> >::const_iterator sinks_it =
            node_descendant_sinks.find(move.node);
        if (sinks_it == node_descendant_sinks.end()) return false;
        affected_sinks = sinks_it->second;
        return true;
    }

    if (move.kind == TimingMoveKind::InsertChain) {
        std::unordered_map<std::string, ClockNode>::const_iterator parent_it = clock_tree.find(move.parent);
        std::unordered_map<std::string, ClockNode>::const_iterator child_it = clock_tree.find(move.child);
        if (parent_it == clock_tree.end() || child_it == clock_tree.end()) return false;
        if (move.chain_types.empty()) return false;

        bool found_child = false;
        for (std::size_t i = 0; i < parent_it->second.children.size(); ++i) {
            if (parent_it->second.children[i] == move.child) {
                found_child = true;
                break;
            }
        }
        if (!found_child) return false;

        for (std::size_t i = 0; i < move.chain_types.size(); ++i) {
            if (!fanoutLegal(move.chain_types[i], 1)) return false;
            delta_ss += nodeDelay(move.chain_types[i], 1, true);
            delta_ff += nodeDelay(move.chain_types[i], 1, false);
            delta_area += cellArea(move.chain_types[i]);
        }

        std::unordered_map<std::string, std::vector<int> >::const_iterator sinks_it =
            node_descendant_sinks.find(move.child);
        if (sinks_it == node_descendant_sinks.end()) return false;
        affected_sinks = sinks_it->second;
        return true;
    }

    return false;
}

std::vector<int> TimingEngine::affectedPathIds(const TimingMove& move,
                                               bool setup_paths) const {
    std::vector<int> affected_sinks;
    double delta_ss = 0.0, delta_ff = 0.0, delta_area = 0.0;
    if (!moveDelta(move, affected_sinks, delta_ss, delta_ff, delta_area)) return {};

    std::unordered_set<int> unique;
    for (int sink : affected_sinks) {
        const std::vector<int>& launch = setup_paths ? setup_launch_paths[sink]
                                                     : hold_launch_paths[sink];
        const std::vector<int>& capture = setup_paths ? setup_capture_paths[sink]
                                                      : hold_capture_paths[sink];
        unique.insert(launch.begin(), launch.end());
        unique.insert(capture.begin(), capture.end());
    }
    std::vector<int> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<int> TimingEngine::getAffectedSetupPaths(const TimingMove& move) const {
    return affectedPathIds(move, true);
}

std::vector<int> TimingEngine::getAffectedHoldPaths(const TimingMove& move) const {
    return affectedPathIds(move, false);
}

IncrementalEvalResult TimingEngine::evaluateResizeMove(const std::string& node,
                                                       const std::string& new_type) {
    TimingMove move;
    move.kind = TimingMoveKind::ResizeBuffer;
    move.node = node;
    move.new_type = new_type;
    return evaluateMove(move);
}

IncrementalEvalResult TimingEngine::evaluateInsertChainMove(
        const std::string& parent,
        const std::string& child,
        const std::vector<std::string>& chain_types) {
    TimingMove move;
    move.kind = TimingMoveKind::InsertChain;
    move.parent = parent;
    move.child = child;
    move.chain_types = chain_types;
    return evaluateMove(move);
}

IncrementalEvalResult TimingEngine::evaluateSubtreeMove(const TimingMove& move) {
    // Both supported move kinds operate on every descendant sink of their
    // target node/edge; moveDelta and the precomputed path incidence maps
    // restrict evaluation to paths touching that subtree.
    return evaluateMove(move);
}

bool TimingEngine::commitResizeMove(const std::string& node,
                                    const std::string& new_type) {
    TimingMove move;
    move.kind = TimingMoveKind::ResizeBuffer;
    move.node = node;
    move.new_type = new_type;
    std::vector<int> affected_sinks;
    double delta_ss = 0.0, delta_ff = 0.0, delta_area = 0.0;
    if (!moveDelta(move, affected_sinks, delta_ss, delta_ff, delta_area)) return false;

    std::unordered_set<int> changed(affected_sinks.begin(), affected_sinks.end());
    std::unordered_set<int> seen_setup;
    std::unordered_set<int> seen_hold;
    for (int sink : affected_sinks) {
        sink_ss_delay[sink] += delta_ss;
        sink_ff_delay[sink] += delta_ff;
        for (int path_id : setup_launch_paths[sink]) {
            if (!seen_setup.insert(path_id).second) continue;
            PathState& path = setup_path_states[path_id];
            const bool launch = changed.find(path.launch_sink) != changed.end();
            const bool capture = changed.find(path.capture_sink) != changed.end();
            const double delta = (capture ? delta_ss : 0.0) - (launch ? delta_ss : 0.0);
            if (std::abs(delta) < 1e-15) continue;
            removeSetupSlack(path.slack);
            path.slack += delta;
            addSetupSlack(path.slack);
        }
        for (int path_id : setup_capture_paths[sink]) {
            if (!seen_setup.insert(path_id).second) continue;
            PathState& path = setup_path_states[path_id];
            const bool launch = changed.find(path.launch_sink) != changed.end();
            const bool capture = changed.find(path.capture_sink) != changed.end();
            const double delta = (capture ? delta_ss : 0.0) - (launch ? delta_ss : 0.0);
            if (std::abs(delta) < 1e-15) continue;
            removeSetupSlack(path.slack);
            path.slack += delta;
            addSetupSlack(path.slack);
        }
        for (int path_id : hold_launch_paths[sink]) {
            if (!seen_hold.insert(path_id).second) continue;
            PathState& path = hold_path_states[path_id];
            const bool launch = changed.find(path.launch_sink) != changed.end();
            const bool capture = changed.find(path.capture_sink) != changed.end();
            const double delta = (launch ? delta_ff : 0.0) - (capture ? delta_ff : 0.0);
            if (std::abs(delta) < 1e-15) continue;
            removeHoldSlack(path.slack);
            path.slack += delta;
            addHoldSlack(path.slack);
        }
        for (int path_id : hold_capture_paths[sink]) {
            if (!seen_hold.insert(path_id).second) continue;
            PathState& path = hold_path_states[path_id];
            const bool launch = changed.find(path.launch_sink) != changed.end();
            const bool capture = changed.find(path.capture_sink) != changed.end();
            const double delta = (launch ? delta_ff : 0.0) - (capture ? delta_ff : 0.0);
            if (std::abs(delta) < 1e-15) continue;
            removeHoldSlack(path.slack);
            path.slack += delta;
            addHoldSlack(path.slack);
        }
    }
    clock_tree[node].type = new_type;
    current_metrics.area += delta_area;
    refreshWns();
    return true;
}

bool TimingEngine::applyMove(const TimingMove& move, TimingRollback& rollback) {
    std::vector<int> affected_sinks;
    double delta_ss = 0.0, delta_ff = 0.0, delta_area = 0.0;
    if (!moveDelta(move, affected_sinks, delta_ss, delta_ff, delta_area)) return false;

    rollback.previous_tree = clock_tree;
    rollback.valid = true;
    if (move.kind == TimingMoveKind::ResizeBuffer) {
        clock_tree[move.node].type = move.new_type;
    } else {
        if (move.chain_node_names.size() != move.chain_types.size()) {
            rollbackMove(rollback);
            return false;
        }
        for (const std::string& name : move.chain_node_names) {
            if (name.empty() || clock_tree.find(name) != clock_tree.end()) {
                rollbackMove(rollback);
                return false;
            }
        }
        ClockNode& parent = clock_tree[move.parent];
        auto child_pos = std::find(parent.children.begin(), parent.children.end(), move.child);
        if (child_pos == parent.children.end()) {
            rollbackMove(rollback);
            return false;
        }
        const int parent_level = parent.level;
        *child_pos = move.chain_node_names.front();
        for (std::size_t i = 0; i < move.chain_node_names.size(); ++i) {
            ClockNode node;
            node.name = move.chain_node_names[i];
            node.type = move.chain_types[i];
            node.parent = i == 0 ? move.parent : move.chain_node_names[i - 1];
            node.level = parent_level + 1 + static_cast<int>(i);
            node.children.push_back(i + 1 < move.chain_node_names.size()
                                    ? move.chain_node_names[i + 1] : move.child);
            clock_tree.emplace(node.name, std::move(node));
        }
        clock_tree[move.child].parent = move.chain_node_names.back();
        const int child_level = parent_level + 1 + static_cast<int>(move.chain_node_names.size());
        std::vector<std::pair<std::string, int> > stack{{move.child, child_level}};
        while (!stack.empty()) {
            const auto item = stack.back();
            stack.pop_back();
            ClockNode& node = clock_tree[item.first];
            node.level = item.second;
            for (const std::string& child : node.children) {
                stack.push_back({child, item.second + 1});
            }
        }
    }
    rebuild();
    return true;
}

bool TimingEngine::rollbackMove(TimingRollback& rollback) {
    if (!rollback.valid) return false;
    clock_tree.swap(rollback.previous_tree);
    rollback.previous_tree.clear();
    rollback.valid = false;
    rebuild();
    return true;
}

IncrementalEvalResult TimingEngine::evaluateMove(const TimingMove& move) {
    IncrementalEvalResult result;
    result.metrics = current_metrics;

    std::vector<int> affected_sinks;
    double delta_ss = 0.0;
    double delta_ff = 0.0;
    double delta_area = 0.0;
    if (!moveDelta(move, affected_sinks, delta_ss, delta_ff, delta_area)) {
        return result;
    }

    result.legal = true;
    result.affected_sink_count = static_cast<int>(affected_sinks.size());
    result.delta_ss = delta_ss;
    result.delta_ff = delta_ff;

    if (affected_sinks.empty() && std::abs(delta_area) < 1e-15) return result;

    std::unordered_set<int> changed_sinks;
    changed_sinks.reserve(affected_sinks.size() * 2 + 1);
    for (std::size_t i = 0; i < affected_sinks.size(); ++i) {
        changed_sinks.insert(affected_sinks[i]);
    }

    std::vector<int> setup_touched;
    std::vector<int> hold_touched;
    std::unordered_set<int> seen_setup;
    std::unordered_set<int> seen_hold;

    for (std::size_t i = 0; i < affected_sinks.size(); ++i) {
        int sink = affected_sinks[i];
        for (std::size_t j = 0; j < setup_launch_paths[sink].size(); ++j) {
            int path_id = setup_launch_paths[sink][j];
            if (seen_setup.insert(path_id).second) setup_touched.push_back(path_id);
        }
        for (std::size_t j = 0; j < setup_capture_paths[sink].size(); ++j) {
            int path_id = setup_capture_paths[sink][j];
            if (seen_setup.insert(path_id).second) setup_touched.push_back(path_id);
        }
        for (std::size_t j = 0; j < hold_launch_paths[sink].size(); ++j) {
            int path_id = hold_launch_paths[sink][j];
            if (seen_hold.insert(path_id).second) hold_touched.push_back(path_id);
        }
        for (std::size_t j = 0; j < hold_capture_paths[sink].size(); ++j) {
            int path_id = hold_capture_paths[sink][j];
            if (seen_hold.insert(path_id).second) hold_touched.push_back(path_id);
        }
    }

    std::vector<std::pair<int, double> > old_setup;
    std::vector<std::pair<int, double> > old_hold;
    old_setup.reserve(setup_touched.size());
    old_hold.reserve(hold_touched.size());

    for (std::size_t i = 0; i < setup_touched.size(); ++i) {
        int path_id = setup_touched[i];
        PathState& path = setup_path_states[path_id];
        bool launch_changed = changed_sinks.find(path.launch_sink) != changed_sinks.end();
        bool capture_changed = changed_sinks.find(path.capture_sink) != changed_sinks.end();
        double slack_delta = (capture_changed ? delta_ss : 0.0) -
                             (launch_changed ? delta_ss : 0.0);
        if (std::abs(slack_delta) < 1e-15) continue;

        old_setup.push_back(std::make_pair(path_id, path.slack));
        removeSetupSlack(path.slack);
        path.slack += slack_delta;
        addSetupSlack(path.slack);
    }

    for (std::size_t i = 0; i < hold_touched.size(); ++i) {
        int path_id = hold_touched[i];
        PathState& path = hold_path_states[path_id];
        bool launch_changed = changed_sinks.find(path.launch_sink) != changed_sinks.end();
        bool capture_changed = changed_sinks.find(path.capture_sink) != changed_sinks.end();
        double slack_delta = (launch_changed ? delta_ff : 0.0) -
                             (capture_changed ? delta_ff : 0.0);
        if (std::abs(slack_delta) < 1e-15) continue;

        old_hold.push_back(std::make_pair(path_id, path.slack));
        removeHoldSlack(path.slack);
        path.slack += slack_delta;
        addHoldSlack(path.slack);
    }

    current_metrics.area += delta_area;
    refreshWns();
    result.metrics = current_metrics;
    result.affected_path_count = static_cast<int>(old_setup.size() + old_hold.size());
    result.affected_setup_path_count = static_cast<int>(old_setup.size());
    result.affected_hold_path_count = static_cast<int>(old_hold.size());

    for (std::size_t i = old_setup.size(); i > 0; --i) {
        int path_id = old_setup[i - 1].first;
        removeSetupSlack(setup_path_states[path_id].slack);
        setup_path_states[path_id].slack = old_setup[i - 1].second;
        addSetupSlack(setup_path_states[path_id].slack);
    }
    for (std::size_t i = old_hold.size(); i > 0; --i) {
        int path_id = old_hold[i - 1].first;
        removeHoldSlack(hold_path_states[path_id].slack);
        hold_path_states[path_id].slack = old_hold[i - 1].second;
        addHoldSlack(hold_path_states[path_id].slack);
    }
    current_metrics.area -= delta_area;
    refreshWns();

    return result;
}
