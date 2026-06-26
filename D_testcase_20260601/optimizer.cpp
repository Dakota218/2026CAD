#include "optimizer.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <array>

namespace {
const double kOptimizationTimeLimitSec = 89.0;
const std::size_t kTnsCleanupSetupPaths = 5000;
const std::size_t kTnsCleanupSinkLimit = 600;
const std::size_t kMaxCandidatesPerIteration = 4000;
const int kMaxNoImproveRounds = 2;
const double kScoreEps = 1e-12;

enum class PhaseKind {
    WorstPathSurgery,
    WnsRepair,
    UndoHarmfulDownsize,
    TnsCleanup,
    AggressiveSetup,
    DownsizeForDelay,
    HoldRepair,
    GroupInsertion,
    AreaRecovery
};

using MoveKind = CandidateMoveKind;

struct PhaseConfig {
    PhaseKind kind;
    const char* name;
    double start_sec;
    double end_sec;
    std::size_t exact_limit;
    int max_no_improve_rounds;
};

struct DelayTable {
    std::unordered_map<std::string, double> ss;
    std::unordered_map<std::string, double> ff;
};

struct PathViolation {
    std::string name;
    std::string launch;
    std::string capture;
    double delay = 0.0;
    double skew = 0.0;
    double slack = 0.0;
};

struct SinkStat {
    std::string name;
    int count = 0;
    int launch_count = 0;
    double total_neg_slack = 0.0;
    double launch_neg_slack = 0.0;
    double worst_slack = 0.0;
};

using Metrics = TimingMetrics;

std::vector<PhaseConfig> buildAdaptivePhases(const Metrics& original,
                                             std::size_t node_count,
                                             std::size_t setup_path_count) {
    (void)node_count;
    (void)setup_path_count;
    return {
        {PhaseKind::DownsizeForDelay, "setup_WNS_breakthrough directional_seed", 0.0, 18.0, 2048, 1},
        {PhaseKind::WorstPathSurgery, "setup_worst_path_surgery", 18.0, 83.0, 1200, 2},
        {PhaseKind::TnsCleanup, "setup_TNS_cleanup", 83.0, 83.1, 128, 1},
        {PhaseKind::HoldRepair, "strong_hold_repair", 83.1, 89.0, 1536, 2},
        {PhaseKind::GroupInsertion, "balanced_area_cleanup", 88.0, 89.0, 512, 1}
    };
}

struct TimingReport {
    Metrics metrics;
    std::vector<PathViolation> ss_violations;
    std::vector<PathViolation> ff_violations;
};

struct RobustScore {
    double ss_tns = 0.0;
    double ss_wns = 0.0;
    double ff_tns = 0.0;
    double ff_wns = 0.0;
    double setup = 0.0;
    double hold = 0.0;
    double area = 0.0;
    std::array<double, 5> robust_scores{};
    double average_robust = 0.0;
    double min_robust = 0.0;
    double timing_score = 0.0;
    double robust = 0.0; // Composite normalized-term objective.
};

using Move = CandidateMove;

struct ChainCandidate {
    std::string first_type;
    std::string second_type;
    std::string third_type;
    std::vector<std::string> types;
    int chain_len = 0;
    double ss_delay = 0.0;
    double ff_delay = 0.0;
    double area = 0.0;
};

struct TargetSkewGuide {
    std::unordered_map<std::string, double> sink_target;
    std::unordered_map<std::string, double> node_target;
    std::unordered_map<std::string, double> edge_target;
    std::size_t positive_sinks = 0;
    std::size_t negative_sinks = 0;
};

struct CandidateStats {
    std::size_t total = 0;
    std::size_t resize_speedup = 0;
    std::size_t resize_delay = 0;
    std::size_t resize_area = 0;
    std::vector<std::size_t> chain_len_count;
};

TimingMove toTimingMove(const Move& move) {
    TimingMove timing_move;
    if (move.kind == MoveKind::ResizeBuffer) {
        timing_move.kind = TimingMoveKind::ResizeBuffer;
        timing_move.node = move.node;
        timing_move.new_type = move.first_type;
        return timing_move;
    }

    timing_move.kind = TimingMoveKind::InsertChain;
    timing_move.parent = move.parent;
    timing_move.child = move.child;
    if (!move.chain_types.empty()) {
        timing_move.chain_types = move.chain_types;
    } else {
        if (move.chain_len >= 1) timing_move.chain_types.push_back(move.first_type);
        if (move.chain_len >= 2) timing_move.chain_types.push_back(move.second_type);
        if (move.chain_len >= 3) timing_move.chain_types.push_back(move.third_type);
    }
    return timing_move;
}

MoveEvaluation evaluateCandidateMove(TimingEngine& engine,
                                     const Move& move,
                                     const Metrics& original) {
    MoveEvaluation result;
    result.move = move;
    IncrementalEvalResult timing;
    if (move.kind == MoveKind::ResizeBuffer) {
        timing = engine.evaluateResizeMove(move.node, move.first_type);
    } else {
        const TimingMove timing_move = toTimingMove(move);
        timing = engine.evaluateInsertChainMove(timing_move.parent,
                                                timing_move.child,
                                                timing_move.chain_types);
    }
    result.legal = timing.legal;
    result.metrics = timing.metrics;
    result.score_terms = TimingEngine::computeScoreTerms(timing.metrics, original);
    result.affected_sinks = timing.affected_sink_count;
    result.affected_setup_paths = timing.affected_setup_path_count;
    result.affected_hold_paths = timing.affected_hold_path_count;
    return result;
}

struct SearchResult {
    bool found = false;
    Move first_move;
    Metrics metrics;
    std::unordered_map<std::string, ClockNode> tree;
    std::size_t evaluated = 0;
    int next_new_buf_id = 0;
    int sequence_len = 0;
    int resize_count = 0;
    std::size_t rejected_area_hurt_timing = 0;
    std::size_t rejected_hold_collapse = 0;
    std::size_t rejected_score = 0;
    std::size_t rejected_illegal = 0;
};

using ModeState = ModeResult;

struct OptimizationLogger {
    std::ofstream out;

    OptimizationLogger() : out("optimization_journal.md", std::ios::out) {
        if (out) {
            out << "# Optimization log\n\n";
            out << "Priority order: setup timing first, then hold timing, then area.\n\n";
            out << "Architecture: shared CandidateMove/ModeResult types; TimingEngine-owned "
                   "normalized scoring, affected-path evaluation, and transactional rollback.\n\n";
        }
    }

    void line(const std::string& text) {
        if (out) out << text << "\n";
    }
};

double elapsedSec(const std::chrono::steady_clock::time_point& start_time) {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_time).count();
}

bool isTimeLimitReached(const std::chrono::steady_clock::time_point& start_time) {
    return elapsedSec(start_time) >= kOptimizationTimeLimitSec;
}

const char* phaseName(PhaseKind phase) {
    switch (phase) {
        case PhaseKind::WorstPathSurgery: return "worst setup path surgery";
        case PhaseKind::WnsRepair: return "WNS repair";
        case PhaseKind::UndoHarmfulDownsize: return "undo harmful launch downsize";
        case PhaseKind::TnsCleanup: return "TNS/NVP cleanup";
        case PhaseKind::GroupInsertion: return "group/ancestor insertion";
        case PhaseKind::AggressiveSetup: return "aggressive setup";
        case PhaseKind::DownsizeForDelay: return "downsize for useful delay";
        case PhaseKind::HoldRepair: return "hold final repair";
        case PhaseKind::AreaRecovery: return "area recovery";
    }
    return "unknown";
}

std::size_t dynamicTopSetupPaths(int nvp_ss) {
    return static_cast<std::size_t>(std::min(10000, std::max(1000, nvp_ss / 5)));
}

std::size_t dynamicWnsSetupPaths(int nvp_ss) {
    return static_cast<std::size_t>(std::min(1000, std::max(100, nvp_ss / 40)));
}

std::size_t dynamicTopHoldPaths(int nvp_ff) {
    return static_cast<std::size_t>(std::min(2000, std::max(200, nvp_ff * 10)));
}

double nodeDelay(const std::unordered_map<std::string, BufferCell>& buf_lib,
                 const std::string& type,
                 int fanout,
                 bool ss_corner) {
    if (type == "ROOT" || fanout <= 0) return 0.0;
    auto it = buf_lib.find(type);
    if (it == buf_lib.end()) return 0.0;
    const BufferCell& cell = it->second;
    int idx = std::min(fanout, cell.max_fanout) - 1;
    if (idx < 0) return 0.0;
    return ss_corner ? cell.ss_delays[idx] : cell.ff_delays[idx];
}

double cellArea(const std::unordered_map<std::string, BufferCell>& buf_lib,
                const std::string& type) {
    auto it = buf_lib.find(type);
    return it == buf_lib.end() ? 0.0 : it->second.area;
}

void computeDelayDfs(const std::string& node_name,
                     double ss_delay,
                     double ff_delay,
                     const std::unordered_map<std::string, ClockNode>& tree,
                     const std::unordered_map<std::string, BufferCell>& buf_lib,
                     DelayTable& delays) {
    delays.ss[node_name] = ss_delay;
    delays.ff[node_name] = ff_delay;

    const ClockNode& node = tree.at(node_name);
    int fanout = static_cast<int>(node.children.size());
    double next_ss = ss_delay + nodeDelay(buf_lib, node.type, fanout, true);
    double next_ff = ff_delay + nodeDelay(buf_lib, node.type, fanout, false);

    for (const auto& child : node.children) {
        computeDelayDfs(child, next_ss, next_ff, tree, buf_lib, delays);
    }
}

DelayTable computeDelays(const std::string& root_name,
                         const std::unordered_map<std::string, ClockNode>& tree,
                         const std::unordered_map<std::string, BufferCell>& buf_lib) {
    DelayTable delays;
    computeDelayDfs(root_name, 0.0, 0.0, tree, buf_lib, delays);
    return delays;
}

void updateSubtreeLevels(std::unordered_map<std::string, ClockNode>& tree,
                         const std::string& node_name,
                         int new_level) {
    ClockNode& node = tree[node_name];
    node.level = new_level;
    for (const auto& child : node.children) {
        updateSubtreeLevels(tree, child, new_level + 1);
    }
}

bool checkFanoutLegal(const std::unordered_map<std::string, ClockNode>& tree,
                      const std::unordered_map<std::string, BufferCell>& buf_lib) {
    for (const auto& pair : tree) {
        const ClockNode& node = pair.second;
        if (node.type == "ROOT" || node.is_sink) continue;
        auto cell_it = buf_lib.find(node.type);
        if (cell_it == buf_lib.end()) return false;
        if (static_cast<int>(node.children.size()) > cell_it->second.max_fanout) return false;
    }
    return true;
}

std::string getNextNewBufferName(const std::unordered_map<std::string, ClockNode>& tree,
                                 int& next_new_buf_id) {
    std::string name;
    do {
        name = "NEW_BUF_" + std::to_string(next_new_buf_id++);
    } while (tree.find(name) != tree.end());
    return name;
}

int findNextNewBufferId(const std::unordered_map<std::string, ClockNode>& tree) {
    int next_id = 0;
    for (const auto& pair : tree) {
        const std::string prefix = "NEW_BUF_";
        if (pair.first.find(prefix) != 0) continue;
        try {
            int id = std::stoi(pair.first.substr(prefix.size()));
            next_id = std::max(next_id, id + 1);
        } catch (...) {
        }
    }
    return next_id;
}

bool replaceChild(std::unordered_map<std::string, ClockNode>& tree,
                  const std::string& parent,
                  const std::string& old_child,
                  const std::string& new_child) {
    auto& children = tree[parent].children;
    auto it = std::find(children.begin(), children.end(), old_child);
    if (it == children.end()) return false;
    *it = new_child;
    return true;
}

std::vector<std::string> sortedCellNames(const std::unordered_map<std::string, BufferCell>& buf_lib) {
    std::vector<std::string> cells;
    cells.reserve(buf_lib.size());
    for (const auto& pair : buf_lib) cells.push_back(pair.first);
    std::sort(cells.begin(), cells.end(), [&buf_lib](const std::string& a, const std::string& b) {
        const BufferCell& ca = buf_lib.at(a);
        const BufferCell& cb = buf_lib.at(b);
        if (ca.area != cb.area) return ca.area < cb.area;
        return a < b;
    });
    return cells;
}

TimingReport analyzeTiming(const std::string& root_name,
                           const std::unordered_map<std::string, ClockNode>& tree,
                           const std::unordered_map<std::string, BufferCell>& buf_lib,
                           const std::vector<DataPath>& ss_paths,
                           const std::vector<DataPath>& ff_paths,
                           double clock_period) {
    TimingReport report;
    DelayTable delays = computeDelays(root_name, tree, buf_lib);
    const double setup_time = 0.08 * clock_period;
    const double hold_time = 0.05 * clock_period;

    for (const auto& path : ss_paths) {
        double skew = delays.ss[path.capture_ff] - delays.ss[path.launch_ff];
        double slack = clock_period - setup_time - path.delay + skew;
        if (slack < 0.0) {
            report.metrics.tns_ss += slack;
            report.metrics.wns_ss = std::min(report.metrics.wns_ss, slack);
            ++report.metrics.nvp_ss;
            PathViolation violation;
            violation.name = path.name;
            violation.launch = path.launch_ff;
            violation.capture = path.capture_ff;
            violation.delay = path.delay;
            violation.skew = skew;
            violation.slack = slack;
            report.ss_violations.push_back(violation);
        }
    }

    for (const auto& path : ff_paths) {
        double skew = delays.ff[path.capture_ff] - delays.ff[path.launch_ff];
        double slack = path.delay - hold_time - skew;
        if (slack < 0.0) {
            report.metrics.tns_ff += slack;
            report.metrics.wns_ff = std::min(report.metrics.wns_ff, slack);
            ++report.metrics.nvp_ff;
            PathViolation violation;
            violation.name = path.name;
            violation.launch = path.launch_ff;
            violation.capture = path.capture_ff;
            violation.delay = path.delay;
            violation.skew = skew;
            violation.slack = slack;
            report.ff_violations.push_back(violation);
        }
    }

    for (const auto& pair : tree) {
        const ClockNode& node = pair.second;
        if (node.type != "ROOT" && !node.is_sink) {
            report.metrics.area += cellArea(buf_lib, node.type);
        }
    }

    auto bySlack = [](const PathViolation& a, const PathViolation& b) {
        return a.slack < b.slack;
    };
    std::sort(report.ss_violations.begin(), report.ss_violations.end(), bySlack);
    std::sort(report.ff_violations.begin(), report.ff_violations.end(), bySlack);
    return report;
}

std::vector<std::string> clockPathToSink(
        const std::unordered_map<std::string, ClockNode>& tree,
        const std::string& sink) {
    std::vector<std::string> path;
    std::string node = sink;
    std::unordered_set<std::string> visited;
    while (!node.empty() && visited.insert(node).second) {
        auto it = tree.find(node);
        if (it == tree.end()) break;
        if (!it->second.is_sink && it->second.type != "ROOT") path.push_back(node);
        node = it->second.parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::string pathText(const std::vector<std::string>& path) {
    std::ostringstream out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i) out << "->";
        out << path[i];
    }
    return out.str();
}

void printTopViolations(const TimingReport& report,
                        const std::unordered_map<std::string, ClockNode>& tree) {
    std::size_t top_setup = std::min(dynamicTopSetupPaths(report.metrics.nvp_ss), report.ss_violations.size());
    std::size_t top_hold = std::min(dynamicTopHoldPaths(report.metrics.nvp_ff), report.ff_violations.size());
    const std::size_t ss_count = std::min<std::size_t>(top_setup, 20);
    const std::size_t ff_count = std::min<std::size_t>(top_hold, 5);
    std::cout << "[VIOLATION] Inspecting " << top_setup
              << " SS paths; printing worst " << ss_count << ":\n";
    for (std::size_t i = 0; i < ss_count; ++i) {
        const auto& v = report.ss_violations[i];
        const std::vector<std::string> launch_path = clockPathToSink(tree, v.launch);
        const std::vector<std::string> capture_path = clockPathToSink(tree, v.capture);
        std::string common = "ROOT";
        const std::size_t shared = std::min(launch_path.size(), capture_path.size());
        for (std::size_t j = 0; j < shared && launch_path[j] == capture_path[j]; ++j) {
            common = launch_path[j];
        }
        std::cout << "  SS#" << i
                  << " slack=" << v.slack
                  << " launch=" << v.launch
                  << " capture=" << v.capture
                  << " data=" << v.delay
                  << " skew=" << v.skew
                  << " common=" << common << "\n"
                  << "    launch_clock=" << pathText(launch_path) << "\n"
                  << "    capture_clock=" << pathText(capture_path) << "\n";
    }

    std::cout << "[VIOLATION] Inspecting " << top_hold
              << " FF paths; printing worst " << ff_count << ":\n";
    for (std::size_t i = 0; i < ff_count; ++i) {
        const auto& v = report.ff_violations[i];
        std::cout << "  FF#" << i
                  << " slack=" << v.slack
                  << " launch=" << v.launch
                  << " capture=" << v.capture
                  << " data=" << v.delay
                  << " skew=" << v.skew << "\n";
    }
}

bool validTimingDenominator(double original) {
    return original < -1e-12;
}

double weakestScoreTerm(const RobustScore& score, const Metrics& original) {
    double weakest = std::numeric_limits<double>::infinity();
    if (validTimingDenominator(original.tns_ss)) weakest = std::min(weakest, score.ss_tns);
    if (validTimingDenominator(original.wns_ss)) weakest = std::min(weakest, score.ss_wns);
    if (validTimingDenominator(original.tns_ff)) weakest = std::min(weakest, score.ff_tns);
    if (validTimingDenominator(original.wns_ff)) weakest = std::min(weakest, score.ff_wns);
    const bool timing_strong = score.ss_tns >= 0.90 && score.ss_wns >= 0.90 &&
                               score.ff_tns >= 0.90 && score.ff_wns >= 0.90;
    if (timing_strong && original.area > 1e-12) weakest = std::min(weakest, score.area);
    return std::isfinite(weakest) ? weakest : 0.0;
}

std::string weakestScoreTermName(const RobustScore& score, const Metrics& original) {
    std::string name = "N/A";
    double weakest = std::numeric_limits<double>::infinity();
    auto consider = [&](const char* candidate_name, double value, bool valid) {
        if (valid && value < weakest) {
            weakest = value;
            name = candidate_name;
        }
    };
    consider("SS_TNS_term", score.ss_tns, validTimingDenominator(original.tns_ss));
    consider("SS_WNS_term", score.ss_wns, validTimingDenominator(original.wns_ss));
    consider("FF_TNS_term", score.ff_tns, validTimingDenominator(original.tns_ff));
    consider("FF_WNS_term", score.ff_wns, validTimingDenominator(original.wns_ff));
    const bool timing_strong = score.ss_tns >= 0.90 && score.ss_wns >= 0.90 &&
                               score.ff_tns >= 0.90 && score.ff_wns >= 0.90;
    consider("AREA_term", score.area, timing_strong && original.area > 1e-12);
    return name;
}

RobustScore evaluateRobustScore(const Metrics& m, const Metrics& original) {
    static const double alphas[] = {0.50, 0.60, 0.70, 0.80, 0.90};

    RobustScore score;
    const ScoreTerms normalized = TimingEngine::computeScoreTerms(m, original);
    score.ss_tns = normalized.ss_tns;
    score.ss_wns = normalized.ss_wns;
    score.ff_tns = normalized.ff_tns;
    score.ff_wns = normalized.ff_wns;
    score.setup = score.ss_tns + score.ss_wns;
    score.hold = score.ff_tns + score.ff_wns;
    score.area = normalized.area;
    score.min_robust = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < score.robust_scores.size(); ++i) {
        double alpha = alphas[i];
        double beta = (1.0 - alpha) / 2.0;
        double gamma = beta;
        double weighted = alpha * score.setup +
                          beta * score.hold +
                          gamma * score.area;
        score.robust_scores[i] = weighted;
        score.average_robust += weighted;
        score.min_robust = std::min(score.min_robust, weighted);
    }
    score.average_robust /= static_cast<double>(score.robust_scores.size());
    if (!std::isfinite(score.min_robust)) score.min_robust = 0.0;
    // Additive by design: AREA is a small penalty/tie-breaker, not a term
    // that can veto large timing progress merely because it is near zero.
    score.timing_score = normalized.timing_score;
    score.robust = normalized.robust_score;

    return score;
}

double setupWnsFocusedScore(const RobustScore& score) {
    return 8.0 * score.ss_wns +
           4.0 * score.ss_tns +
           0.8 * score.ff_wns +
           0.5 * score.ff_tns +
           0.3 * score.area;
}

double worstPathSetupScore(const RobustScore& score) {
    return 10.0 * score.ss_wns +
           3.0 * score.ss_tns +
           0.8 * score.ff_wns +
           0.5 * score.ff_tns +
           0.2 * score.area;
}

double finalSelectionScore(const RobustScore& score) {
    return 5.0 * score.ss_wns +
           4.0 * score.ss_tns +
           2.0 * score.ff_wns +
           1.5 * score.ff_tns +
           0.5 * score.area;
}

std::string bottleneckName(const RobustScore& score, const Metrics& original) {
    if ((validTimingDenominator(original.tns_ss) && score.ss_tns < 0.90) ||
        (validTimingDenominator(original.wns_ss) && score.ss_wns < 0.90)) return "setup repair";
    if ((validTimingDenominator(original.tns_ff) && score.ff_tns < 0.90) ||
        (validTimingDenominator(original.wns_ff) && score.ff_wns < 0.90)) return "hold repair";
    if (original.area > 1e-12 && score.area < 0.30) return "area recovery";
    return "balanced cleanup";
}

std::string scoreTermsText(const Metrics& metrics, const Metrics& original) {
    RobustScore score = evaluateRobustScore(metrics, original);
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "SS_TNS_term=" << (validTimingDenominator(original.tns_ss) ? std::to_string(score.ss_tns) : "N/A")
        << " SS_WNS_term=" << (validTimingDenominator(original.wns_ss) ? std::to_string(score.ss_wns) : "N/A")
        << " FF_TNS_term=" << (validTimingDenominator(original.tns_ff) ? std::to_string(score.ff_tns) : "N/A")
        << " FF_WNS_term=" << (validTimingDenominator(original.wns_ff) ? std::to_string(score.ff_wns) : "N/A")
        << " AREA_term=" << (original.area > 1e-12 ? std::to_string(score.area) : "N/A")
        << " setup_sum=" << score.setup
        << " hold_sum=" << score.hold
        << " weakest_term=" << weakestScoreTerm(score, original)
        << " bottleneck=" << bottleneckName(score, original)
        << " robust[0.50,0.60,0.70,0.80,0.90]=";
    for (std::size_t i = 0; i < score.robust_scores.size(); ++i) {
        if (i) out << ",";
        out << score.robust_scores[i];
    }
    out << " average_robust=" << score.average_robust
        << " min_robust=" << score.min_robust
        << " robust_score=" << score.robust
        << " timing_score=" << score.timing_score
        << " weakest=" << weakestScoreTermName(score, original);
    return out.str();
}

void reportScoreTerms(const std::string& label,
                      const Metrics& metrics,
                      const Metrics& original,
                      OptimizationLogger& log) {
    const std::string text = scoreTermsText(metrics, original);
    std::cout << "[SCORE TERMS] " << label << " | " << text << "\n";
    log.line("- Score terms " + label + ": " + text);
}

double guidedWeakestDelta(const RobustScore& cand,
                          const RobustScore& base,
                          const Metrics& original);

std::string moveDescription(const Move& move) {
    std::ostringstream out;
    if (move.kind == MoveKind::ResizeBuffer) {
        out << "resize node=" << move.node << " "
            << (move.old_type.empty() ? "?" : move.old_type) << "->" << move.first_type;
    } else {
        out << (move.kind == MoveKind::InsertLeaf ? "insert-leaf" : "insert-internal")
            << " edge=" << move.parent << "->" << move.child;
    }
    return out.str();
}

const char* directionName(CandidateDirection direction) {
    switch (direction) {
        case CandidateDirection::CaptureSide: return "capture-side";
        case CandidateDirection::LaunchSide: return "launch-side";
        case CandidateDirection::Neutral: return "neutral";
    }
    return "neutral";
}

void reportAcceptedMove(const Move& move,
                        const Metrics& before,
                        const Metrics& after,
                        const Metrics& original,
                        OptimizationLogger& log) {
    RobustScore old_score = evaluateRobustScore(before, original);
    RobustScore new_score = evaluateRobustScore(after, original);
    std::ostringstream delta;
    delta << std::fixed << std::setprecision(6)
          << "dSS_TNS=" << new_score.ss_tns - old_score.ss_tns
          << " dSS_WNS=" << new_score.ss_wns - old_score.ss_wns
          << " dFF_TNS=" << new_score.ff_tns - old_score.ff_tns
          << " dFF_WNS=" << new_score.ff_wns - old_score.ff_wns
          << " dAREA=" << new_score.area - old_score.area
          << " dROBUST=" << new_score.robust - old_score.robust
          << " dTIMING=" << new_score.timing_score - old_score.timing_score;
    const bool weakest_improved = weakestScoreTerm(new_score, original) >
                                  weakestScoreTerm(old_score, original) + kScoreEps;
    const bool guided_improved = guidedWeakestDelta(new_score, old_score, original) > kScoreEps;
    const std::string reason = weakest_improved
        ? "weakest normalized term improved"
        : (guided_improved ? "phase-guiding weakest timing term improved"
                           : "timing score improved with protected terms");
    std::cout << "[ACCEPTED] " << moveDescription(move)
              << " | " << directionName(move.direction)
              << " | dSS_WNS=" << new_score.ss_wns - old_score.ss_wns
              << " dSS_TNS=" << new_score.ss_tns - old_score.ss_tns
              << " dFF_WNS=" << new_score.ff_wns - old_score.ff_wns
              << " dAREA=" << new_score.area - old_score.area
              << " | " << reason << "\n";
    log.line("- Accepted move " + moveDescription(move) +
             "; direction " + directionName(move.direction) +
             "; old " + scoreTermsText(before, original) +
             "; new " + scoreTermsText(after, original) +
             "; delta " + delta.str() +
             "; reason: " + reason + ".");
}

bool normalizedDamageIsSafe(const RobustScore& cand,
                            const RobustScore& base,
                            const Metrics& original) {
    const double max_drop = 0.05;
    if (validTimingDenominator(original.tns_ss) && cand.ss_tns < base.ss_tns - max_drop) return false;
    if (validTimingDenominator(original.wns_ss) && cand.ss_wns < base.ss_wns - max_drop) return false;
    if (validTimingDenominator(original.tns_ff) && cand.ff_tns < base.ff_tns - max_drop) return false;
    if (validTimingDenominator(original.wns_ff) && cand.ff_wns < base.ff_wns - max_drop) return false;
    if (original.area > 1e-12 && cand.area < base.area - max_drop) return false;
    return true;
}

double guidedWeakestDelta(const RobustScore& cand,
                          const RobustScore& base,
                          const Metrics& original) {
    struct Term { double candidate; double current; bool valid; };
    std::array<Term, 5> terms{{
        {cand.ss_tns, base.ss_tns, validTimingDenominator(original.tns_ss)},
        {cand.ss_wns, base.ss_wns, validTimingDenominator(original.wns_ss)},
        {cand.ff_tns, base.ff_tns, validTimingDenominator(original.tns_ff)},
        {cand.ff_wns, base.ff_wns, validTimingDenominator(original.wns_ff)},
        {cand.area, base.area, original.area > 1e-12}
    }};

    // A tiny area increase must not hijack every timing phase.  Area becomes
    // the guide when it is materially negative, or timing is nearly solved.
    const bool timing_nearly_solved = base.ss_tns > 0.90 && base.ss_wns > 0.90 &&
                                      base.ff_tns > 0.90 && base.ff_wns > 0.90;
    if (!timing_nearly_solved && base.area > -0.02) terms[4].valid = false;

    double weakest = std::numeric_limits<double>::infinity();
    for (const Term& term : terms) if (term.valid) weakest = std::min(weakest, term.current);
    if (!std::isfinite(weakest)) return 0.0;

    double delta_sum = 0.0;
    int tied = 0;
    for (const Term& term : terms) {
        if (term.valid && std::abs(term.current - weakest) <= 1e-9) {
            delta_sum += term.candidate - term.current;
            ++tied;
        }
    }
    return tied ? delta_sum / tied : 0.0;
}

double candidateImprovementScore(const RobustScore& cand,
                                 const RobustScore& base,
                                 const Metrics& original) {
    (void)original;
    return (cand.timing_score - base.timing_score) +
           0.50 * (cand.average_robust - base.average_robust) +
           0.25 * (cand.min_robust - base.min_robust);
}

bool setupSeverelyDegrades(const Metrics& cand, const Metrics& base, const Metrics& original) {
    double allowed_wns_drop = std::max(0.0005, 0.002 * std::abs(original.wns_ss));
    double allowed_tns_drop = std::max(0.01, 0.002 * std::abs(original.tns_ss));
    return cand.wns_ss < base.wns_ss - allowed_wns_drop ||
           cand.tns_ss < base.tns_ss - allowed_tns_drop;
}

bool holdSeverelyDegrades(const Metrics& cand, const Metrics& base, const Metrics& original) {
    double allowed_wns_drop = std::max(0.005, 0.10 * std::abs(original.wns_ff));
    double allowed_tns_drop = std::max(0.005, 0.10 * std::abs(original.tns_ff));
    return cand.wns_ff < base.wns_ff - allowed_wns_drop ||
           cand.tns_ff < base.tns_ff - allowed_tns_drop;
}

bool areaIncreaseIsUnjustified(const Metrics& cand, const Metrics& base, const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);
    double setup_wns_gain = cand.wns_ss - base.wns_ss;
    double setup_tns_gain = cand.tns_ss - base.tns_ss;
    bool area_increases = cand.area > base.area + 1e-9;
    bool setup_barely_changes = setup_wns_gain < 0.0002 && setup_tns_gain < 0.01;
    bool robust_barely_changes = cand_score.robust < base_score.robust + 1e-5;
    return area_increases && setup_barely_changes && robust_barely_changes;
}

bool acceptableCandidate(const Metrics& cand, const Metrics& base, const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);
    if (candidateImprovementScore(cand_score, base_score, original) <= kScoreEps) return false;
    if (setupSeverelyDegrades(cand, base, original)) return false;
    if (holdSeverelyDegrades(cand, base, original)) return false;
    if (areaIncreaseIsUnjustified(cand, base, original)) return false;
    return true;
}

bool betterCandidateOrder(const Metrics& cand, const Metrics& base, const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);

    double forward = candidateImprovementScore(cand_score, base_score, original);
    double reverse = candidateImprovementScore(base_score, cand_score, original);
    if (forward > reverse + kScoreEps) return true;
    if (reverse > forward + kScoreEps) return false;
    if (cand_score.robust > base_score.robust + kScoreEps) return true;
    if (cand_score.robust < base_score.robust - kScoreEps) return false;

    if (!holdSeverelyDegrades(cand, base, original) && holdSeverelyDegrades(base, cand, original)) return true;
    if (holdSeverelyDegrades(cand, base, original) && !holdSeverelyDegrades(base, cand, original)) return false;

    if (cand.area < base.area - 1e-9) return true;
    if (cand.area > base.area + 1e-9) return false;

    return cand.nvp_ss + cand.nvp_ff < base.nvp_ss + base.nvp_ff;
}

bool betterOutputCandidate(const Metrics& cand, const Metrics& base, const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);

    double forward = candidateImprovementScore(cand_score, base_score, original);
    double reverse = candidateImprovementScore(base_score, cand_score, original);
    if (forward > reverse + kScoreEps) return true;
    if (reverse > forward + kScoreEps) return false;
    if (cand_score.robust > base_score.robust + kScoreEps) return true;
    if (cand_score.robust < base_score.robust - kScoreEps) return false;

    int cand_nvp = cand.nvp_ss + cand.nvp_ff;
    int base_nvp = base.nvp_ss + base.nvp_ff;
    if (cand_nvp < base_nvp) return true;
    if (cand_nvp > base_nvp) return false;

    if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
    if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
    if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
    if (cand.wns_ss < base.wns_ss - kScoreEps) return false;
    return cand.area < base.area - 1e-9;
}

bool setupNvpImproves(const Metrics& cand, const Metrics& base) {
    if (cand.nvp_ss >= base.nvp_ss) return false;
    int nvp_gain = base.nvp_ss - cand.nvp_ss;
    double wns_tol = nvp_gain >= 100 ? 0.010 : 0.006;
    double tns_tol = nvp_gain >= 100 ? 120.0 : 40.0;
    return cand.wns_ss >= base.wns_ss - wns_tol &&
           cand.tns_ss >= base.tns_ss - tns_tol;
}

bool setupProtected(const Metrics& cand, const Metrics& base, double wns_tol, double tns_tol) {
    return cand.wns_ss >= base.wns_ss - wns_tol &&
           cand.tns_ss >= base.tns_ss - tns_tol &&
           cand.nvp_ss <= base.nvp_ss + 20;
}

bool holdProtected(const Metrics& cand, const Metrics& base, double wns_tol, double tns_tol) {
    return cand.wns_ff >= base.wns_ff - wns_tol &&
           cand.tns_ff >= base.tns_ff - tns_tol;
}

bool acceptableForPhase(PhaseKind phase,
                        const Metrics& cand,
                        const Metrics& base,
                        const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);
    const bool setup_first = phase == PhaseKind::WorstPathSurgery ||
                             phase == PhaseKind::WnsRepair ||
                             phase == PhaseKind::UndoHarmfulDownsize ||
                             phase == PhaseKind::TnsCleanup;
    const bool undo_focused = phase == PhaseKind::UndoHarmfulDownsize;
    // Setup-first phases deliberately bypass the old balanced-score and 0.05
    // per-term guards.  Those guards made the advertised relaxed-hold phase
    // behave like a hold-preserving phase in practice.
    if (!setup_first) {
        if (!undo_focused && candidateImprovementScore(cand_score, base_score, original) <= kScoreEps) return false;
        if (!normalizedDamageIsSafe(cand_score, base_score, original)) return false;
    }

    const bool large_wns_gain = cand_score.ss_wns >= base_score.ss_wns + 0.03;
    const double ff_wns_floor = large_wns_gain ? 0.50 : 0.70;
    // A run begins at normalized term zero, so early preprocessing may still
    // be climbing toward the requested budget.  Below a floor, require
    // monotonic recovery; once reached, the full temporary budget is usable.
    const auto withinBudget = [](double candidate, double current, double floor) {
        return candidate >= floor || (current < floor && candidate >= current - kScoreEps);
    };
    const bool temporary_hold_ok = withinBudget(cand_score.ff_tns, base_score.ff_tns, 0.70) &&
                                   withinBudget(cand_score.ff_wns, base_score.ff_wns, ff_wns_floor);

    switch (phase) {
        case PhaseKind::WorstPathSurgery: {
            const bool peels_worst_cluster = cand_score.ss_wns >= base_score.ss_wns - kScoreEps &&
                cand_score.ss_tns > base_score.ss_tns + 1e-7;
            const bool direct_breakthrough = cand_score.ss_wns > base_score.ss_wns + kScoreEps;
            return (direct_breakthrough || peels_worst_cluster) &&
                   worstPathSetupScore(cand_score) > worstPathSetupScore(base_score) + kScoreEps &&
                   cand_score.area >= 0.25 && temporary_hold_ok;
        }
        case PhaseKind::WnsRepair: {
            const bool direct_wns_gain = cand_score.ss_wns > base_score.ss_wns + kScoreEps &&
                setupWnsFocusedScore(cand_score) > setupWnsFocusedScore(base_score) + kScoreEps;
            const bool unlocks_shared_setup =
                std::abs(cand_score.ss_wns - base_score.ss_wns) <= kScoreEps &&
                cand.nvp_ss + 10 < base.nvp_ss && cand.area <= base.area + 4.0;
            return (direct_wns_gain || unlocks_shared_setup) &&
                   cand.wns_ss >= base.wns_ss - 1e-12 &&
                   cand_score.area >= 0.25 && temporary_hold_ok;
        }
        case PhaseKind::UndoHarmfulDownsize:
            return cand_score.ss_wns > base_score.ss_wns + kScoreEps &&
                   setupWnsFocusedScore(cand_score) > setupWnsFocusedScore(base_score) + kScoreEps &&
                   setupProtected(cand, base, 0.0, 8.0) &&
                   cand_score.area >= 0.25 && temporary_hold_ok;
        case PhaseKind::TnsCleanup:
            return (cand_score.ss_tns > base_score.ss_tns + kScoreEps || setupNvpImproves(cand, base)) &&
                   setupProtected(cand, base, 0.010, 120.0) &&
                   cand_score.area >= 0.25 && temporary_hold_ok &&
                   cand_score.ss_wns >= base_score.ss_wns - kScoreEps;
        case PhaseKind::GroupInsertion:
            return ((cand_score.ss_tns + cand_score.ss_wns >
                     base_score.ss_tns + base_score.ss_wns + kScoreEps) ||
                    cand_score.area > base_score.area + kScoreEps) &&
                   setupProtected(cand, base, 0.001, 0.25) &&
                   holdProtected(cand, base, 0.001, 0.005);
        case PhaseKind::AggressiveSetup: {
            double hold_wns_floor = original.wns_ff - std::max(0.010, 0.35 * std::abs(original.wns_ff));
            double hold_tns_floor = original.tns_ff - std::max(0.25, 0.35 * std::abs(original.tns_ff));
            bool strong_setup = cand_score.ss_wns > base_score.ss_wns + 0.0005 ||
                                cand_score.ss_tns > base_score.ss_tns + 0.0005 ||
                                cand.nvp_ss + 25 < base.nvp_ss;
            return strong_setup &&
                   setupProtected(cand, base, 0.004, 60.0) &&
                   cand.wns_ff >= hold_wns_floor &&
                   cand.tns_ff >= hold_tns_floor;
        }
        case PhaseKind::DownsizeForDelay:
            return cand.area < base.area - 1e-9 &&
                   cand_score.timing_score > base_score.timing_score + kScoreEps &&
                   setupProtected(cand, base, 0.003, 12.0) &&
                   holdProtected(cand, base, 0.006, 0.08);
        case PhaseKind::HoldRepair:
            return (cand_score.ff_tns + cand_score.ff_wns >
                    base_score.ff_tns + base_score.ff_wns + kScoreEps) &&
                   setupProtected(cand, base, 0.002, 0.50);
        case PhaseKind::AreaRecovery:
            return base_score.ss_tns > 0.85 && base_score.ss_wns > 0.80 &&
                   base_score.ff_tns > 0.40 && base_score.ff_wns > 0.40 &&
                   cand_score.area > base_score.area + kScoreEps &&
                   cand.wns_ss >= base.wns_ss - 1e-12 &&
                   cand.tns_ss >= base.tns_ss - 1e-9 &&
                   cand.nvp_ss <= base.nvp_ss &&
                   cand.wns_ff >= base.wns_ff - 1e-12 &&
                   cand.tns_ff >= base.tns_ff - 1e-9 &&
                   cand.nvp_ff <= base.nvp_ff;
    }
    return acceptableCandidate(cand, base, original);
}

bool betterCandidateForPhase(PhaseKind phase,
                             const Metrics& cand,
                             const Metrics& base,
                             const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);
    if (phase == PhaseKind::WorstPathSurgery ||
        phase == PhaseKind::WnsRepair || phase == PhaseKind::UndoHarmfulDownsize) {
        if (phase == PhaseKind::WorstPathSurgery) {
            const double cand_focused = worstPathSetupScore(cand_score);
            const double base_focused = worstPathSetupScore(base_score);
            if (cand_score.ss_wns > base_score.ss_wns + kScoreEps) return true;
            if (cand_score.ss_wns < base_score.ss_wns - kScoreEps) return false;
            if (cand_focused > base_focused + kScoreEps) return true;
            if (cand_focused < base_focused - kScoreEps) return false;
            return cand.tns_ss > base.tns_ss;
        }
        const double cand_focused = setupWnsFocusedScore(cand_score);
        const double base_focused = setupWnsFocusedScore(base_score);
        if (cand_score.ss_wns > base_score.ss_wns + kScoreEps) return true;
        if (cand_score.ss_wns < base_score.ss_wns - kScoreEps) return false;
        if (cand_focused > base_focused + kScoreEps) return true;
        if (cand_focused < base_focused - kScoreEps) return false;
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
        if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::TnsCleanup) {
        if (cand_score.ss_tns > base_score.ss_tns + kScoreEps) return true;
        if (cand_score.ss_tns < base_score.ss_tns - kScoreEps) return false;
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
        if (cand.wns_ss < base.wns_ss - kScoreEps) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::GroupInsertion) {
        if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
        if (cand.wns_ss < base.wns_ss - kScoreEps) return false;
        if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
        if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::AggressiveSetup) {
        if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
        if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
        if (cand.wns_ss < base.wns_ss - kScoreEps) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::DownsizeForDelay) {
        if (cand_score.timing_score > base_score.timing_score + kScoreEps) return true;
        if (cand_score.timing_score < base_score.timing_score - kScoreEps) return false;
        if (cand.area < base.area - 1e-9) return true;
        if (cand.area > base.area + 1e-9) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::HoldRepair) {
        if (cand_score.ff_wns > base_score.ff_wns + kScoreEps) return true;
        if (cand_score.ff_wns < base_score.ff_wns - kScoreEps) return false;
        if (cand_score.ff_tns > base_score.ff_tns + kScoreEps) return true;
        if (cand_score.ff_tns < base_score.ff_tns - kScoreEps) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::AreaRecovery) {
        if (cand.area < base.area - 1e-9) return true;
        if (cand.area > base.area + 1e-9) return false;
        return betterCandidateOrder(cand, base, original);
    }
    return betterCandidateOrder(cand, base, original);
}

void addAncestorsForFocus(const std::unordered_map<std::string, ClockNode>& tree,
                          const std::string& sink,
                          double priority,
                          std::unordered_map<std::string, double>& node_priority,
                          std::unordered_set<std::string>& focus_nodes,
                          std::vector<std::pair<std::string, std::string>>& focus_edges) {
    std::string child = sink;
    double weight = 0.55;
    for (int depth = 0; depth < 8; ++depth) {
        auto child_it = tree.find(child);
        if (child_it == tree.end() || child_it->second.parent.empty()) break;
        std::string parent = child_it->second.parent;
        focus_nodes.insert(parent);
        focus_edges.push_back({parent, child});
        node_priority[parent] += priority * weight;
        child = parent;
        weight *= 0.55;
    }
}

TargetSkewGuide buildTargetSkewGuide(const std::unordered_map<std::string, ClockNode>& tree,
                                     const TimingReport& report,
                                     const std::vector<DataPath>& ss_paths,
                                     const std::vector<DataPath>& ff_paths,
                                     double clock_period,
                                     PhaseKind phase) {
    TargetSkewGuide guide;
    std::unordered_set<std::string> sinks;
    for (const auto& pair : tree) {
        if (pair.second.is_sink) {
            sinks.insert(pair.first);
            guide.sink_target[pair.first] = 0.0;
        }
    }

    const double setup_time = 0.08 * clock_period;
    const double hold_time = 0.05 * clock_period;
    const double setup_weight = (phase == PhaseKind::AggressiveSetup ||
                                 phase == PhaseKind::UndoHarmfulDownsize ||
                                 phase == PhaseKind::DownsizeForDelay) ? 1.35 : 1.0;
    const double hold_weight = (phase == PhaseKind::HoldRepair) ? 1.25 : 0.55;

    for (int iter = 0; iter < 4; ++iter) {
        std::size_t visited = 0;
        std::size_t limit = std::min<std::size_t>(ss_paths.size(),
            std::max<std::size_t>(5000, dynamicTopSetupPaths(report.metrics.nvp_ss) * 5));
        for (const auto& path : ss_paths) {
            if (++visited > limit) break;
            if (sinks.find(path.launch_ff) == sinks.end() || sinks.find(path.capture_ff) == sinks.end()) continue;
            double required = path.delay + setup_time - clock_period;
            if (required <= -0.5) continue;
            double lhs = guide.sink_target[path.capture_ff] - guide.sink_target[path.launch_ff];
            double deficit = required - lhs;
            if (deficit <= 0.0) continue;
            double step = setup_weight * 0.5 * deficit;
            guide.sink_target[path.capture_ff] += step;
            guide.sink_target[path.launch_ff] -= 0.35 * step;
        }

        visited = 0;
        limit = std::min<std::size_t>(ff_paths.size(),
            std::max<std::size_t>(3000, dynamicTopHoldPaths(report.metrics.nvp_ff) * 10));
        for (const auto& path : ff_paths) {
            if (++visited > limit) break;
            if (sinks.find(path.launch_ff) == sinks.end() || sinks.find(path.capture_ff) == sinks.end()) continue;
            double upper = path.delay - hold_time;
            if (upper >= 0.5) continue;
            double lhs = guide.sink_target[path.capture_ff] - guide.sink_target[path.launch_ff];
            double excess = lhs - upper;
            if (excess <= 0.0) continue;
            double step = hold_weight * 0.5 * excess;
            guide.sink_target[path.launch_ff] += step;
            guide.sink_target[path.capture_ff] -= 0.35 * step;
        }
    }

    for (const auto& v : report.ss_violations) {
        double need = -v.slack;
        guide.sink_target[v.capture] += setup_weight * 0.65 * need;
        guide.sink_target[v.launch] -= setup_weight * 0.30 * need;
    }
    for (const auto& v : report.ff_violations) {
        double need = -v.slack;
        guide.sink_target[v.launch] += hold_weight * 0.85 * need;
        guide.sink_target[v.capture] -= hold_weight * 0.35 * need;
    }

    for (const auto& pair : guide.sink_target) {
        if (pair.second > 1e-9) ++guide.positive_sinks;
        if (pair.second < -1e-9) ++guide.negative_sinks;
        std::string child = pair.first;
        double weight = 1.0;
        for (int depth = 0; depth < 10; ++depth) {
            auto child_it = tree.find(child);
            if (child_it == tree.end() || child_it->second.parent.empty()) break;
            const std::string& parent = child_it->second.parent;
            guide.node_target[parent] += pair.second * weight;
            guide.edge_target[parent + "|" + child] += pair.second * weight;
            child = parent;
            weight *= 0.58;
        }
    }
    return guide;
}

std::string moveKey(const Move& move) {
    std::string chain_key;
    for (std::size_t i = 0; i < move.chain_types.size(); ++i) {
        chain_key += "/" + move.chain_types[i];
    }
    return std::to_string(static_cast<int>(move.kind)) + "|" +
           move.parent + "|" + move.child + "|" + move.node + "|" +
           move.first_type + "|" + move.second_type + "|" + move.third_type + "|" +
           std::to_string(move.chain_len) + "|" + chain_key;
}

void pushUniqueMove(const Move& move,
                    std::vector<Move>& moves,
                    std::unordered_set<std::string>& seen) {
    std::string key = moveKey(move);
    if (seen.insert(key).second) {
        moves.push_back(move);
    }
}

std::vector<ChainCandidate> synthesizeChainsForTarget(
        const std::vector<ChainCandidate>& lut,
        double target_delta,
        std::size_t max_results,
        bool hold_mode);

/*
void addInsertionMoves(const std::string& parent,
                       const std::string& child,
                       MoveKind kind,
                       double priority,
                       const std::vector<std::string>& cells,
                       const std::unordered_map<std::string, BufferCell>& buf_lib,
                       std::vector<Move>& moves,
                       std::unordered_set<std::string>& seen) {
    for (const auto& first : cells) {
        const BufferCell& first_cell = buf_lib.at(first);
        if (first_cell.max_fanout < 1) continue;

        Move one;
        one.kind = kind;
        one.parent = parent;
        one.child = child;
        one.first_type = first;
        one.chain_types.push_back(first);
        one.chain_len = 1;
        one.estimate = priority / (1.0 + first_cell.area);
        pushUniqueMove(one, moves, seen);

        for (const auto& second : cells) {
            const BufferCell& second_cell = buf_lib.at(second);
            if (second_cell.max_fanout < 1) continue;
            Move two;
            two.kind = kind;
            two.parent = parent;
            two.child = child;
            two.first_type = first;
            two.second_type = second;
            two.chain_types.push_back(first);
            two.chain_types.push_back(second);
            two.chain_len = 2;
            two.estimate = 1.4 * priority / (1.0 + first_cell.area + second_cell.area);
            pushUniqueMove(two, moves, seen);
        }
    }
}
*/

void addChainMovesForEdge(const std::string& parent,
                          const std::string& child,
                          MoveKind kind,
                          double priority,
                          double target_delay,
                          bool hold_mode,
                          const std::vector<ChainCandidate>& chain_lut,
                          std::size_t max_chains,
                          CandidateDirection direction,
                          std::vector<Move>& moves,
                          std::unordered_set<std::string>& seen) {
    if (priority <= 0.0) return;
    double target = std::max(0.0, target_delay);
    std::vector<ChainCandidate> chains = synthesizeChainsForTarget(chain_lut, target, max_chains, hold_mode);
    for (const auto& chain : chains) {
        if (chain.types.empty()) continue;
        Move move;
        move.kind = kind;
        move.parent = parent;
        move.child = child;
        move.chain_types = chain.types;
        move.chain_len = static_cast<int>(chain.types.size());
        move.direction = direction;
        move.first_type = chain.types[0];
        if (chain.types.size() > 1) move.second_type = chain.types[1];
        if (chain.types.size() > 2) move.third_type = chain.types[2];
        double delay = hold_mode ? chain.ff_delay : chain.ss_delay;
        double damage = hold_mode ? chain.ss_delay : chain.ff_delay;
        move.estimate = priority * (1.0 + std::min(delay, target + 0.05))
                      - 50000.0 * std::abs(delay - target)
                      - 1500.0 * damage
                      - 1000.0 * chain.area
                      - 50.0 * chain.chain_len;
        pushUniqueMove(move, moves, seen);
    }
}

std::vector<ChainCandidate> buildChainLookup(
        const std::vector<std::string>& cells,
        const std::unordered_map<std::string, BufferCell>& buf_lib) {
    std::vector<ChainCandidate> frontier;
    std::vector<ChainCandidate> current;
    for (const auto& cell : cells) {
        if (buf_lib.at(cell).max_fanout < 1) continue;
        ChainCandidate c;
        c.types.push_back(cell);
        c.first_type = cell;
        c.chain_len = 1;
        c.ss_delay = nodeDelay(buf_lib, cell, 1, true);
        c.ff_delay = nodeDelay(buf_lib, cell, 1, false);
        c.area = cellArea(buf_lib, cell);
        current.push_back(c);
        frontier.push_back(c);
    }

    const int max_chain_len = 8;
    const std::size_t max_frontier = 400;
    for (int len = 2; len <= max_chain_len; ++len) {
        std::vector<ChainCandidate> next;
        next.reserve(std::min<std::size_t>(current.size() * cells.size(), 20000));
        for (const auto& base : current) {
            for (const auto& cell : cells) {
                if (buf_lib.at(cell).max_fanout < 1) continue;
                ChainCandidate c = base;
                c.types.push_back(cell);
                c.chain_len = len;
                c.ss_delay += nodeDelay(buf_lib, cell, 1, true);
                c.ff_delay += nodeDelay(buf_lib, cell, 1, false);
                c.area += cellArea(buf_lib, cell);
                next.push_back(c);
            }
        }

        std::sort(next.begin(), next.end(), [](const ChainCandidate& a, const ChainCandidate& b) {
            if (a.area != b.area) return a.area < b.area;
            if (a.ss_delay != b.ss_delay) return a.ss_delay < b.ss_delay;
            if (a.ff_delay != b.ff_delay) return a.ff_delay < b.ff_delay;
            return a.types < b.types;
        });

        std::vector<ChainCandidate> pareto;
        for (const auto& cand : next) {
            bool dominated = false;
            for (std::size_t i = 0; i < pareto.size(); ++i) {
                const ChainCandidate& p = pareto[i];
                if (p.ss_delay >= cand.ss_delay - 1e-12 &&
                    p.ff_delay <= cand.ff_delay + 1e-12 &&
                    p.area <= cand.area + 1e-12) {
                    dominated = true;
                    break;
                }
            }
            if (!dominated) pareto.push_back(cand);
            if (pareto.size() >= max_frontier) break;
        }
        current.swap(pareto);
        frontier.insert(frontier.end(), current.begin(), current.end());
    }

    for (auto& c : frontier) {
        if (!c.types.empty()) c.first_type = c.types[0];
        if (c.types.size() > 1) c.second_type = c.types[1];
        if (c.types.size() > 2) c.third_type = c.types[2];
    }

    std::sort(frontier.begin(), frontier.end(), [](const ChainCandidate& a, const ChainCandidate& b) {
        if (a.ss_delay != b.ss_delay) return a.ss_delay < b.ss_delay;
        if (a.area != b.area) return a.area < b.area;
        return a.chain_len < b.chain_len;
    });
    return frontier;
}

std::vector<ChainCandidate> synthesizeChainsForTarget(
        const std::vector<ChainCandidate>& lut,
        double target_delta,
        std::size_t max_results,
        bool hold_mode = false) {
    std::vector<ChainCandidate> candidates = lut;
    std::sort(candidates.begin(), candidates.end(),
              [target_delta, hold_mode](const ChainCandidate& a, const ChainCandidate& b) {
        double a_delay = hold_mode ? a.ff_delay : a.ss_delay;
        double b_delay = hold_mode ? b.ff_delay : b.ss_delay;
        double a_error = std::abs(a_delay - target_delta);
        double b_error = std::abs(b_delay - target_delta);
        if (a_error != b_error) return a_error < b_error;
        double a_damage = hold_mode ? a.ss_delay : a.ff_delay;
        double b_damage = hold_mode ? b.ss_delay : b.ff_delay;
        if (a_damage != b_damage) return a_damage < b_damage;
        if (a.area != b.area) return a.area < b.area;
        return a.chain_len < b.chain_len;
    });
    if (candidates.size() > max_results) candidates.resize(max_results);
    return candidates;
}

void addWnsRepairMoves(const std::unordered_map<std::string, ClockNode>& tree,
                       const std::unordered_map<std::string, BufferCell>& buf_lib,
                       const TimingReport& report,
                       const std::vector<ChainCandidate>& chain_lut,
                       std::vector<Move>& moves,
                       std::unordered_set<std::string>& seen) {
    (void)buf_lib;
    std::size_t ss_count = std::min(dynamicWnsSetupPaths(report.metrics.nvp_ss), report.ss_violations.size());
    for (std::size_t i = 0; i < ss_count; ++i) {
        const auto& violation = report.ss_violations[i];
        auto capture_it = tree.find(violation.capture);
        if (capture_it == tree.end() || !capture_it->second.is_sink || capture_it->second.parent.empty()) continue;

        const double need_delay = -violation.slack;
        const double rank_bonus = static_cast<double>(ss_count - i) * 1000000.0;
        const std::string& parent = capture_it->second.parent;
        const std::string& child = violation.capture;
        std::vector<ChainCandidate> chains = synthesizeChainsForTarget(chain_lut, need_delay, 12, false);

        for (const auto& chain : chains) {
            if (chain.types.empty()) continue;
            Move move;
            move.kind = MoveKind::InsertLeaf;
            move.parent = parent;
            move.child = child;
            move.chain_types = chain.types;
            move.first_type = chain.types[0];
            if (chain.types.size() > 1) move.second_type = chain.types[1];
            if (chain.types.size() > 2) move.third_type = chain.types[2];
            move.chain_len = chain.chain_len;
            move.direction = CandidateDirection::CaptureSide;
            move.estimate = 1000000000.0 + rank_bonus
                         - 100000.0 * std::abs(chain.ss_delay - need_delay)
                         - 1000.0 * chain.area;
            pushUniqueMove(move, moves, seen);
        }
    }
}

void addWorstPathSurgeryMoves(
        const std::unordered_map<std::string, ClockNode>& tree,
        const std::unordered_map<std::string, BufferCell>& buf_lib,
        const TimingReport& report,
        const std::vector<ChainCandidate>& chain_lut,
        std::vector<Move>& moves,
        std::unordered_set<std::string>& seen) {
    const std::vector<std::string> cells = sortedCellNames(buf_lib);
    const std::size_t path_count = std::min<std::size_t>(20, report.ss_violations.size());
    for (std::size_t rank = 0; rank < path_count; ++rank) {
        const PathViolation& violation = report.ss_violations[rank];
        const std::vector<std::string> launch_path = clockPathToSink(tree, violation.launch);
        const std::vector<std::string> capture_path = clockPathToSink(tree, violation.capture);
        std::size_t common = 0;
        while (common < launch_path.size() && common < capture_path.size() &&
               launch_path[common] == capture_path[common]) ++common;

        const double rank_priority = 1.0e9 - 1.0e6 * static_cast<double>(rank);
        const double needed = std::max(0.005, -violation.slack);
        const double step_target = std::min(needed, 0.20);

        // Capture-only buffers: add delay by legal slow-cell resizing.
        for (std::size_t i = common; i < capture_path.size(); ++i) {
            const ClockNode& node = tree.at(capture_path[i]);
            const int fanout = static_cast<int>(node.children.size());
            const double old_ss = nodeDelay(buf_lib, node.type, fanout, true);
            for (const std::string& cell : cells) {
                if (cell == node.type || fanout > buf_lib.at(cell).max_fanout) continue;
                const double delta = nodeDelay(buf_lib, cell, fanout, true) - old_ss;
                if (delta <= 1e-12) continue;
                Move move;
                move.kind = MoveKind::ResizeBuffer;
                move.node = node.name;
                move.old_type = node.type;
                move.first_type = cell;
                move.direction = CandidateDirection::CaptureSide;
                move.estimate = rank_priority + 1.0e5 * std::min(delta, needed)
                              - 100.0 * cellArea(buf_lib, cell);
                pushUniqueMove(move, moves, seen);
            }
        }

        // Launch-only buffers: explicitly undo downsizing and select faster cells.
        for (std::size_t i = common; i < launch_path.size(); ++i) {
            const ClockNode& node = tree.at(launch_path[i]);
            const int fanout = static_cast<int>(node.children.size());
            const double old_ss = nodeDelay(buf_lib, node.type, fanout, true);
            for (const std::string& cell : cells) {
                if (cell == node.type || fanout > buf_lib.at(cell).max_fanout) continue;
                const double gain = old_ss - nodeDelay(buf_lib, cell, fanout, true);
                if (gain <= 1e-12) continue;
                Move move;
                move.kind = MoveKind::ResizeBuffer;
                move.node = node.name;
                move.old_type = node.type;
                move.first_type = cell;
                move.direction = CandidateDirection::LaunchSide;
                move.estimate = rank_priority + 1.0e5 * std::min(gain, needed)
                              - 50.0 * cellArea(buf_lib, cell);
                pushUniqueMove(move, moves, seen);
            }
        }

        auto capture = tree.find(violation.capture);
        if (capture == tree.end() || capture->second.parent.empty()) continue;
        // Direct capture surgery is always present. Include short and long
        // Pareto chains (1..8), not merely chains closest to the full slack.
        std::vector<ChainCandidate> chains = chain_lut;
        std::sort(chains.begin(), chains.end(), [&](const ChainCandidate& a, const ChainCandidate& b) {
            const double ae = std::abs(a.ss_delay - step_target);
            const double be = std::abs(b.ss_delay - step_target);
            if (ae != be) return ae < be;
            if (a.area != b.area) return a.area < b.area;
            return a.chain_len < b.chain_len;
        });
        if (chains.size() > 12) chains.resize(12);
        for (const ChainCandidate& chain : chains) {
            Move move;
            move.kind = MoveKind::InsertLeaf;
            move.parent = capture->second.parent;
            move.child = violation.capture;
            move.chain_types = chain.types;
            move.chain_len = chain.chain_len;
            move.first_type = chain.types.front();
            move.direction = CandidateDirection::CaptureSide;
            move.estimate = rank_priority + 2.0e5 * std::min(chain.ss_delay, step_target)
                          - 1000.0 * std::abs(chain.ss_delay - step_target)
                          - 100.0 * chain.area;
            pushUniqueMove(move, moves, seen);
        }

        // Insert immediately after the common prefix on the capture branch;
        // this affects capture but provably not this path's launch side.
        if (common < capture_path.size()) {
            const std::string& child = capture_path[common];
            const auto child_it = tree.find(child);
            if (child_it != tree.end() && !child_it->second.parent.empty()) {
                std::vector<ChainCandidate> internal =
                    synthesizeChainsForTarget(chain_lut, step_target, 6, false);
                for (const ChainCandidate& chain : internal) {
                    Move move;
                    move.kind = MoveKind::InsertInternal;
                    move.parent = child_it->second.parent;
                    move.child = child;
                    move.chain_types = chain.types;
                    move.chain_len = chain.chain_len;
                    move.first_type = chain.types.front();
                    move.direction = CandidateDirection::CaptureSide;
                    move.estimate = rank_priority + 1.5e5 * std::min(chain.ss_delay, step_target)
                                  - 100.0 * chain.area;
                    pushUniqueMove(move, moves, seen);
                }
            }
        }
    }
}

double tnsCleanupPriority(const SinkStat& stat) {
    double net_count = static_cast<double>(stat.count) - 0.85 * static_cast<double>(stat.launch_count);
    double net_slack = stat.total_neg_slack - 0.65 * stat.launch_neg_slack;
    return 30000.0 * net_count
         + 3000.0 * net_slack
         + 2000.0 * (-stat.worst_slack);
}

void addTnsCleanupFocus(const std::unordered_map<std::string, ClockNode>& tree,
                        const TimingReport& report,
                        const TargetSkewGuide& guide,
                        std::unordered_map<std::string, double>& node_priority,
                        std::unordered_set<std::string>& focus_sinks,
                        std::unordered_set<std::string>& focus_nodes,
                        std::vector<std::pair<std::string, std::string>>& focus_edges) {
    std::unordered_map<std::string, SinkStat> sink_stats;
    std::size_t ss_count = std::min(kTnsCleanupSetupPaths, report.ss_violations.size());

    for (std::size_t i = 0; i < ss_count; ++i) {
        const auto& violation = report.ss_violations[i];
        SinkStat& stat = sink_stats[violation.capture];
        stat.name = violation.capture;
        ++stat.count;
        stat.total_neg_slack += -violation.slack;
        stat.worst_slack = std::min(stat.worst_slack, violation.slack);

        SinkStat& launch_stat = sink_stats[violation.launch];
        launch_stat.name = violation.launch;
        ++launch_stat.launch_count;
        launch_stat.launch_neg_slack += -violation.slack;
    }

    std::vector<SinkStat> ranked_sinks;
    ranked_sinks.reserve(sink_stats.size());
    for (const auto& pair : sink_stats) {
        ranked_sinks.push_back(pair.second);
    }

    std::sort(ranked_sinks.begin(), ranked_sinks.end(), [](const SinkStat& a, const SinkStat& b) {
        double pa = tnsCleanupPriority(a);
        double pb = tnsCleanupPriority(b);
        if (pa != pb) return pa > pb;
        return a.name < b.name;
    });

    if (ranked_sinks.size() > kTnsCleanupSinkLimit) {
        ranked_sinks.resize(kTnsCleanupSinkLimit);
    }

    for (const auto& stat : ranked_sinks) {
        auto sink_it = tree.find(stat.name);
        if (sink_it == tree.end() || !sink_it->second.is_sink) continue;

        double priority = tnsCleanupPriority(stat);
        if (std::abs(priority) <= 1e-12) continue;
        focus_sinks.insert(stat.name);
        node_priority[stat.name] += priority;
        addAncestorsForFocus(tree, stat.name, 0.65 * priority,
                             node_priority, focus_nodes, focus_edges);
    }

    for (const auto& pair : guide.sink_target) {
        if (std::abs(pair.second) < 1e-5) continue;
        auto sink_it = tree.find(pair.first);
        if (sink_it == tree.end() || !sink_it->second.is_sink) continue;
        double priority = 25000.0 * pair.second;
        focus_sinks.insert(pair.first);
        node_priority[pair.first] += priority;
        addAncestorsForFocus(tree, pair.first, priority,
                             node_priority, focus_nodes, focus_edges);
    }
}

void addAreaRecoveryMoves(const std::unordered_map<std::string, ClockNode>& tree,
                          const std::unordered_map<std::string, BufferCell>& buf_lib,
                          const std::vector<std::string>& cells,
                          std::vector<Move>& moves,
                          std::unordered_set<std::string>& seen) {
    for (const auto& pair : tree) {
        const ClockNode& node = pair.second;
        if (node.type == "ROOT" || node.is_sink) continue;
        auto old_cell = buf_lib.find(node.type);
        if (old_cell == buf_lib.end()) continue;

        int fanout = static_cast<int>(node.children.size());
        for (const auto& cell_name : cells) {
            auto new_cell = buf_lib.find(cell_name);
            if (new_cell == buf_lib.end()) continue;
            if (fanout > new_cell->second.max_fanout) continue;
            if (new_cell->second.area >= old_cell->second.area - 1e-12) continue;

            Move move;
            move.kind = MoveKind::ResizeBuffer;
            move.node = node.name;
            move.first_type = cell_name;
            move.old_type = node.type;
            move.direction = CandidateDirection::Neutral;
            move.chain_len = 0;
            move.estimate = old_cell->second.area - new_cell->second.area;
            pushUniqueMove(move, moves, seen);
        }
    }
}

void addDownsizeForDelayMoves(const std::unordered_map<std::string, ClockNode>& tree,
                              const std::unordered_map<std::string, BufferCell>& buf_lib,
                              const std::vector<std::string>& cells,
                              const TargetSkewGuide& guide,
                              std::vector<Move>& moves,
                              std::unordered_set<std::string>& seen) {
    for (const auto& pair : tree) {
        const ClockNode& node = pair.second;
        if (node.type == "ROOT" || node.is_sink) continue;
        const auto old_it = buf_lib.find(node.type);
        if (old_it == buf_lib.end()) continue;

        const int fanout = static_cast<int>(node.children.size());
        const double old_ss = nodeDelay(buf_lib, node.type, fanout, true);
        const double old_ff = nodeDelay(buf_lib, node.type, fanout, false);
        const auto target_it = guide.node_target.find(node.name);
        const double target = target_it == guide.node_target.end() ? 0.0 : target_it->second;

        // A negative target marks a launch-dominated subtree.  Slowing it is
        // setup-harmful, so leave it for the explicit launch speedup phase.
        if (target < -1e-5) continue;

        for (const auto& cell_name : cells) {
            const BufferCell& cell = buf_lib.at(cell_name);
            if (cell_name == node.type || fanout > cell.max_fanout ||
                cell.area >= old_it->second.area - 1e-12) continue;
            const double delta_ss = nodeDelay(buf_lib, cell_name, fanout, true) - old_ss;
            const double delta_ff = nodeDelay(buf_lib, cell_name, fanout, false) - old_ff;
            const double area_gain = old_it->second.area - cell.area;

            Move move;
            move.kind = MoveKind::ResizeBuffer;
            move.node = node.name;
            move.first_type = cell_name;
            move.old_type = node.type;
            move.direction = target > 1e-5 ? CandidateDirection::CaptureSide
                                            : CandidateDirection::Neutral;
            // Rank useful capture delay first, then area.  Exact incremental
            // evaluation still decides whether the move is accepted.
            move.estimate = 1000000.0 * target * delta_ss
                          - 5000.0 * std::abs(delta_ff - delta_ss)
                          + 1000.0 * area_gain;
            pushUniqueMove(move, moves, seen);
            // Cells are area-sorted.  One direct minimum-area alternative per
            // node lets the exact budget cover the whole tree.
            break;
        }
    }
}

bool isSpeedupResizeMove(const Move& move,
                         const std::unordered_map<std::string, ClockNode>& tree,
                         const std::unordered_map<std::string, BufferCell>& buf_lib) {
    if (move.kind != MoveKind::ResizeBuffer) return false;
    auto node_it = tree.find(move.node);
    if (node_it == tree.end()) return false;
    int fanout = static_cast<int>(node_it->second.children.size());
    double old_delay = 0.5 * (nodeDelay(buf_lib, node_it->second.type, fanout, true) +
                              nodeDelay(buf_lib, node_it->second.type, fanout, false));
    double new_delay = 0.5 * (nodeDelay(buf_lib, move.first_type, fanout, true) +
                              nodeDelay(buf_lib, move.first_type, fanout, false));
    return new_delay < old_delay - 1e-12;
}

void trimCandidateMoves(std::vector<Move>& moves,
                        const std::unordered_map<std::string, ClockNode>& tree,
                        const std::unordered_map<std::string, BufferCell>& buf_lib,
                        PhaseKind phase) {
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.estimate > b.estimate;
    });

    std::vector<Move> speedups;
    std::vector<Move> resizes;
    std::vector<Move> insertions;
    for (const Move& move : moves) {
        if (move.kind != MoveKind::ResizeBuffer) insertions.push_back(move);
        else if (isSpeedupResizeMove(move, tree, buf_lib)) speedups.push_back(move);
        else resizes.push_back(move);
    }

    // Estimates from path-ranked insertions and subtree-ranked resizes have
    // different scales.  Interleave the classes so the exact-evaluation
    // budget cannot be monopolized by thousands of near-duplicate leaf moves.
    std::vector<Move> ordered;
    ordered.reserve(std::min(moves.size(), kMaxCandidatesPerIteration));
    std::size_t si = 0, ri = 0, ii = 0;
    const bool resize_only = phase == PhaseKind::DownsizeForDelay ||
                             phase == PhaseKind::UndoHarmfulDownsize ||
                             phase == PhaseKind::AreaRecovery;
    while (ordered.size() < kMaxCandidatesPerIteration &&
           (si < speedups.size() || ri < resizes.size() || ii < insertions.size())) {
        if (si < speedups.size()) ordered.push_back(speedups[si++]);
        if (ordered.size() >= kMaxCandidatesPerIteration) break;
        if (ri < resizes.size()) ordered.push_back(resizes[ri++]);
        if (ordered.size() >= kMaxCandidatesPerIteration) break;
        if (!resize_only && ii < insertions.size()) ordered.push_back(insertions[ii++]);
        if (resize_only && si >= speedups.size() && ri >= resizes.size()) break;
    }
    moves.swap(ordered);
}

std::vector<Move> generateCandidateMoves(
        const std::unordered_map<std::string, ClockNode>& tree,
        const std::unordered_map<std::string, BufferCell>& buf_lib,
        const TimingReport& report,
        const std::vector<DataPath>& ss_paths,
        const std::vector<DataPath>& ff_paths,
        double clock_period,
        PhaseKind phase,
        const std::vector<ChainCandidate>& chain_lut) {
    std::vector<Move> moves;
    std::unordered_set<std::string> seen;
    std::vector<std::string> cells = sortedCellNames(buf_lib);
    TargetSkewGuide guide = buildTargetSkewGuide(tree, report, ss_paths, ff_paths, clock_period, phase);
    std::unordered_map<std::string, double> node_priority;
    std::unordered_set<std::string> focus_sinks;
    std::unordered_set<std::string> focus_nodes;
    std::vector<std::pair<std::string, std::string>> focus_edges;

    if (phase == PhaseKind::AreaRecovery) {
        addAreaRecoveryMoves(tree, buf_lib, cells, moves, seen);
        trimCandidateMoves(moves, tree, buf_lib, phase);
        return moves;
    }

    if (phase == PhaseKind::WorstPathSurgery) {
        addWorstPathSurgeryMoves(tree, buf_lib, report, chain_lut, moves, seen);
        trimCandidateMoves(moves, tree, buf_lib, phase);
        return moves;
    }

    if (phase == PhaseKind::DownsizeForDelay) {
        addDownsizeForDelayMoves(tree, buf_lib, cells, guide, moves, seen);
        trimCandidateMoves(moves, tree, buf_lib, phase);
        return moves;
    }

    if (phase == PhaseKind::GroupInsertion) {
        addAreaRecoveryMoves(tree, buf_lib, cells, moves, seen);
    }

    if (phase == PhaseKind::WnsRepair) {
        addWnsRepairMoves(tree, buf_lib, report, chain_lut, moves, seen);
    }

    if (phase == PhaseKind::TnsCleanup || phase == PhaseKind::GroupInsertion ||
        phase == PhaseKind::AggressiveSetup) {
        addTnsCleanupFocus(tree, report, guide, node_priority, focus_sinks, focus_nodes, focus_edges);
    }

    std::size_t ss_count = std::min(dynamicTopSetupPaths(report.metrics.nvp_ss), report.ss_violations.size());
    if (phase == PhaseKind::WnsRepair || phase == PhaseKind::UndoHarmfulDownsize ||
        phase == PhaseKind::TnsCleanup ||
        phase == PhaseKind::GroupInsertion || phase == PhaseKind::AggressiveSetup) {
        for (std::size_t i = 0; i < ss_count; ++i) {
            const auto& v = report.ss_violations[i];
            double rank_weight = static_cast<double>(ss_count - i) / std::max<std::size_t>(1, ss_count);
            double phase_boost = (phase == PhaseKind::WnsRepair) ? 5.0 :
                                 (phase == PhaseKind::AggressiveSetup ? 2.4 : 1.0);
            double priority = phase_boost * ((1000.0 * rank_weight) + (100.0 * -v.slack));
            focus_sinks.insert(v.capture);
            node_priority[v.capture] += priority;
            addAncestorsForFocus(tree, v.capture, priority, node_priority, focus_nodes, focus_edges);

            auto launch_it = tree.find(v.launch);
            if (launch_it != tree.end() && launch_it->second.is_sink) {
                double speed_priority = -0.75 * priority;
                focus_sinks.insert(v.launch);
                node_priority[v.launch] += speed_priority;
                addAncestorsForFocus(tree, v.launch, speed_priority, node_priority, focus_nodes, focus_edges);
            }
        }
    }

    std::size_t ff_count = std::min(dynamicTopHoldPaths(report.metrics.nvp_ff), report.ff_violations.size());
    if (phase == PhaseKind::HoldRepair) {
        for (std::size_t i = 0; i < ff_count; ++i) {
            const auto& v = report.ff_violations[i];
            double rank_weight = static_cast<double>(ff_count - i) / std::max<std::size_t>(1, ff_count);
            double priority = (3000.0 * rank_weight) + (500.0 * -v.slack);
            focus_sinks.insert(v.launch);
            focus_sinks.insert(v.capture);
            node_priority[v.launch] += priority;
            node_priority[v.capture] -= 0.8 * priority;
            addAncestorsForFocus(tree, v.launch, priority, node_priority, focus_nodes, focus_edges);
            addAncestorsForFocus(tree, v.capture, -0.8 * priority, node_priority, focus_nodes, focus_edges);
        }
    }

    // Prune focus objects before expanding cells/chains.  The old flow
    // generated tens of thousands of duplicate edge variants and only then
    // trimmed the move list, which could consume a whole short phase.
    const auto nodePriority = [&](const std::string& name) {
        const auto it = node_priority.find(name);
        return it == node_priority.end() ? 0.0 : it->second;
    };
    const auto edgePriority = [&](const std::pair<std::string, std::string>& edge) {
        const auto it = guide.edge_target.find(edge.first + "|" + edge.second);
        const double target = it == guide.edge_target.end() ? 0.0 : it->second;
        return std::abs(nodePriority(edge.second)) + 30000.0 * std::abs(target);
    };
    std::vector<std::string> ranked_sinks(focus_sinks.begin(), focus_sinks.end());
    std::sort(ranked_sinks.begin(), ranked_sinks.end(), [&](const std::string& a, const std::string& b) {
        return std::abs(nodePriority(a)) > std::abs(nodePriority(b));
    });
    const std::size_t sink_limit = phase == PhaseKind::HoldRepair ? 300 : 600;
    if (ranked_sinks.size() > sink_limit) ranked_sinks.resize(sink_limit);

    std::vector<std::string> ranked_nodes(focus_nodes.begin(), focus_nodes.end());
    std::sort(ranked_nodes.begin(), ranked_nodes.end(), [&](const std::string& a, const std::string& b) {
        return std::abs(nodePriority(a)) > std::abs(nodePriority(b));
    });
    if (ranked_nodes.size() > 1200) ranked_nodes.resize(1200);

    std::sort(focus_edges.begin(), focus_edges.end());
    focus_edges.erase(std::unique(focus_edges.begin(), focus_edges.end()), focus_edges.end());
    std::sort(focus_edges.begin(), focus_edges.end(), [&](const auto& a, const auto& b) {
        return edgePriority(a) > edgePriority(b);
    });
    if (focus_edges.size() > 1200) focus_edges.resize(1200);

    if (phase != PhaseKind::UndoHarmfulDownsize) for (const auto& sink : ranked_sinks) {
        auto it = tree.find(sink);
        if (it == tree.end() || !it->second.is_sink || it->second.parent.empty()) continue;
        double priority = std::max(0.0, node_priority[sink]);
        if (priority <= 0.0) continue;
        bool hold_mode = phase == PhaseKind::HoldRepair;
        double target = std::max(0.01, std::abs(guide.sink_target[sink]));
        addChainMovesForEdge(it->second.parent, sink, MoveKind::InsertLeaf, priority,
                             target, hold_mode, chain_lut, 10,
                             hold_mode ? CandidateDirection::LaunchSide
                                       : CandidateDirection::CaptureSide,
                             moves, seen);
    }

    for (const auto& node_name : ranked_nodes) {
        auto node_it = tree.find(node_name);
        if (node_it == tree.end()) continue;
        const ClockNode& node = node_it->second;
        if (node.type == "ROOT" || node.is_sink) continue;

        int fanout = static_cast<int>(node.children.size());
        double priority = node_priority[node.name];
        if (phase == PhaseKind::UndoHarmfulDownsize && priority >= -1e-12) continue;
        double old_avg_delay = 0.5 * (nodeDelay(buf_lib, node.type, fanout, true) +
                                      nodeDelay(buf_lib, node.type, fanout, false));

        for (const auto& cell_name : cells) {
            if (cell_name == node.type) continue;
            const BufferCell& cell = buf_lib.at(cell_name);
            if (fanout > cell.max_fanout) continue;

            double new_avg_delay = 0.5 * (nodeDelay(buf_lib, cell_name, fanout, true) +
                                          nodeDelay(buf_lib, cell_name, fanout, false));
            double delta_delay = new_avg_delay - old_avg_delay;
            if (priority > 0.0 && delta_delay <= 1e-12 && phase != PhaseKind::AreaRecovery) continue;
            if (priority < 0.0 && delta_delay >= -1e-12) continue;
            Move move;
            move.kind = MoveKind::ResizeBuffer;
            move.node = node.name;
            move.first_type = cell_name;
            move.old_type = node.type;
            move.direction = priority > 0.0 ? CandidateDirection::CaptureSide
                                             : CandidateDirection::LaunchSide;
            move.chain_len = 0;
            move.estimate = priority * delta_delay - 0.001 * cell.area;
            pushUniqueMove(move, moves, seen);
        }
    }

    if (phase == PhaseKind::WnsRepair || phase == PhaseKind::TnsCleanup || phase == PhaseKind::GroupInsertion ||
        phase == PhaseKind::AggressiveSetup || phase == PhaseKind::HoldRepair) {
        for (const auto& edge : focus_edges) {
            auto parent_it = tree.find(edge.first);
            auto child_it = tree.find(edge.second);
            if (parent_it == tree.end() || child_it == tree.end()) continue;
            double priority = std::max(0.0, guide.edge_target[edge.first + "|" + edge.second] * 30000.0 +
                                            node_priority[edge.second] * 0.3);
            if (priority <= 0.0) continue;
            double phase_weight = (phase == PhaseKind::WnsRepair) ? 1.8 :
                                  (phase == PhaseKind::TnsCleanup) ? 1.2 : 0.8;
            bool hold_mode = phase == PhaseKind::HoldRepair;
            double target = std::max(0.01, std::abs(guide.edge_target[edge.first + "|" + edge.second]));
            addChainMovesForEdge(edge.first, edge.second, MoveKind::InsertInternal, phase_weight * priority,
                                 target, hold_mode, chain_lut, 8,
                                 hold_mode ? CandidateDirection::LaunchSide
                                           : CandidateDirection::CaptureSide,
                                 moves, seen);
        }
    }

    trimCandidateMoves(moves, tree, buf_lib, phase);
    return moves;
}

bool applyMove(std::unordered_map<std::string, ClockNode>& tree,
               const Move& move,
               const std::unordered_map<std::string, BufferCell>& buf_lib,
               int& next_new_buf_id) {
    if (move.kind == MoveKind::ResizeBuffer) {
        auto node_it = tree.find(move.node);
        auto cell_it = buf_lib.find(move.first_type);
        if (node_it == tree.end() || cell_it == buf_lib.end()) return false;
        ClockNode& node = node_it->second;
        if (node.type == "ROOT" || node.is_sink) return false;
        if (static_cast<int>(node.children.size()) > cell_it->second.max_fanout) return false;
        node.type = move.first_type;
        return checkFanoutLegal(tree, buf_lib);
    }

    if (move.chain_len < 1 || move.chain_len > 10) return false;
    auto parent_it = tree.find(move.parent);
    auto child_it = tree.find(move.child);
    if (parent_it == tree.end() || child_it == tree.end()) return false;

    std::vector<std::string> chain_types;
    if (!move.chain_types.empty()) {
        chain_types = move.chain_types;
    } else {
        chain_types.push_back(move.first_type);
        if (move.chain_len >= 2) chain_types.push_back(move.second_type);
        if (move.chain_len >= 3) chain_types.push_back(move.third_type);
    }
    if (chain_types.size() != static_cast<std::size_t>(move.chain_len)) return false;

    for (const auto& type : chain_types) {
        auto cell = buf_lib.find(type);
        if (cell == buf_lib.end() || cell->second.max_fanout < 1) return false;
    }

    std::vector<std::string> chain_names;
    chain_names.reserve(chain_types.size());
    for (std::size_t i = 0; i < chain_types.size(); ++i) {
        chain_names.push_back(getNextNewBufferName(tree, next_new_buf_id));
    }

    if (!replaceChild(tree, move.parent, move.child, chain_names.front())) return false;

    int base_level = tree[move.parent].level;
    for (std::size_t i = 0; i < chain_names.size(); ++i) {
        ClockNode new_buf;
        new_buf.name = chain_names[i];
        new_buf.type = chain_types[i];
        new_buf.is_sink = false;
        new_buf.parent = (i == 0) ? move.parent : chain_names[i - 1];
        new_buf.level = base_level + static_cast<int>(i) + 1;
        new_buf.children.push_back((i + 1 == chain_names.size()) ? move.child : chain_names[i + 1]);
        tree[chain_names[i]] = new_buf;
    }

    tree[move.child].parent = chain_names.back();
    updateSubtreeLevels(tree, move.child, tree[chain_names.back()].level + 1);

    return checkFanoutLegal(tree, buf_lib);
}

int countInsertedBuffers(const std::unordered_map<std::string, ClockNode>& tree) {
    int count = 0;
    for (const auto& pair : tree) {
        if (pair.first.find("NEW_BUF_") == 0) ++count;
    }
    return count;
}

int moveResizeCount(const Move& move) {
    return move.kind == MoveKind::ResizeBuffer ? 1 : 0;
}

CandidateStats summarizeCandidates(const std::vector<Move>& moves,
                                   const std::unordered_map<std::string, ClockNode>& tree,
                                   const std::unordered_map<std::string, BufferCell>& buf_lib) {
    CandidateStats stats;
    stats.total = moves.size();
    stats.chain_len_count.assign(11, 0);
    for (const auto& move : moves) {
        if (move.kind == MoveKind::ResizeBuffer) {
            auto node_it = tree.find(move.node);
            if (node_it == tree.end()) continue;
            int fanout = static_cast<int>(node_it->second.children.size());
            double old_delay = 0.5 * (nodeDelay(buf_lib, node_it->second.type, fanout, true) +
                                      nodeDelay(buf_lib, node_it->second.type, fanout, false));
            double new_delay = 0.5 * (nodeDelay(buf_lib, move.first_type, fanout, true) +
                                      nodeDelay(buf_lib, move.first_type, fanout, false));
            double old_area = cellArea(buf_lib, node_it->second.type);
            double new_area = cellArea(buf_lib, move.first_type);
            if (new_area < old_area - 1e-12) ++stats.resize_area;
            if (new_delay < old_delay - 1e-12) ++stats.resize_speedup;
            if (new_delay > old_delay + 1e-12) ++stats.resize_delay;
        } else {
            int len = move.chain_len;
            if (len >= 0 && len < static_cast<int>(stats.chain_len_count.size())) {
                ++stats.chain_len_count[len];
            }
        }
    }
    return stats;
}

std::string chainDistributionString(const CandidateStats& stats) {
    std::string out;
    for (std::size_t i = 1; i < stats.chain_len_count.size(); ++i) {
        if (stats.chain_len_count[i] == 0) continue;
        if (!out.empty()) out += ", ";
        out += "L" + std::to_string(i) + "=" + std::to_string(stats.chain_len_count[i]);
    }
    return out.empty() ? "none" : out;
}

void recordRejectedCandidate(SearchResult& result,
                             const Metrics& candidate,
                             const Metrics& base,
                             const Metrics& original) {
    RobustScore cand = evaluateRobustScore(candidate, original);
    RobustScore old = evaluateRobustScore(base, original);
    const bool setup_worse = cand.ss_tns < old.ss_tns - kScoreEps ||
                             cand.ss_wns < old.ss_wns - kScoreEps;
    if (cand.area > old.area + kScoreEps && setup_worse) {
        ++result.rejected_area_hurt_timing;
    } else if (cand.ff_tns < old.ff_tns - 0.05 || cand.ff_wns < old.ff_wns - 0.05) {
        ++result.rejected_hold_collapse;
    } else {
        ++result.rejected_score;
    }
}

bool betterModeState(const ModeState& candidate,
                     const ModeState& current,
                     const Metrics& original) {
    RobustScore cand = evaluateRobustScore(candidate.metrics, original);
    RobustScore base = evaluateRobustScore(current.metrics, original);
    const bool cand_final_safe = cand.ff_tns >= 0.90 && cand.ff_wns >= 0.90 && cand.area >= 0.25;
    const bool base_final_safe = base.ff_tns >= 0.90 && base.ff_wns >= 0.90 && base.area >= 0.25;
    if (cand_final_safe != base_final_safe) return cand_final_safe;
    const double cand_final = finalSelectionScore(cand);
    const double base_final = finalSelectionScore(base);
    if (cand_final > base_final + kScoreEps) return true;
    if (cand_final < base_final - kScoreEps) return false;
    return candidate.metrics.area < current.metrics.area - 1e-9;
}

void printModeComparison(const std::vector<ModeState>& modes,
                         const ModeState& selected,
                         const Metrics& original,
                         OptimizationLogger& log) {
    std::ostringstream table;
    table << "========== Mode Comparison: testcase0 ==========" << "\n"
          << std::left << std::setw(20) << "mode"
          << std::right << std::setw(10) << "SS_TNS" << std::setw(10) << "SS_WNS"
          << std::setw(10) << "FF_TNS" << std::setw(10) << "FF_WNS"
          << std::setw(10) << "AREA" << std::setw(16) << "timing_score"
          << std::setw(16) << "robust_score" << std::setw(12) << "runtime" << "\n";
    auto row = [&](const std::string& name, const Metrics& metrics, double runtime_sec) {
        const RobustScore score = evaluateRobustScore(metrics, original);
        table << std::left << std::setw(20) << name << std::right << std::fixed << std::setprecision(4)
              << std::setw(10) << score.ss_tns << std::setw(10) << score.ss_wns
              << std::setw(10) << score.ff_tns << std::setw(10) << score.ff_wns
              << std::setw(10) << score.area << std::setw(16) << score.timing_score
              << std::setw(16) << score.average_robust << std::setw(11)
              << std::setprecision(1) << runtime_sec << "s\n";
    };
    for (const ModeState& mode : modes) row(mode.name, mode.metrics, mode.runtime_sec);
    row("selected_best", selected.metrics, selected.runtime_sec);
    table << "=====================================";
    std::cout << table.str() << "\n";
    log.line("\n```\n" + table.str() + "\n```");
}

SearchResult searchPhaseCandidates(
        const std::string& root_name,
        const std::unordered_map<std::string, ClockNode>& current_tree,
        const std::unordered_map<std::string, BufferCell>& buf_lib,
        const std::vector<DataPath>& ss_paths,
        const std::vector<DataPath>& ff_paths,
        double clock_period,
        const Metrics& current_metrics,
        const Metrics& original,
        const std::vector<Move>& moves,
        PhaseKind phase,
        std::size_t exact_limit,
        double phase_end_sec,
        int next_new_buf_id,
        const std::chrono::steady_clock::time_point& start_time) {
    SearchResult result;
    result.next_new_buf_id = next_new_buf_id;

    std::unordered_map<std::string, ClockNode> working_tree = current_tree;
    Metrics working_metrics = current_metrics;
    int working_next_new_buf_id = next_new_buf_id;
    // Keep the checker-safety margin established by hold repair through every
    // later cleanup phase.  Otherwise a nominally zero-hold balanced/area move
    // can consume the margin and reintroduce tiny checker-visible violations.
    const bool preserve_hold_margin = phase == PhaseKind::HoldRepair ||
                                      phase == PhaseKind::GroupInsertion ||
                                      phase == PhaseKind::AreaRecovery;
    const double hold_guard = preserve_hold_margin ? 0.004 : 0.0;
    TimingEngine current_engine(root_name, current_tree, buf_lib, ss_paths, ff_paths,
                                clock_period, hold_guard);
    if (preserve_hold_margin) {
        working_metrics = current_engine.computeCurrentMetrics();
    }
    const std::size_t chunk_size = phase == PhaseKind::DownsizeForDelay ? 1 : 96;
    const int max_commits = phase == PhaseKind::DownsizeForDelay
                          ? static_cast<int>(exact_limit) : 96;
    std::size_t cursor = 0;
    int commits = 0;
    Move first_move;

    while (cursor < moves.size() && result.evaluated < exact_limit &&
           commits < max_commits && elapsedSec(start_time) < phase_end_sec &&
           !isTimeLimitReached(start_time)) {
        bool chunk_found = false;
        Move chunk_move;
        Metrics chunk_metrics;
        const std::size_t chunk_end = std::min(moves.size(), cursor + chunk_size);

        for (; cursor < chunk_end && result.evaluated < exact_limit; ++cursor) {
            MoveEvaluation eval = evaluateCandidateMove(current_engine, moves[cursor], original);
            if (!eval.legal) {
                ++result.rejected_illegal;
                continue;
            }
            ++result.evaluated;
            if (!acceptableForPhase(phase, eval.metrics, working_metrics, original)) {
                recordRejectedCandidate(result, eval.metrics, working_metrics, original);
                continue;
            }
            if (!chunk_found || betterCandidateForPhase(phase, eval.metrics, chunk_metrics, original)) {
                chunk_found = true;
                chunk_move = moves[cursor];
                chunk_metrics = eval.metrics;
            }
        }

        if (!chunk_found) continue;
        if (!applyMove(working_tree, chunk_move, buf_lib, working_next_new_buf_id)) {
            ++result.rejected_illegal;
            continue;
        }
        if (commits == 0) first_move = chunk_move;
        ++commits;
        result.resize_count += moveResizeCount(chunk_move);
        if (chunk_move.kind == MoveKind::ResizeBuffer) {
            if (!current_engine.commitResizeMove(chunk_move.node, chunk_move.first_type)) {
                current_engine.updateTiming(working_tree);
            }
        } else {
            current_engine.updateTiming(working_tree);
        }
        working_metrics = current_engine.computeCurrentMetrics();

        result.found = true;
        result.first_move = first_move;
        result.metrics = working_metrics;
        result.tree = working_tree;
        result.next_new_buf_id = working_next_new_buf_id;
        result.sequence_len = commits;
    }

    if (result.found) {
        TimingReport exact_report = analyzeTiming(root_name, result.tree, buf_lib,
                                                  ss_paths, ff_paths, clock_period);
        result.metrics = exact_report.metrics;
    }

    return result;
}

[[maybe_unused]] bool runAreaRecoverySweep(const std::string& root_name,
                          std::unordered_map<std::string, ClockNode>& current_tree,
                          const std::unordered_map<std::string, BufferCell>& buf_lib,
                          const std::vector<DataPath>& ss_paths,
                          const std::vector<DataPath>& ff_paths,
                          double clock_period,
                          Metrics& current_metrics,
                          const Metrics& original,
                          int& next_new_buf_id,
                          const std::chrono::steady_clock::time_point& start_time,
                          double phase_end_sec,
                          OptimizationLogger& log) {
    TimingReport report = analyzeTiming(root_name, current_tree, buf_lib,
                                        ss_paths, ff_paths, clock_period);
    const std::vector<ChainCandidate> chain_lut =
        buildChainLookup(sortedCellNames(buf_lib), buf_lib);
    std::vector<Move> moves = generateCandidateMoves(current_tree, buf_lib, report,
                                                     ss_paths, ff_paths, clock_period,
                                                     PhaseKind::AreaRecovery, chain_lut);
    if (moves.empty()) return false;
    SearchResult area_result = searchPhaseCandidates(
        root_name, current_tree, buf_lib, ss_paths, ff_paths, clock_period,
        report.metrics, original, moves, PhaseKind::AreaRecovery, 256,
        phase_end_sec, next_new_buf_id, start_time);
    if (!area_result.found) return false;
    current_tree = area_result.tree;
    current_metrics = area_result.metrics;
    next_new_buf_id = area_result.next_new_buf_id;
    log.line("- Safe area sweep accepted: area " + std::to_string(current_metrics.area) +
             ", SS " + std::to_string(current_metrics.tns_ss) + "/" +
             std::to_string(current_metrics.wns_ss) + "/" +
             std::to_string(current_metrics.nvp_ss) + ", FF " +
             std::to_string(current_metrics.tns_ff) + "/" +
             std::to_string(current_metrics.wns_ff) + "/" +
             std::to_string(current_metrics.nvp_ff) + ".");
    std::cout << "[OPT] Safe area sweep accepted"
              << " | SS TNS/WNS/NVP: " << current_metrics.tns_ss << "/" << current_metrics.wns_ss << "/" << current_metrics.nvp_ss
              << " | FF TNS/WNS/NVP: " << current_metrics.tns_ff << "/" << current_metrics.wns_ff << "/" << current_metrics.nvp_ff
              << " | Area: " << current_metrics.area << "\n";
    return true;
}
}

Optimizer::Optimizer(const std::string& r_name,
                     const std::unordered_map<std::string, ClockNode>& tree,
                     const std::unordered_map<std::string, BufferCell>& lib,
                     const std::vector<DataPath>& ss_p,
                     const std::vector<DataPath>& ff_p,
                     double period)
    : root_name(r_name), clock_tree(tree), buf_lib(lib), ss_paths(ss_p), ff_paths(ff_p), clock_period(period) {
    
    double min_area = 1e9;
    double max_area = -1.0;
    for (const auto& pair : buf_lib) {
        if (pair.second.area < min_area) {
            min_area = pair.second.area;
            small_buffer = pair.first;
        }
        if (pair.second.area > max_area) {
            max_area = pair.second.area;
            large_buffer = pair.first;
        }
    }
}

double Optimizer::computeScore(double tns_ss, double wns_ss, double tns_ff, double wns_ff, double area,
                               double ori_tns_ss, double ori_wns_ss, double ori_tns_ff, double ori_wns_ff, double ori_area) {
    Metrics m;
    m.tns_ss = tns_ss;
    m.wns_ss = wns_ss;
    m.tns_ff = tns_ff;
    m.wns_ff = wns_ff;
    m.area = area;

    Metrics original;
    original.tns_ss = ori_tns_ss;
    original.wns_ss = ori_wns_ss;
    original.tns_ff = ori_tns_ff;
    original.wns_ff = ori_wns_ff;
    original.area = ori_area;
    return evaluateRobustScore(m, original).robust;
}

void Optimizer::runOptimization(std::unordered_map<std::string, ClockNode>& best_tree) {
    const auto start_time = std::chrono::steady_clock::now();
    OptimizationLogger log;

    TimingReport original_report = analyzeTiming(root_name, clock_tree, buf_lib,
                                                 ss_paths, ff_paths, clock_period);
    Metrics original = original_report.metrics;
    const std::vector<PhaseConfig> phases = buildAdaptivePhases(original, clock_tree.size(), ss_paths.size());
    // Cell-library chain options never change during a run.  Building this
    // Pareto table once avoids repeating its quadratic pruning in every round.
    const std::vector<ChainCandidate> chain_lut =
        buildChainLookup(sortedCellNames(buf_lib), buf_lib);
    Metrics best_metrics = original;
    Metrics current_metrics = original;
    double best_score = evaluateRobustScore(best_metrics, original).robust;

    best_tree = clock_tree;
    std::unordered_map<std::string, ClockNode> current_tree = clock_tree;
    int next_new_buf_id = findNextNewBufferId(current_tree);
    int no_improve_rounds = 0;
    int applied_moves = 0;
    int resize_moves = 0;
    bool aggressive_worsened_hold = false;
    bool hold_recovered_after_aggressive = false;
    std::vector<ModeState> mode_states;
    ModeState baseline_state;
    baseline_state.name = "baseline";
    baseline_state.metrics = original;
    baseline_state.tree = clock_tree;
    baseline_state.score_terms = TimingEngine::computeScoreTerms(original, original);
    baseline_state.runtime_sec = elapsedSec(start_time);
    mode_states.push_back(std::move(baseline_state));

    log.line("## Baseline");
    log.line("- Adaptive schedule selected from initial node/path/violation counts.");
    log.line("- Baseline SS TNS/WNS/NVP: " + std::to_string(original.tns_ss) + "/" +
             std::to_string(original.wns_ss) + "/" + std::to_string(original.nvp_ss));
    log.line("- Baseline FF TNS/WNS/NVP: " + std::to_string(original.tns_ff) + "/" +
             std::to_string(original.wns_ff) + "/" + std::to_string(original.nvp_ff));
    log.line("- Baseline area: " + std::to_string(original.area));
    log.line("- Initial nodes/setup paths: " + std::to_string(clock_tree.size()) + "/" +
             std::to_string(ss_paths.size()));

    RobustScore original_score = evaluateRobustScore(original, original);
    std::cout << "[OPT] Initial robust score: " << best_score
              << " | setup/hold/area score: "
              << original_score.setup << "/" << original_score.hold << "/" << original_score.area
              << " | SS TNS/WNS/NVP: " << original.tns_ss << "/" << original.wns_ss << "/" << original.nvp_ss
              << " | FF TNS/WNS/NVP: " << original.tns_ff << "/" << original.wns_ff << "/" << original.nvp_ff
              << " | Area: " << original.area << "\n";
    reportScoreTerms("initial score terms", original, original, log);
    std::cout << "========== testcase0 Optimization Focus ==========\n"
              << "Current bottleneck: setup WNS\n"
              << "SS_TNS_term = " << original_score.ss_tns << "\n"
              << "SS_WNS_term = " << original_score.ss_wns << "\n"
              << "FF_TNS_term = " << original_score.ff_tns << "\n"
              << "FF_WNS_term = " << original_score.ff_wns << "\n"
              << "AREA_term   = " << original_score.area << "\n"
              << "==================================================\n";
    std::cout << "[OPT] Adaptive schedule selected"
              << " | nodes: " << clock_tree.size()
              << " | setup paths: " << ss_paths.size()
              << " | setup NVP: " << original.nvp_ss << "\n";
    for (const PhaseConfig& phase : phases) {
        std::cout << "[OPT]   " << phase.name << ": "
                  << phase.start_sec << "-" << phase.end_sec
                  << " sec, exact limit " << phase.exact_limit << "\n";
        log.line("- Phase " + std::string(phase.name) + ": " +
                 std::to_string(phase.start_sec) + "-" +
                 std::to_string(phase.end_sec) + " sec, exact limit " +
                 std::to_string(phase.exact_limit));
    }
    printTopViolations(original_report, clock_tree);
    std::cout.flush();

    int global_iter = 0;
    for (const PhaseConfig& phase : phases) {
        while (elapsedSec(start_time) < phase.start_sec && !isTimeLimitReached(start_time)) {
            break;
        }
        if (isTimeLimitReached(start_time)) break;

        no_improve_rounds = 0;
        const Metrics phase_start_metrics = current_metrics;
        PathViolation surgery_worst_before;
        bool have_surgery_worst = false;
        DelayTable surgery_start_delays;
        Move surgery_best_move;
        bool have_surgery_move = false;
        if (phase.kind == PhaseKind::WorstPathSurgery) {
            TimingReport start_report = analyzeTiming(root_name, current_tree, buf_lib,
                                                      ss_paths, ff_paths, clock_period);
            if (!start_report.ss_violations.empty()) {
                surgery_worst_before = start_report.ss_violations.front();
                surgery_start_delays = computeDelays(root_name, current_tree, buf_lib);
                have_surgery_worst = true;
            }
        }
        std::size_t phase_generated = 0;
        std::size_t phase_evaluated = 0;
        std::size_t phase_rejected_hold = 0;
        std::size_t phase_rejected_area_timing = 0;
        std::size_t phase_rejected_score = 0;
        std::size_t phase_rejected_illegal = 0;
        std::cout << "[OPT] === Phase: " << phase.name
                  << " (" << phase.start_sec << "-" << phase.end_sec
                  << " sec, exact limit " << phase.exact_limit << ") ===\n";
        log.line("\n## Phase: " + std::string(phase.name));
        log.line("- Time window: " + std::to_string(phase.start_sec) + "-" +
                 std::to_string(phase.end_sec) + " sec");
        reportScoreTerms("before " + std::string(phase.name), current_metrics, original, log);
        std::cout.flush();

        for (int phase_iter = 0;
             elapsedSec(start_time) < phase.end_sec - 1.0 && !isTimeLimitReached(start_time);
             ++phase_iter, ++global_iter) {
            if (phase.kind == PhaseKind::GroupInsertion || phase.kind == PhaseKind::AreaRecovery) {
                const RobustScore terms = evaluateRobustScore(current_metrics, original);
                const bool timing_strong = terms.ss_tns >= 0.80 && terms.ss_wns >= 0.70 &&
                                           terms.ff_tns >= 0.80 && terms.ff_wns >= 0.80;
                if (!timing_strong) {
                    std::cout << "[OPT] " << phase.name
                              << " skipped: timing terms are not yet strong enough for cleanup\n";
                    log.line("- Cleanup skipped because normalized timing terms are below the safety gate.");
                    break;
                }
            }
            TimingReport current_report = analyzeTiming(root_name, current_tree, buf_lib,
                                                        ss_paths, ff_paths, clock_period);
            current_metrics = current_report.metrics;
            std::vector<Move> moves = generateCandidateMoves(current_tree, buf_lib,
                                                             current_report, ss_paths, ff_paths,
                                                             clock_period, phase.kind, chain_lut);
            if (moves.empty()) {
                std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                          << ": no legal candidates.\n";
                log.line("- Iteration " + std::to_string(phase_iter) + ": no legal candidates.");
                break;
            }
            phase_generated += moves.size();

            CandidateStats cand_stats = summarizeCandidates(moves, current_tree, buf_lib);

            std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                      << " | current SS TNS/WNS/NVP: "
                      << current_metrics.tns_ss << "/" << current_metrics.wns_ss << "/" << current_metrics.nvp_ss
                      << " | FF TNS/WNS/NVP: "
                      << current_metrics.tns_ff << "/" << current_metrics.wns_ff << "/" << current_metrics.nvp_ff
                      << " | candidates: " << moves.size()
                      << " | chains: " << chainDistributionString(cand_stats)
                      << " | resize speed/delay/area: "
                      << cand_stats.resize_speedup << "/" << cand_stats.resize_delay << "/" << cand_stats.resize_area
                      << " | exact limit: " << phase.exact_limit << "\n";
            std::cout.flush();
            log.line("- Iteration " + std::to_string(phase_iter) +
                     ": generated " + std::to_string(moves.size()) +
                     " candidates; chains " + chainDistributionString(cand_stats) +
                     "; resize speed/delay/area " +
                     std::to_string(cand_stats.resize_speedup) + "/" +
                     std::to_string(cand_stats.resize_delay) + "/" +
                     std::to_string(cand_stats.resize_area) + ".");

            SearchResult search_result = searchPhaseCandidates(
                root_name, current_tree, buf_lib, ss_paths, ff_paths, clock_period,
                current_metrics, original, moves, phase.kind, phase.exact_limit,
                phase.end_sec, next_new_buf_id, start_time);
            phase_evaluated += search_result.evaluated;
            phase_rejected_hold += search_result.rejected_hold_collapse;
            phase_rejected_area_timing += search_result.rejected_area_hurt_timing;
            phase_rejected_score += search_result.rejected_score;
            phase_rejected_illegal += search_result.rejected_illegal;

            if (!search_result.found) {
                std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                          << " | evaluated: " << search_result.evaluated
                          << " | no acceptable phase candidate"
                          << " | rejected(area-hurt-timing/hold-collapse/score/illegal): "
                          << search_result.rejected_area_hurt_timing << "/"
                          << search_result.rejected_hold_collapse << "/"
                          << search_result.rejected_score << "/"
                          << search_result.rejected_illegal
                          << " | elapsed: " << elapsedSec(start_time) << " sec\n";
                log.line("- Iteration " + std::to_string(phase_iter) +
                         ": evaluated " + std::to_string(search_result.evaluated) +
                         ", no acceptable candidate.");
                ++no_improve_rounds;
                if (no_improve_rounds >= phase.max_no_improve_rounds) break;
                continue;
            }

            std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                      << " | evaluated: " << search_result.evaluated
                      << " | sequence length: " << search_result.sequence_len
                      << " | best SS TNS/WNS/NVP: "
                      << search_result.metrics.tns_ss << "/" << search_result.metrics.wns_ss << "/" << search_result.metrics.nvp_ss
                      << " | FF TNS/WNS/NVP: "
                      << search_result.metrics.tns_ff << "/" << search_result.metrics.wns_ff << "/" << search_result.metrics.nvp_ff
                      << " | robust score: " << evaluateRobustScore(search_result.metrics, original).robust
                      << " | timing score: " << evaluateRobustScore(search_result.metrics, original).timing_score
                      << " | rejected(area-hurt-timing/hold-collapse/score/illegal): "
                      << search_result.rejected_area_hurt_timing << "/"
                      << search_result.rejected_hold_collapse << "/"
                      << search_result.rejected_score << "/"
                      << search_result.rejected_illegal
                      << " | area: " << search_result.metrics.area
                      << " | elapsed: " << elapsedSec(start_time) << " sec\n";

            RobustScore before_terms = evaluateRobustScore(current_metrics, original);
            RobustScore accepted_terms = evaluateRobustScore(search_result.metrics, original);
            bool weakest_improved = weakestScoreTerm(accepted_terms, original) >
                                    weakestScoreTerm(before_terms, original) + kScoreEps;
            reportAcceptedMove(search_result.first_move, current_metrics,
                               search_result.metrics, original, log);
            if (phase.kind == PhaseKind::WorstPathSurgery) {
                surgery_best_move = search_result.first_move;
                have_surgery_move = true;
            }
            std::cout << "[SCORE TERMS] progress mode: "
                      << (weakest_improved ? "improving the weakest term" : "improving timing-first normalized score")
                      << "\n";
            log.line(std::string("  - Progress mode: ") +
                     (weakest_improved ? "improving the weakest term." : "improving timing-first normalized score."));

            log.line("- Iteration " + std::to_string(phase_iter) +
                     ": accepted " + std::string(phaseName(phase.kind)) +
                     " sequence; evaluated " + std::to_string(search_result.evaluated) +
                     "; sequence length " + std::to_string(search_result.sequence_len) +
                     "; SS TNS/WNS/NVP " + std::to_string(search_result.metrics.tns_ss) + "/" +
                     std::to_string(search_result.metrics.wns_ss) + "/" +
                     std::to_string(search_result.metrics.nvp_ss) +
                     "; FF TNS/WNS/NVP " + std::to_string(search_result.metrics.tns_ff) + "/" +
                     std::to_string(search_result.metrics.wns_ff) + "/" +
                     std::to_string(search_result.metrics.nvp_ff) +
                     "; area " + std::to_string(search_result.metrics.area) + ".");

            current_tree = search_result.tree;
            current_metrics = search_result.metrics;
            applied_moves += search_result.sequence_len;
            resize_moves += search_result.resize_count;
            next_new_buf_id = search_result.next_new_buf_id;
            no_improve_rounds = 0;

            if (betterOutputCandidate(current_metrics, best_metrics, original)) {
                best_metrics = current_metrics;
                best_tree = current_tree;
                best_score = evaluateRobustScore(best_metrics, original).robust;
                log.line("  - Updated best_tree.");
            }

            if (phase.kind == PhaseKind::WorstPathSurgery ||
                phase.kind == PhaseKind::WnsRepair ||
                phase.kind == PhaseKind::UndoHarmfulDownsize ||
                phase.kind == PhaseKind::TnsCleanup) {
                if (current_metrics.wns_ff < original.wns_ff - 1e-12 ||
                    current_metrics.tns_ff < original.tns_ff - 1e-12) {
                    aggressive_worsened_hold = true;
                }
            }
        }

        if (phase.kind == PhaseKind::HoldRepair && aggressive_worsened_hold) {
            hold_recovered_after_aggressive =
                current_metrics.wns_ff >= original.wns_ff - 1e-12 &&
                current_metrics.tns_ff >= original.tns_ff - 1e-12;
        }

        std::string phase_label = "after " + std::string(phase.name);
        reportScoreTerms(phase_label, current_metrics, original, log);
        if (phase.kind == PhaseKind::WorstPathSurgery ||
            phase.kind == PhaseKind::WnsRepair || phase.kind == PhaseKind::UndoHarmfulDownsize) {
            const RobustScore before = evaluateRobustScore(phase_start_metrics, original);
            const RobustScore after = evaluateRobustScore(current_metrics, original);
            if (after.ss_wns <= before.ss_wns + kScoreEps) {
                std::cout << "[WNS DIAGNOSTIC] no SS_WNS improvement"
                          << " | generated=" << phase_generated
                          << " exact=" << phase_evaluated
                          << " rejected hold/area-score/objective/illegal="
                          << phase_rejected_hold << "/" << phase_rejected_area_timing << "/"
                          << phase_rejected_score << "/" << phase_rejected_illegal
                          << " | likely cause: candidates exhausted or delay step did not move the global worst path\n";
            }
        }
        if (phase.kind == PhaseKind::WorstPathSurgery && have_surgery_worst) {
            const RobustScore before = evaluateRobustScore(phase_start_metrics, original);
            const RobustScore after = evaluateRobustScore(current_metrics, original);
            const double launch_clk = surgery_start_delays.ss[surgery_worst_before.launch];
            const double capture_clk = surgery_start_delays.ss[surgery_worst_before.capture];
            std::cout << "========== Worst Setup Path Surgery Summary ==========\n"
                      << "worst path before:\n"
                      << "path = " << surgery_worst_before.name << "\n"
                      << "launch = " << surgery_worst_before.launch << "\n"
                      << "capture = " << surgery_worst_before.capture << "\n"
                      << "SS data delay = " << surgery_worst_before.delay << "\n"
                      << "launch clock arrival = " << launch_clk << "\n"
                      << "capture clock arrival = " << capture_clk << "\n"
                      << "SS slack = " << surgery_worst_before.slack << "\n"
                      << "SS_WNS_term = " << before.ss_wns << "\n"
                      << "required skew increase = " << -surgery_worst_before.slack << "\n\n"
                      << "best accepted targeted sequence:\n"
                      << "move = " << (have_surgery_move ? moveDescription(surgery_best_move) : "none") << "\n"
                      << "SS_WNS_term old -> new = " << before.ss_wns << " -> " << after.ss_wns << "\n"
                      << "SS_TNS_term old -> new = " << before.ss_tns << " -> " << after.ss_tns << "\n"
                      << "FF_WNS_term old -> new = " << before.ff_wns << " -> " << after.ff_wns << "\n"
                      << "FF_TNS_term old -> new = " << before.ff_tns << " -> " << after.ff_tns << "\n"
                      << "AREA_term old -> new = " << before.area << " -> " << after.area << "\n"
                      << "accepted/rejected = " << (have_surgery_move ? "accepted" : "rejected") << "\n"
                      << "reason = " << (have_surgery_move
                              ? "exact WNS improvement or WNS-neutral worst-cluster peeling"
                              : "no targeted candidate passed exact budgets") << "\n"
                      << "======================================================\n";
        }

        const char* mode_name = nullptr;
        if (phase.kind == PhaseKind::WorstPathSurgery) mode_name = "setup_worst_path_surgery";
        else if (phase.kind == PhaseKind::WnsRepair) mode_name = "setup_WNS_breakthrough";
        else if (phase.kind == PhaseKind::UndoHarmfulDownsize) mode_name = "setup_WNS_breakthrough_launch_speedup";
        else if (phase.kind == PhaseKind::TnsCleanup) mode_name = "setup_TNS_cleanup";
        else if (phase.kind == PhaseKind::DownsizeForDelay) mode_name = "setup_WNS_breakthrough_seed";
        else if (phase.kind == PhaseKind::HoldRepair) mode_name = "strong_hold_repair";
        else if (phase.kind == PhaseKind::GroupInsertion) mode_name = "safe_area_cleanup";
        if (mode_name) {
            ModeState state;
            state.name = mode_name;
            state.metrics = current_metrics;
            state.tree = current_tree;
            state.score_terms = TimingEngine::computeScoreTerms(current_metrics, original);
            state.runtime_sec = elapsedSec(start_time);
            if (phase.kind == PhaseKind::WorstPathSurgery ||
                phase.kind == PhaseKind::WnsRepair ||
                phase.kind == PhaseKind::UndoHarmfulDownsize ||
                phase.kind == PhaseKind::TnsCleanup) {
                state.mode = OptimizationMode::TimingAggressive;
            } else if (phase.kind == PhaseKind::DownsizeForDelay) {
                state.mode = OptimizationMode::DownsizeForDelay;
            } else if (phase.kind == PhaseKind::HoldRepair) {
                state.mode = OptimizationMode::HoldRepair;
            } else {
                state.mode = OptimizationMode::BalancedCleanup;
            }
            mode_states.push_back(std::move(state));
        }
    }

    const std::array<const char*, 5> required_modes{{
        "setup_WNS_breakthrough_seed", "setup_worst_path_surgery",
        "setup_TNS_cleanup", "strong_hold_repair", "safe_area_cleanup"
    }};
    for (const char* required : required_modes) {
        bool found = false;
        for (const ModeState& state : mode_states) {
            if (state.name == required) { found = true; break; }
        }
        if (!found) {
            const Metrics fallback_metrics = mode_states.empty() ? current_metrics : mode_states.back().metrics;
            const auto& fallback_tree = mode_states.empty() ? current_tree : mode_states.back().tree;
            ModeState fallback;
            fallback.name = required;
            fallback.metrics = fallback_metrics;
            fallback.tree = fallback_tree;
            fallback.score_terms = TimingEngine::computeScoreTerms(fallback_metrics, original);
            fallback.runtime_sec = elapsedSec(start_time);
            mode_states.push_back(std::move(fallback));
        }
    }
    ModeState selected = mode_states.front();
    for (std::size_t i = 1; i < mode_states.size(); ++i) {
        if (betterModeState(mode_states[i], selected, original)) selected = mode_states[i];
    }
    best_metrics = selected.metrics;
    best_tree = selected.tree;
    best_score = evaluateRobustScore(best_metrics, original).robust;
    printModeComparison(mode_states, selected, original, log);

    RobustScore final_score = evaluateRobustScore(best_metrics, original);
    reportScoreTerms("final selected solution (" + selected.name + ")", best_metrics, original, log);
    log.line("\n## Final");
    log.line("- Final best robust score: " + std::to_string(best_score));
    log.line("- Final SS TNS/WNS/NVP: " + std::to_string(best_metrics.tns_ss) + "/" +
             std::to_string(best_metrics.wns_ss) + "/" + std::to_string(best_metrics.nvp_ss));
    log.line("- Final FF TNS/WNS/NVP: " + std::to_string(best_metrics.tns_ff) + "/" +
             std::to_string(best_metrics.wns_ff) + "/" + std::to_string(best_metrics.nvp_ff));
    log.line("- Final area: " + std::to_string(best_metrics.area));
    log.line("- Applied moves: " + std::to_string(applied_moves) +
             "; inserted buffers: " + std::to_string(countInsertedBuffers(best_tree)) +
             "; resize moves: " + std::to_string(resize_moves));
    log.line("- Aggressive setup worsened hold temporarily: " +
             std::string(aggressive_worsened_hold ? "yes" : "no") +
             "; hold repair recovered: " +
             std::string(hold_recovered_after_aggressive ? "yes" : "no") + ".");
    std::cout << "[OPT] Final best robust score: " << best_score
              << " | setup/hold/area score: "
              << final_score.setup << "/" << final_score.hold << "/" << final_score.area
              << " | SS TNS/WNS/NVP: " << best_metrics.tns_ss << "/" << best_metrics.wns_ss << "/" << best_metrics.nvp_ss
              << " | FF TNS/WNS/NVP: " << best_metrics.tns_ff << "/" << best_metrics.wns_ff << "/" << best_metrics.nvp_ff
              << " | Area: " << best_metrics.area << "\n";
    std::cout << "[OPT] Applied moves: " << applied_moves
              << " | inserted buffers: " << countInsertedBuffers(best_tree)
              << " | resize moves: " << resize_moves << "\n";
    std::cout << "[OPT] Aggressive setup hold temporary damage: "
              << (aggressive_worsened_hold ? "yes" : "no")
              << " | hold repair recovered: "
              << (hold_recovered_after_aggressive ? "yes" : "no") << "\n";
}
