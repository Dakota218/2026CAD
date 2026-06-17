#ifndef PARSER_H
#define PARSER_H

#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Buffer Library Structure
struct BufferCell {
    std::string name;
    double width = 0.0;
    double height = 0.0;
    double area = 0.0;
    std::vector<double> ss_delays; // delays for fanout 1, 2, 3, 4, 5...
    std::vector<double> ff_delays;
    int max_fanout = 0;
};

// Clock Tree Node
struct ClockNode {
    std::string name;
    std::string type; // "REALBUF_X8", "FIFO", etc.
    bool is_sink = false;
    std::string parent;
    std::vector<std::string> children;
    int level = 0;
};

// Data Path Structure
struct DataPath {
    std::string name;
    std::string launch_ff;
    std::string capture_ff;
    double delay = 0.0;
};

class Parser {
public:
    static bool parseStructure(const std::string& filepath, 
                               std::string& root_name, 
                               std::unordered_map<std::string, ClockNode>& clock_tree);
    
    static bool parseLib(const std::string& filepath, 
                         std::unordered_map<std::string, BufferCell>& buf_lib);
    
    static bool parseDelayRpt(const std::string& filepath, 
                              double& clock_period, 
                              std::vector<DataPath>& paths);
    
    static void writeOutput(const std::string& filepath, 
                            const std::string& root_name, 
                            const std::unordered_map<std::string, ClockNode>& clock_tree);
};

#endif // PARSER_H