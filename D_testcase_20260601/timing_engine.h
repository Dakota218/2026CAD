#ifndef TIMING_ENGINE_H
#define TIMING_ENGINE_H

#pragma once
#include "parser.h"
#include <unordered_map>
#include <vector>
#include <string>

class TimingEngine {
private:
    std::string root_name;
    std::unordered_map<std::string, ClockNode> clock_tree;
    std::unordered_map<std::string, BufferCell> buf_lib;
    
    std::vector<DataPath> ss_paths;
    std::vector<DataPath> ff_paths;
    double clock_period;

    std::unordered_map<std::string, double> ss_delays;
    std::unordered_map<std::string, double> ff_delays;

    void computeTreeDelays();
    void computeDfs(const std::string& node_name, double current_ss_delay, double current_ff_delay);

public:
    TimingEngine(const std::string& r_name,
                 const std::unordered_map<std::string, ClockNode>& tree,
                 const std::unordered_map<std::string, BufferCell>& lib,
                 const std::vector<DataPath>& ss_p,
                 const std::vector<DataPath>& ff_p,
                 double period);

    void updateTiming(const std::unordered_map<std::string, ClockNode>& new_tree);
    void evaluateMetrics(double& tns_ss, double& wns_ss, double& tns_ff, double& wns_ff, double& total_area) const;
};

#endif // TIMING_ENGINE_H