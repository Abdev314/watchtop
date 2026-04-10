#pragma once
#include <unordered_map>
#include <vector>
#include <string>

class PortScanner {
public:

    static std::unordered_map<int, std::vector<std::string>> scanListeningPorts();
};


