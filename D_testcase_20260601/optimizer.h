#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#pragma once
#include "parser.h"
#include "timing_engine.h"
#include <unordered_map>
#include <vector>
#include <string>

class Optimizer {
private:
    std::string root_name;
    std::unordered_map<std::string, ClockNode> clock_tree;
    std::unordered_map<std::string, BufferCell> buf_lib;
    std::vector<DataPath> ss_paths;
    std::vector<DataPath> ff_paths;
    double clock_period;

    std::string small_buffer;
    std::string large_buffer;

    double computeScore(double tns_ss, double wns_ss, double tns_ff, double wns_ff, double area,
                        double ori_tns_ss, double ori_wns_ss, double ori_tns_ff, double ori_wns_ff, double ori_area);

public:
    Optimizer(const std::string& r_name,
              const std::unordered_map<std::string, ClockNode>& tree,
              const std::unordered_map<std::string, BufferCell>& lib,
              const std::vector<DataPath>& ss_p,
              const std::vector<DataPath>& ff_p,
              double period);

    void runOptimization(std::unordered_map<std::string, ClockNode>& best_tree);
};

#endif // OPTIMIZER_H