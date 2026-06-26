#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#pragma once
#include "parser.h"
#include "timing_engine.h"
#include <unordered_map>
#include <vector>
#include <string>

enum class OptimizationMode {
    TimingAggressive,
    DownsizeForDelay,
    HoldRepair,
    BalancedCleanup
};

enum class CandidateMoveKind {
    InsertLeaf,
    ResizeBuffer,
    InsertInternal
};

enum class CandidateDirection {
    CaptureSide,
    LaunchSide,
    Neutral
};

struct CandidateMove {
    CandidateMoveKind kind = CandidateMoveKind::ResizeBuffer;
    std::string parent;
    std::string child;
    std::string node;
    std::string first_type;
    std::string second_type;
    std::string third_type;
    std::vector<std::string> chain_types;
    int chain_len = 0;
    double estimate = 0.0;
    CandidateDirection direction = CandidateDirection::Neutral;
    std::string old_type;
};

struct MoveEvaluation {
    bool legal = false;
    CandidateMove move;
    TimingMetrics metrics;
    ScoreTerms score_terms;
    int affected_sinks = 0;
    int affected_setup_paths = 0;
    int affected_hold_paths = 0;
};

struct ModeResult {
    std::string name;
    TimingMetrics metrics;
    std::unordered_map<std::string, ClockNode> tree;
    OptimizationMode mode = OptimizationMode::BalancedCleanup;
    ScoreTerms score_terms;
    double runtime_sec = 0.0;
};

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
