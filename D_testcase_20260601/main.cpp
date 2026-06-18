#include "parser.h"
#include "timing_engine.h"
#include "optimizer.h"
#include <iostream>
#include <cmath>

int main(int argc, char* argv[]) {
    // Command line arguments standard validation (Section 5 Spec)
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <testcase_dir_path> <output_file_path>\n";
        std::cerr << "Example: ./cadd0000 ./testcase0 ./testcase0/modified_clk_tree.structure\n";
        return 1;
    }

    std::string testcase_dir = argv[1];
    std::string output_filepath = argv[2];

    std::string struct_file = testcase_dir + "/clk_tree.structure";
    std::string lib_file    = testcase_dir + "/buf.lib";
    std::string ss_rpt      = testcase_dir + "/SS_delay.rpt";
    std::string ff_rpt      = testcase_dir + "/FF_delay.rpt";

    std::cout << "=========================================================\n";
    std::cout << "  ICCAD 2026 Problem D: Timing Fixing Useful Skew Solver  \n";
    std::cout << "=========================================================\n";

    std::string root_name;
    std::unordered_map<std::string, ClockNode> clock_tree;
    std::unordered_map<std::string, BufferCell> buf_lib;
    std::vector<DataPath> ss_paths;
    std::vector<DataPath> ff_paths;
    double clock_period_ss = 0.0;
    double clock_period_ff = 0.0;

    std::cout << "[INFO] Commencing file parsing processes...\n";
    if (!Parser::parseStructure(struct_file, root_name, clock_tree)) {
        std::cerr << "[ERROR] Critical Failure: Unable to locate/parse " << struct_file << "\n";
        return 1;
    }
    if (!Parser::parseLib(lib_file, buf_lib)) {
        std::cerr << "[ERROR] Critical Failure: Unable to locate/parse " << lib_file << "\n";
        return 1;
    }
    if (!Parser::parseDelayRpt(ss_rpt, clock_period_ss, ss_paths)) {
        std::cerr << "[ERROR] Critical Failure: Unable to locate/parse " << ss_rpt << "\n";
        return 1;
    }
    if (!Parser::parseDelayRpt(ff_rpt, clock_period_ff, ff_paths)) {
        std::cerr << "[ERROR] Critical Failure: Unable to locate/parse " << ff_rpt << "\n";
        return 1;
    }
    if (std::abs(clock_period_ss - clock_period_ff) > 1e-9) {
        std::cerr << "[WARN] SS/FF clock periods differ. Using SS clock period as contest Tclk. "
                  << "SS: " << clock_period_ss << " | FF: " << clock_period_ff << "\n";
    }

    std::cout << "[SUCCESS] Map database populated successfully.\n";
    std::cout << "          Initial Clock Tree Nodes Count: " << clock_tree.size() << "\n";
    std::cout << "          Available Cell Library Sizes:   " << buf_lib.size() << "\n";
    std::cout << "          Monitored Path Constraints:     SS: " << ss_paths.size() << " | FF: " << ff_paths.size() << "\n";

    // Initial Baseline Evaluation
    TimingEngine initial_engine(root_name, clock_tree, buf_lib, ss_paths, ff_paths, clock_period_ss);
    double init_tns_ss, init_wns_ss, init_tns_ff, init_wns_ff, init_area;
    initial_engine.evaluateMetrics(init_tns_ss, init_wns_ss, init_tns_ff, init_wns_ff, init_area);
    TimingMetrics init_metrics = initial_engine.metrics();
    
    std::cout << "\n[BASELINE METRICS]:\n";
    std::cout << "  SS Corner -> TNS: " << init_tns_ss << " | WNS: " << init_wns_ss << "\n";
    std::cout << "  FF Corner -> TNS: " << init_tns_ff << " | WNS: " << init_wns_ff << "\n";
    std::cout << "  Total Initial Buffer Area: " << init_area << "\n";

    // Run Post-CTS Greedy Optimization
    std::cout << "\n[OPTIMIZATION] Initializing Useful Skew Optimizer Engine...\n";
    std::cout.flush();
    Optimizer optimizer(root_name, clock_tree, buf_lib, ss_paths, ff_paths, clock_period_ss);
    
    std::unordered_map<std::string, ClockNode> optimized_tree;
    optimizer.runOptimization(optimized_tree);

    // Final Architecture Quantization Check
    TimingEngine final_engine(root_name, optimized_tree, buf_lib, ss_paths, ff_paths, clock_period_ss);
    double final_tns_ss, final_wns_ss, final_tns_ff, final_wns_ff, final_area;
    final_engine.evaluateMetrics(final_tns_ss, final_wns_ss, final_tns_ff, final_wns_ff, final_area);
    TimingMetrics final_metrics = final_engine.metrics();

    std::cout << "\n[OPTIMIZED METRICS]:\n";
    std::cout << "  SS Corner -> TNS: " << final_tns_ss << " | WNS: " << final_wns_ss << "\n";
    std::cout << "  FF Corner -> TNS: " << final_tns_ff << " | WNS: " << final_wns_ff << "\n";
    std::cout << "  Total Optimized Buffer Area: " << final_area << "\n";

    // Serialize and Output Results
    std::cout << "\n[OUTPUT] Serializing structural netlist to output stream...\n";
    Parser::writeOutput(output_filepath, root_name, optimized_tree);
    std::cout << "[SUCCESS] File saved to: " << output_filepath << "\n";

    double area_delta_pct = init_area == 0.0 ? 0.0 : 100.0 * (final_area - init_area) / init_area;
    std::cout << "\n[FINAL SUMMARY]\n";
    std::cout << "Original:\n";
    std::cout << "SS WNS = " << init_metrics.wns_ss
              << ", TNS = " << init_metrics.tns_ss
              << ", NVP = " << init_metrics.nvp_ss << "\n";
    std::cout << "FF WNS = " << init_metrics.wns_ff
              << ", TNS = " << init_metrics.tns_ff
              << ",  NVP = " << init_metrics.nvp_ff << "\n\n";

    std::cout << "Modified:\n";
    std::cout << "SS WNS = " << final_metrics.wns_ss
              << ", TNS = " << final_metrics.tns_ss
              << ", NVP = " << final_metrics.nvp_ss << "\n";
    std::cout << "FF WNS = " << final_metrics.wns_ff
              << ", TNS = " << final_metrics.tns_ff
              << ",  NVP = " << final_metrics.nvp_ff << "\n\n";

    std::cout << "Area:\n";
    std::cout << init_area << " -> " << final_area
              << " (" << (area_delta_pct >= 0.0 ? "+" : "")
              << area_delta_pct << "%)\n";
    std::cout << "=========================================================\n";

    return 0;
}
