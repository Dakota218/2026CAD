#include "optimizer.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <fstream>
#include <iomanip>

namespace {
const double kOptimizationTimeLimitSec = 570.0;
const std::size_t kTopSetupPaths = 50;
const std::size_t kTopHoldPaths = 20;
const std::size_t kWnsRepairSetupPaths = 10;
const std::size_t kTnsCleanupSetupPaths = 50000;
const std::size_t kTnsCleanupSinkLimit = 1500;
const std::size_t kMaxCandidatesPerIteration = 50000;
const std::size_t kMaxExactEvaluationsPerIteration = 8192;
const int kMaxNoImproveRounds = 2;
const double kScoreEps = 1e-12;

enum class PhaseKind {
    WnsRepair,
    TnsCleanup,
    GroupInsertion,
    HoldRepair,
    AreaRecovery
};

enum class MoveKind {
    InsertLeaf,
    ResizeBuffer,
    InsertInternal
};

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

struct Metrics {
    double tns_ss = 0.0;
    double wns_ss = 0.0;
    int nvp_ss = 0;
    double tns_ff = 0.0;
    double wns_ff = 0.0;
    int nvp_ff = 0;
    double area = 0.0;
};

struct TimingReport {
    Metrics metrics;
    std::vector<PathViolation> ss_violations;
    std::vector<PathViolation> ff_violations;
};

struct RobustScore {
    double setup = 0.0;
    double hold = 0.0;
    double area = 0.0;
    double robust = 0.0;
};

struct Move {
    MoveKind kind = MoveKind::ResizeBuffer;
    std::string parent;
    std::string child;
    std::string node;
    std::string first_type;
    std::string second_type;
    std::string third_type;
    int chain_len = 0;
    double estimate = 0.0;
};

struct OptimizationLogger {
    std::ofstream out;

    OptimizationLogger() : out("optimization_log.md", std::ios::out) {
        if (out) {
            out << "# Optimization log\n\n";
            out << "Priority order: setup timing first, then hold timing, then area.\n\n";
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
        case PhaseKind::WnsRepair: return "WNS repair";
        case PhaseKind::TnsCleanup: return "TNS/NVP cleanup";
        case PhaseKind::GroupInsertion: return "group/ancestor insertion";
        case PhaseKind::HoldRepair: return "hold final repair";
        case PhaseKind::AreaRecovery: return "area recovery";
    }
    return "unknown";
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

void printTopViolations(const TimingReport& report) {
    std::cout << "[VIOLATION] Top " << kTopSetupPaths << " SS setup violated paths:\n";
    std::size_t ss_count = std::min(kTopSetupPaths, report.ss_violations.size());
    for (std::size_t i = 0; i < ss_count; ++i) {
        const auto& v = report.ss_violations[i];
        std::cout << "  SS#" << i
                  << " slack=" << v.slack
                  << " launch=" << v.launch
                  << " capture=" << v.capture
                  << " data=" << v.delay
                  << " skew=" << v.skew << "\n";
    }

    std::cout << "[VIOLATION] Top " << kTopHoldPaths << " FF hold violated paths:\n";
    std::size_t ff_count = std::min(kTopHoldPaths, report.ff_violations.size());
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

double timingScoreTerm(double current, double original) {
    const double eps = 1e-12;
    if (original < -eps) {
        return 1.0 - current / original;
    }
    if (current >= -eps) {
        return 0.0;
    }
    return -1000.0 * (-current);
}

RobustScore evaluateRobustScore(const Metrics& m, const Metrics& original) {
    static const double alphas[] = {0.36, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90};

    RobustScore score;
    score.setup = timingScoreTerm(m.tns_ss, original.tns_ss) +
                  timingScoreTerm(m.wns_ss, original.wns_ss);
    score.hold = timingScoreTerm(m.tns_ff, original.tns_ff) +
                 timingScoreTerm(m.wns_ff, original.wns_ff);
    score.area = original.area > 0.0 ? (1.0 - m.area / original.area) : 0.0;
    score.robust = std::numeric_limits<double>::infinity();

    for (double alpha : alphas) {
        double beta = (1.0 - alpha) / 2.0;
        double gamma = beta;
        double weighted = alpha * score.setup + beta * score.hold + gamma * score.area;
        score.robust = std::min(score.robust, weighted);
    }

    return score;
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
    if (cand_score.robust <= base_score.robust + kScoreEps) return false;
    if (setupSeverelyDegrades(cand, base, original)) return false;
    if (holdSeverelyDegrades(cand, base, original)) return false;
    if (areaIncreaseIsUnjustified(cand, base, original)) return false;
    return true;
}

bool betterCandidateOrder(const Metrics& cand, const Metrics& base, const Metrics& original) {
    RobustScore cand_score = evaluateRobustScore(cand, original);
    RobustScore base_score = evaluateRobustScore(base, original);

    if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
    if (cand.wns_ss < base.wns_ss - kScoreEps) return false;

    if (cand.nvp_ss < base.nvp_ss) return true;
    if (cand.nvp_ss > base.nvp_ss) return false;

    if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
    if (cand.tns_ss < base.tns_ss - kScoreEps) return false;

    if (cand_score.robust > base_score.robust + kScoreEps) return true;
    if (cand_score.robust < base_score.robust - kScoreEps) return false;

    if (!holdSeverelyDegrades(cand, base, original) && holdSeverelyDegrades(base, cand, original)) return true;
    if (holdSeverelyDegrades(cand, base, original) && !holdSeverelyDegrades(base, cand, original)) return false;

    if (cand.area < base.area - 1e-9) return true;
    if (cand.area > base.area + 1e-9) return false;

    return cand.nvp_ss + cand.nvp_ff < base.nvp_ss + base.nvp_ff;
}

bool setupImproves(const Metrics& cand, const Metrics& base) {
    if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
    if (cand.wns_ss >= base.wns_ss - 0.001 &&
        cand.tns_ss > base.tns_ss + 1e-9) return true;
    if (cand.wns_ss >= base.wns_ss - 0.004 &&
        cand.tns_ss >= base.tns_ss - 25.0 &&
        cand.nvp_ss < base.nvp_ss) return true;
    if (cand.wns_ss >= base.wns_ss - 0.008 &&
        cand.tns_ss >= base.tns_ss - 75.0 &&
        cand.nvp_ss + 50 < base.nvp_ss) return true;
    return false;
}

bool setupNvpImproves(const Metrics& cand, const Metrics& base) {
    if (cand.nvp_ss >= base.nvp_ss) return false;
    int nvp_gain = base.nvp_ss - cand.nvp_ss;
    double wns_tol = nvp_gain >= 100 ? 0.010 : 0.006;
    double tns_tol = nvp_gain >= 100 ? 120.0 : 40.0;
    return cand.wns_ss >= base.wns_ss - wns_tol &&
           cand.tns_ss >= base.tns_ss - tns_tol;
}

bool holdImproves(const Metrics& cand, const Metrics& base) {
    if (cand.wns_ff > base.wns_ff + kScoreEps) return true;
    if (std::abs(cand.wns_ff - base.wns_ff) <= kScoreEps &&
        cand.tns_ff > base.tns_ff + 1e-9) return true;
    if (std::abs(cand.wns_ff - base.wns_ff) <= kScoreEps &&
        std::abs(cand.tns_ff - base.tns_ff) <= 1e-9 &&
        cand.nvp_ff < base.nvp_ff) return true;
    return false;
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
    switch (phase) {
        case PhaseKind::WnsRepair:
            return setupImproves(cand, base) &&
                   cand.wns_ss >= base.wns_ss - 1e-12 &&
                   holdProtected(cand, base, 0.004, 0.05);
        case PhaseKind::TnsCleanup:
            return (setupImproves(cand, base) || setupNvpImproves(cand, base)) &&
                   setupProtected(cand, base, 0.010, 120.0) &&
                   holdProtected(cand, base, 0.004, 0.04);
        case PhaseKind::GroupInsertion:
            return setupImproves(cand, base) &&
                   setupProtected(cand, base, 0.001, 0.25) &&
                   holdProtected(cand, base, 0.001, 0.005);
        case PhaseKind::HoldRepair:
            return holdImproves(cand, base) &&
                   setupProtected(cand, base, 0.0005, 0.01);
        case PhaseKind::AreaRecovery:
            return cand.area < base.area - 1e-9 &&
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
    if (phase == PhaseKind::WnsRepair) {
        if (cand.wns_ss > base.wns_ss + kScoreEps) return true;
        if (cand.wns_ss < base.wns_ss - kScoreEps) return false;
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
        if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
        return betterCandidateOrder(cand, base, original);
    }
    if (phase == PhaseKind::TnsCleanup) {
        if (cand.nvp_ss < base.nvp_ss) return true;
        if (cand.nvp_ss > base.nvp_ss) return false;
        if (cand.tns_ss > base.tns_ss + kScoreEps) return true;
        if (cand.tns_ss < base.tns_ss - kScoreEps) return false;
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
    if (phase == PhaseKind::HoldRepair) {
        if (cand.wns_ff > base.wns_ff + kScoreEps) return true;
        if (cand.wns_ff < base.wns_ff - kScoreEps) return false;
        if (cand.tns_ff > base.tns_ff + kScoreEps) return true;
        if (cand.tns_ff < base.tns_ff - kScoreEps) return false;
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

std::string moveKey(const Move& move) {
    return std::to_string(static_cast<int>(move.kind)) + "|" +
           move.parent + "|" + move.child + "|" + move.node + "|" +
           move.first_type + "|" + move.second_type + "|" + move.third_type + "|" +
           std::to_string(move.chain_len);
}

void pushUniqueMove(const Move& move,
                    std::vector<Move>& moves,
                    std::unordered_set<std::string>& seen) {
    std::string key = moveKey(move);
    if (seen.insert(key).second) {
        moves.push_back(move);
    }
}

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
            two.chain_len = 2;
            two.estimate = 1.4 * priority / (1.0 + first_cell.area + second_cell.area);
            pushUniqueMove(two, moves, seen);
        }
    }
}

double chainSsDelay(const Move& move,
                    const std::unordered_map<std::string, BufferCell>& buf_lib) {
    double delay = 0.0;
    if (move.chain_len >= 1) delay += nodeDelay(buf_lib, move.first_type, 1, true);
    if (move.chain_len >= 2) delay += nodeDelay(buf_lib, move.second_type, 1, true);
    if (move.chain_len >= 3) delay += nodeDelay(buf_lib, move.third_type, 1, true);
    return delay;
}

double chainArea(const Move& move,
                 const std::unordered_map<std::string, BufferCell>& buf_lib) {
    double area = 0.0;
    if (move.chain_len >= 1) area += cellArea(buf_lib, move.first_type);
    if (move.chain_len >= 2) area += cellArea(buf_lib, move.second_type);
    if (move.chain_len >= 3) area += cellArea(buf_lib, move.third_type);
    return area;
}

void addWnsRepairMoves(const std::unordered_map<std::string, ClockNode>& tree,
                       const std::unordered_map<std::string, BufferCell>& buf_lib,
                       const TimingReport& report,
                       const std::vector<std::string>& cells,
                       std::vector<Move>& moves,
                       std::unordered_set<std::string>& seen) {
    std::size_t ss_count = std::min(kWnsRepairSetupPaths, report.ss_violations.size());
    for (std::size_t i = 0; i < ss_count; ++i) {
        const auto& violation = report.ss_violations[i];
        auto capture_it = tree.find(violation.capture);
        if (capture_it == tree.end() || !capture_it->second.is_sink || capture_it->second.parent.empty()) continue;

        const double need_delay = -violation.slack;
        const double rank_bonus = static_cast<double>(ss_count - i) * 1000000.0;
        const std::string& parent = capture_it->second.parent;
        const std::string& child = violation.capture;

        for (const auto& first : cells) {
            if (buf_lib.at(first).max_fanout < 1) continue;

            Move one;
            one.kind = MoveKind::InsertLeaf;
            one.parent = parent;
            one.child = child;
            one.first_type = first;
            one.chain_len = 1;
            one.estimate = 1000000000.0 + rank_bonus
                         - 100000.0 * std::abs(chainSsDelay(one, buf_lib) - need_delay)
                         - 1000.0 * chainArea(one, buf_lib);
            pushUniqueMove(one, moves, seen);

            for (const auto& second : cells) {
                if (buf_lib.at(second).max_fanout < 1) continue;

                Move two;
                two.kind = MoveKind::InsertLeaf;
                two.parent = parent;
                two.child = child;
                two.first_type = first;
                two.second_type = second;
                two.chain_len = 2;
                two.estimate = 1000000000.0 + rank_bonus
                             - 100000.0 * std::abs(chainSsDelay(two, buf_lib) - need_delay)
                             - 1000.0 * chainArea(two, buf_lib);
                pushUniqueMove(two, moves, seen);

                for (const auto& third : cells) {
                    if (buf_lib.at(third).max_fanout < 1) continue;

                    Move three;
                    three.kind = MoveKind::InsertLeaf;
                    three.parent = parent;
                    three.child = child;
                    three.first_type = first;
                    three.second_type = second;
                    three.third_type = third;
                    three.chain_len = 3;
                    three.estimate = 1000000000.0 + rank_bonus
                                   - 100000.0 * std::abs(chainSsDelay(three, buf_lib) - need_delay)
                                   - 1000.0 * chainArea(three, buf_lib);
                    pushUniqueMove(three, moves, seen);
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
        if (priority <= 0.0 || stat.count == 0) continue;
        focus_sinks.insert(stat.name);
        node_priority[stat.name] += priority;
        addAncestorsForFocus(tree, stat.name, 0.65 * priority,
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
            move.chain_len = 0;
            move.estimate = old_cell->second.area - new_cell->second.area;
            pushUniqueMove(move, moves, seen);
        }
    }
}

std::vector<Move> generateCandidateMoves(
        const std::unordered_map<std::string, ClockNode>& tree,
        const std::unordered_map<std::string, BufferCell>& buf_lib,
        const TimingReport& report,
        PhaseKind phase) {
    std::vector<Move> moves;
    std::unordered_set<std::string> seen;
    std::vector<std::string> cells = sortedCellNames(buf_lib);
    std::unordered_map<std::string, double> node_priority;
    std::unordered_set<std::string> focus_sinks;
    std::unordered_set<std::string> focus_nodes;
    std::vector<std::pair<std::string, std::string>> focus_edges;

    if (phase == PhaseKind::AreaRecovery) {
        addAreaRecoveryMoves(tree, buf_lib, cells, moves, seen);
        std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
            return a.estimate > b.estimate;
        });
        if (moves.size() > kMaxCandidatesPerIteration) {
            moves.resize(kMaxCandidatesPerIteration);
        }
        return moves;
    }

    if (phase == PhaseKind::WnsRepair) {
        addWnsRepairMoves(tree, buf_lib, report, cells, moves, seen);
    }

    if (phase == PhaseKind::TnsCleanup || phase == PhaseKind::GroupInsertion) {
        addTnsCleanupFocus(tree, report, node_priority, focus_sinks, focus_nodes, focus_edges);
    }

    std::size_t ss_count = std::min(kTopSetupPaths, report.ss_violations.size());
    if (phase == PhaseKind::WnsRepair || phase == PhaseKind::TnsCleanup ||
        phase == PhaseKind::GroupInsertion) {
        for (std::size_t i = 0; i < ss_count; ++i) {
            const auto& v = report.ss_violations[i];
            double rank_weight = static_cast<double>(ss_count - i) / std::max<std::size_t>(1, ss_count);
            double phase_boost = (phase == PhaseKind::WnsRepair) ? 5.0 : 1.0;
            double priority = phase_boost * ((1000.0 * rank_weight) + (100.0 * -v.slack));
            focus_sinks.insert(v.capture);
            node_priority[v.capture] += priority;
            addAncestorsForFocus(tree, v.capture, priority, node_priority, focus_nodes, focus_edges);
        }
    }

    std::size_t ff_count = std::min(kTopHoldPaths, report.ff_violations.size());
    if (phase == PhaseKind::HoldRepair) {
        for (std::size_t i = 0; i < ff_count; ++i) {
            const auto& v = report.ff_violations[i];
            double rank_weight = static_cast<double>(ff_count - i) / std::max<std::size_t>(1, ff_count);
            double priority = (3000.0 * rank_weight) + (500.0 * -v.slack);
            focus_sinks.insert(v.launch);
            node_priority[v.launch] += priority;
            node_priority[v.capture] -= 0.5 * priority;
            addAncestorsForFocus(tree, v.launch, priority, node_priority, focus_nodes, focus_edges);
        }
    }

    for (const auto& sink : focus_sinks) {
        auto it = tree.find(sink);
        if (it == tree.end() || !it->second.is_sink || it->second.parent.empty()) continue;
        double priority = std::max(0.0, node_priority[sink]);
        if (priority <= 0.0) continue;
        addInsertionMoves(it->second.parent, sink, MoveKind::InsertLeaf, priority,
                          cells, buf_lib, moves, seen);
    }

    for (const auto& node_name : focus_nodes) {
        auto node_it = tree.find(node_name);
        if (node_it == tree.end()) continue;
        const ClockNode& node = node_it->second;
        if (node.type == "ROOT" || node.is_sink) continue;

        int fanout = static_cast<int>(node.children.size());
        double priority = node_priority[node.name];
        double old_avg_delay = 0.5 * (nodeDelay(buf_lib, node.type, fanout, true) +
                                      nodeDelay(buf_lib, node.type, fanout, false));

        for (const auto& cell_name : cells) {
            if (cell_name == node.type) continue;
            const BufferCell& cell = buf_lib.at(cell_name);
            if (fanout > cell.max_fanout) continue;

            double new_avg_delay = 0.5 * (nodeDelay(buf_lib, cell_name, fanout, true) +
                                          nodeDelay(buf_lib, cell_name, fanout, false));
            double delta_delay = new_avg_delay - old_avg_delay;
            Move move;
            move.kind = MoveKind::ResizeBuffer;
            move.node = node.name;
            move.first_type = cell_name;
            move.chain_len = 0;
            move.estimate = priority * delta_delay - 0.001 * cell.area;
            pushUniqueMove(move, moves, seen);
        }
    }

    if (phase == PhaseKind::TnsCleanup || phase == PhaseKind::GroupInsertion) {
        for (const auto& edge : focus_edges) {
            auto parent_it = tree.find(edge.first);
            auto child_it = tree.find(edge.second);
            if (parent_it == tree.end() || child_it == tree.end()) continue;
            double priority = std::max(0.0, node_priority[edge.second]);
            if (priority <= 0.0) continue;
            double phase_weight = (phase == PhaseKind::TnsCleanup) ? 1.2 : 0.8;
            addInsertionMoves(edge.first, edge.second, MoveKind::InsertInternal, phase_weight * priority,
                              cells, buf_lib, moves, seen);
        }
    }

    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.estimate > b.estimate;
    });
    if (moves.size() > kMaxCandidatesPerIteration) {
        moves.resize(kMaxCandidatesPerIteration);
    }
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

    if (move.chain_len < 1 || move.chain_len > 3) return false;
    auto parent_it = tree.find(move.parent);
    auto child_it = tree.find(move.child);
    if (parent_it == tree.end() || child_it == tree.end()) return false;

    std::vector<std::string> chain_types;
    chain_types.push_back(move.first_type);
    if (move.chain_len >= 2) chain_types.push_back(move.second_type);
    if (move.chain_len >= 3) chain_types.push_back(move.third_type);

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
    const PhaseConfig phases[] = {
        {PhaseKind::WnsRepair, "WNS repair", 0.0, 70.0, 384, 3},
        {PhaseKind::TnsCleanup, "TNS/NVP cleanup", 70.0, 420.0, 768, 4},
        {PhaseKind::GroupInsertion, "group/ancestor insertion", 420.0, 500.0, 768, 2},
        {PhaseKind::HoldRepair, "hold final repair", 480.0, 540.0, 384, 2},
        {PhaseKind::AreaRecovery, "area recovery", 540.0, 570.0, 512, 2}
    };

    TimingReport original_report = analyzeTiming(root_name, clock_tree, buf_lib,
                                                 ss_paths, ff_paths, clock_period);
    Metrics original = original_report.metrics;
    Metrics best_metrics = original;
    Metrics current_metrics = original;
    double best_score = evaluateRobustScore(best_metrics, original).robust;

    best_tree = clock_tree;
    std::unordered_map<std::string, ClockNode> current_tree = clock_tree;
    int next_new_buf_id = findNextNewBufferId(current_tree);
    int no_improve_rounds = 0;
    int applied_moves = 0;
    int resize_moves = 0;

    log.line("## Baseline");
    log.line("- Existing optimizer status: it did not have fixed 0-2/2-6/6-8/8-9/9-9.5 minute phases; it used one mixed greedy loop.");
    log.line("- New schedule: 0-2 WNS repair, 2-6 TNS/NVP cleanup, 6-8 group/ancestor insertion, 8-9 hold final repair, 9-9.5 area recovery, then output best_tree.");
    log.line("- Baseline SS TNS/WNS/NVP: " + std::to_string(original.tns_ss) + "/" +
             std::to_string(original.wns_ss) + "/" + std::to_string(original.nvp_ss));
    log.line("- Baseline FF TNS/WNS/NVP: " + std::to_string(original.tns_ff) + "/" +
             std::to_string(original.wns_ff) + "/" + std::to_string(original.nvp_ff));
    log.line("- Baseline area: " + std::to_string(original.area));

    RobustScore original_score = evaluateRobustScore(original, original);
    std::cout << "[OPT] Initial robust score: " << best_score
              << " | setup/hold/area score: "
              << original_score.setup << "/" << original_score.hold << "/" << original_score.area
              << " | SS TNS/WNS/NVP: " << original.tns_ss << "/" << original.wns_ss << "/" << original.nvp_ss
              << " | FF TNS/WNS/NVP: " << original.tns_ff << "/" << original.wns_ff << "/" << original.nvp_ff
              << " | Area: " << original.area << "\n";
    printTopViolations(original_report);
    std::cout.flush();

    int global_iter = 0;
    for (const PhaseConfig& phase : phases) {
        while (elapsedSec(start_time) < phase.start_sec && !isTimeLimitReached(start_time)) {
            break;
        }
        if (isTimeLimitReached(start_time)) break;

        no_improve_rounds = 0;
        std::cout << "[OPT] === Phase: " << phase.name
                  << " (" << phase.start_sec << "-" << phase.end_sec
                  << " sec, exact limit " << phase.exact_limit << ") ===\n";
        log.line("\n## Phase: " + std::string(phase.name));
        log.line("- Time window: " + std::to_string(phase.start_sec) + "-" +
                 std::to_string(phase.end_sec) + " sec");
        std::cout.flush();

        for (int phase_iter = 0;
             elapsedSec(start_time) < phase.end_sec && !isTimeLimitReached(start_time);
             ++phase_iter, ++global_iter) {
            TimingReport current_report = analyzeTiming(root_name, current_tree, buf_lib,
                                                        ss_paths, ff_paths, clock_period);
            current_metrics = current_report.metrics;
            std::vector<Move> moves = generateCandidateMoves(current_tree, buf_lib,
                                                             current_report, phase.kind);
            if (moves.empty()) {
                std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                          << ": no legal candidates.\n";
                log.line("- Iteration " + std::to_string(phase_iter) + ": no legal candidates.");
                break;
            }

            std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                      << " | current SS TNS/WNS/NVP: "
                      << current_metrics.tns_ss << "/" << current_metrics.wns_ss << "/" << current_metrics.nvp_ss
                      << " | FF TNS/WNS/NVP: "
                      << current_metrics.tns_ff << "/" << current_metrics.wns_ff << "/" << current_metrics.nvp_ff
                      << " | candidates: " << moves.size()
                      << " | exact limit: " << phase.exact_limit << "\n";
            std::cout.flush();

            Move best_move;
            Metrics best_candidate_metrics;
            bool has_candidate = false;
            std::unordered_map<std::string, ClockNode> best_candidate_tree;
            std::size_t evaluated = 0;

            for (const auto& move : moves) {
                if (evaluated >= phase.exact_limit ||
                    elapsedSec(start_time) >= phase.end_sec ||
                    isTimeLimitReached(start_time)) break;

                auto trial_tree = current_tree;
                int trial_next_new_buf_id = next_new_buf_id;
                if (!applyMove(trial_tree, move, buf_lib, trial_next_new_buf_id)) continue;

                TimingReport trial_report = analyzeTiming(root_name, trial_tree, buf_lib,
                                                          ss_paths, ff_paths, clock_period);
                ++evaluated;
                if (!acceptableForPhase(phase.kind, trial_report.metrics, current_metrics, original)) continue;

                if (!has_candidate ||
                    betterCandidateForPhase(phase.kind, trial_report.metrics, best_candidate_metrics, original)) {
                    has_candidate = true;
                    best_candidate_metrics = trial_report.metrics;
                    best_candidate_tree = trial_tree;
                    best_move = move;
                }
            }

            if (!has_candidate) {
                std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                          << " | evaluated: " << evaluated
                          << " | no acceptable phase candidate"
                          << " | elapsed: " << elapsedSec(start_time) << " sec\n";
                log.line("- Iteration " + std::to_string(phase_iter) +
                         ": evaluated " + std::to_string(evaluated) +
                         ", no acceptable candidate.");
                ++no_improve_rounds;
                if (no_improve_rounds >= phase.max_no_improve_rounds) break;
                continue;
            }

            std::cout << "[OPT] " << phase.name << " iteration " << phase_iter
                      << " | evaluated: " << evaluated
                      << " | best SS TNS/WNS/NVP: "
                      << best_candidate_metrics.tns_ss << "/" << best_candidate_metrics.wns_ss << "/" << best_candidate_metrics.nvp_ss
                      << " | FF TNS/WNS/NVP: "
                      << best_candidate_metrics.tns_ff << "/" << best_candidate_metrics.wns_ff << "/" << best_candidate_metrics.nvp_ff
                      << " | robust score: " << evaluateRobustScore(best_candidate_metrics, original).robust
                      << " | area: " << best_candidate_metrics.area
                      << " | elapsed: " << elapsedSec(start_time) << " sec\n";

            log.line("- Iteration " + std::to_string(phase_iter) +
                     ": accepted " + std::string(phaseName(phase.kind)) +
                     " move; evaluated " + std::to_string(evaluated) +
                     "; SS TNS/WNS/NVP " + std::to_string(best_candidate_metrics.tns_ss) + "/" +
                     std::to_string(best_candidate_metrics.wns_ss) + "/" +
                     std::to_string(best_candidate_metrics.nvp_ss) +
                     "; FF TNS/WNS/NVP " + std::to_string(best_candidate_metrics.tns_ff) + "/" +
                     std::to_string(best_candidate_metrics.wns_ff) + "/" +
                     std::to_string(best_candidate_metrics.nvp_ff) +
                     "; area " + std::to_string(best_candidate_metrics.area) + ".");

            current_tree = best_candidate_tree;
            current_metrics = best_candidate_metrics;
            ++applied_moves;
            if (best_move.kind == MoveKind::ResizeBuffer) ++resize_moves;
            if (best_move.kind != MoveKind::ResizeBuffer) {
                next_new_buf_id = findNextNewBufferId(current_tree);
            }
            no_improve_rounds = 0;

            if (betterCandidateOrder(current_metrics, best_metrics, original)) {
                best_metrics = current_metrics;
                best_tree = current_tree;
                best_score = evaluateRobustScore(best_metrics, original).robust;
                log.line("  - Updated best_tree.");
            }
        }
    }

    RobustScore final_score = evaluateRobustScore(best_metrics, original);
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
    std::cout << "[OPT] Final best robust score: " << best_score
              << " | setup/hold/area score: "
              << final_score.setup << "/" << final_score.hold << "/" << final_score.area
              << " | SS TNS/WNS/NVP: " << best_metrics.tns_ss << "/" << best_metrics.wns_ss << "/" << best_metrics.nvp_ss
              << " | FF TNS/WNS/NVP: " << best_metrics.tns_ff << "/" << best_metrics.wns_ff << "/" << best_metrics.nvp_ff
              << " | Area: " << best_metrics.area << "\n";
    std::cout << "[OPT] Applied moves: " << applied_moves
              << " | inserted buffers: " << countInsertedBuffers(best_tree)
              << " | resize moves: " << resize_moves << "\n";
}
