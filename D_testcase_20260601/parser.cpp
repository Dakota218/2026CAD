#include "parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

static inline std::string trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

bool Parser::parseStructure(const std::string& filepath, 
                             std::string& root_name, 
                             std::unordered_map<std::string, ClockNode>& clock_tree) {
    std::ifstream infile(filepath);
    if (!infile.is_open()) return false;

    std::string line;
    std::vector<std::pair<int, std::string>> current_branch;

    while (std::getline(infile, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (line.find("Root:") == 0) {
            root_name = trim(line.substr(5));
            ClockNode root;
            root.name = root_name;
            root.type = "ROOT";
            root.level = 0;
            clock_tree[root_name] = root;
            current_branch.push_back({0, root_name});
            continue;
        }

        if (line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket == std::string::npos) continue;
            
            int level = std::stoi(line.substr(1, close_bracket - 1));
            std::string remaining = trim(line.substr(close_bracket + 1));

            std::stringstream ss(remaining);
            std::string node_name, type_part;
            ss >> node_name >> type_part;

            if (!type_part.empty() && type_part.front() == '(') type_part.erase(0, 1);
            if (!type_part.empty() && type_part.back() == ')') type_part.pop_back();

            bool is_sink = (remaining.find("SINK") != std::string::npos);

            ClockNode node;
            node.name = node_name;
            node.type = type_part;
            node.is_sink = is_sink;
            node.level = level;

            while (!current_branch.empty() && current_branch.back().first >= level) {
                current_branch.pop_back();
            }

            if (!current_branch.empty()) {
                node.parent = current_branch.back().second;
                clock_tree[node.parent].children.push_back(node_name);
            }

            clock_tree[node_name] = node;
            current_branch.push_back({level, node_name});
        }
    }
    return true;
}

bool Parser::parseLib(const std::string& filepath, 
                      std::unordered_map<std::string, BufferCell>& buf_lib) {
    std::ifstream infile(filepath);
    if (!infile.is_open()) return false;

    std::string line;
    BufferCell current_cell;
    bool in_cell = false;

    while (std::getline(infile, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (line.find("cell") == 0) {
            size_t open_p = line.find('(');
            size_t close_p = line.find(')');
            if (open_p != std::string::npos && close_p != std::string::npos) {
                current_cell = BufferCell();
                current_cell.name = line.substr(open_p + 1, close_p - open_p - 1);
                in_cell = true;
            }
            continue;
        }

        if (in_cell) {
            if (line.find("SIZE") == 0) {
                std::stringstream ss(line);
                std::string token;
                double w, h;
                ss >> token >> w >> token >> h;
                current_cell.width = w;
                current_cell.height = h;
                current_cell.area = w * h;
            } else if (line.find("SS_DELAY") == 0) {
                std::stringstream ss(line.substr(8));
                double val;
                while (ss >> val) current_cell.ss_delays.push_back(val);
                current_cell.max_fanout = current_cell.ss_delays.size();
            } else if (line.find("FF_DELAY") == 0) {
                std::stringstream ss(line.substr(8));
                double val;
                while (ss >> val) current_cell.ff_delays.push_back(val);
            } else if (line == "}") {
                buf_lib[current_cell.name] = current_cell;
                in_cell = false;
            }
        }
    }
    return true;
}

bool Parser::parseDelayRpt(const std::string& filepath, 
                           double& clock_period, 
                           std::vector<DataPath>& paths) {
    std::ifstream infile(filepath);
    if (!infile.is_open()) return false;

    std::string line;
    int path_counter = 0;

    while (std::getline(infile, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line.find("Clock Period:") == 0) {
            clock_period = std::stod(trim(line.substr(13)));
            continue;
        }

        if (line.find("Path") == 0 || line.find("path") == 0 || (line.find("->") != std::string::npos)) {
            size_t colon = line.find(':');
            std::string content = (colon == std::string::npos) ? line : line.substr(colon + 1);
            content = trim(content);

            size_t arrow = content.find("->");
            if (arrow == std::string::npos) continue;

            std::string launch = trim(content.substr(0, arrow));
            std::string remaining = trim(content.substr(arrow + 2));

            std::stringstream ss(remaining);
            std::string capture;
            double delay;
            if (ss >> capture >> delay) {
                DataPath path;
                path.name = "Path_" + std::to_string(path_counter++);
                path.launch_ff = launch;
                path.capture_ff = capture;
                path.delay = delay;
                paths.push_back(path);
            }
        }
    }
    return true;
}

static void printTreeDfs(std::ostream& out, const std::string& node_name, 
                          const std::unordered_map<std::string, ClockNode>& clock_tree) {
    const auto& node = clock_tree.at(node_name);
    if (node.type != "ROOT") {
        for (int i = 0; i < node.level; ++i) {
            out << "\t";
        }
        out << "[" << node.level << "] " << node.name << " (" << node.type << ")";
        if (node.is_sink) out << " (SINK)";
        out << "\n";
    }
    for (const auto& child : node.children) {
        printTreeDfs(out, child, clock_tree);
    }
}

void Parser::writeOutput(const std::string& filepath, 
                         const std::string& root_name, 
                         const std::unordered_map<std::string, ClockNode>& clock_tree) {
    std::ofstream outfile(filepath);
    if (!outfile.is_open()) return;

    outfile << "Root: " << root_name << "\n";
    for (const auto& child : clock_tree.at(root_name).children) {
        printTreeDfs(outfile, child, clock_tree);
    }
}
