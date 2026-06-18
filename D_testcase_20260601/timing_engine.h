#ifndef TIMING_ENGINE_H
#define TIMING_ENGINE_H

#pragma once

#include "parser.h"
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct TimingMetrics {
    double tns_ss = 0.0;
    double wns_ss = 0.0;
    int nvp_ss = 0;
    double tns_ff = 0.0;
    double wns_ff = 0.0;
    int nvp_ff = 0;
    double area = 0.0;
};

enum class TimingMoveKind {
    ResizeBuffer,
    InsertChain
};

struct TimingMove {
    TimingMoveKind kind = TimingMoveKind::ResizeBuffer;
    std::string node;
    std::string new_type;
    std::string parent;
    std::string child;
    std::vector<std::string> chain_types;
};

struct IncrementalEvalResult {
    bool legal = false;
    TimingMetrics metrics;
    int affected_sink_count = 0;
    int affected_path_count = 0;
    double delta_ss = 0.0;
    double delta_ff = 0.0;
};

class TimingEngine {
private:
    struct PathState {
        std::string name;
        int launch_sink = -1;
        int capture_sink = -1;
        double data_delay = 0.0;
        double slack = 0.0;
    };

    std::string root_name;
    std::unordered_map<std::string, ClockNode> clock_tree;
    std::unordered_map<std::string, BufferCell> buf_lib;
    std::vector<DataPath> ss_paths;
    std::vector<DataPath> ff_paths;
    double clock_period;
    double setup_time = 0.0;
    double hold_time = 0.0;

    std::unordered_map<std::string, double> ss_delays;
    std::unordered_map<std::string, double> ff_delays;

    std::vector<std::string> sink_names;
    std::unordered_map<std::string, int> sink_id;
    std::vector<double> sink_ss_delay;
    std::vector<double> sink_ff_delay;
    std::unordered_map<std::string, std::vector<int> > node_descendant_sinks;

    std::vector<PathState> setup_path_states;
    std::vector<PathState> hold_path_states;
    std::vector<std::vector<int> > setup_launch_paths;
    std::vector<std::vector<int> > setup_capture_paths;
    std::vector<std::vector<int> > hold_launch_paths;
    std::vector<std::vector<int> > hold_capture_paths;

    TimingMetrics current_metrics;
    std::multiset<double> setup_negative_slacks;
    std::multiset<double> hold_negative_slacks;

    void rebuild();
    void computeTreeDelays();
    void computeDfs(const std::string& node_name, double current_ss_delay, double current_ff_delay);
    void collectDescendantSinks(const std::string& node_name, std::vector<int>& sinks);
    const std::vector<int>& buildDescendantSinkMapDfs(const std::string& node_name);
    void buildSinkMaps();
    void buildPathStates();
    void computeArea();

    double nodeDelay(const std::string& type, int fanout, bool ss_corner) const;
    double cellArea(const std::string& type) const;
    bool fanoutLegal(const std::string& type, int fanout) const;
    bool moveDelta(const TimingMove& move,
                   std::vector<int>& affected_sinks,
                   double& delta_ss,
                   double& delta_ff,
                   double& delta_area) const;

    void removeSetupSlack(double slack);
    void addSetupSlack(double slack);
    void removeHoldSlack(double slack);
    void addHoldSlack(double slack);
    void refreshWns();

public:
    TimingEngine(const std::string& r_name,
                 const std::unordered_map<std::string, ClockNode>& tree,
                 const std::unordered_map<std::string, BufferCell>& lib,
                 const std::vector<DataPath>& ss_p,
                 const std::vector<DataPath>& ff_p,
                 double period);

    void updateTiming(const std::unordered_map<std::string, ClockNode>& new_tree);
    void evaluateMetrics(double& tns_ss, double& wns_ss, double& tns_ff, double& wns_ff, double& total_area) const;

    const TimingMetrics& metrics() const;
    IncrementalEvalResult evaluateMove(const TimingMove& move);
};

#endif // TIMING_ENGINE_H
