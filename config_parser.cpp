#include "config_parser.h"
#include <algorithm>
#include <cctype>

ConfigParser::ConfigParser() {
}

ConfigParser::~ConfigParser() {
}

bool ConfigParser::loadConfig(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_file << std::endl;
        return false;
    }
    
    std::string line;
    std::string current_section = "";
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        line = trim(line);
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // 解析节
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
            continue;
        }
        
        // 解析键值对
        size_t equal_pos = line.find('=');
        if (equal_pos == std::string::npos) {
            std::cerr << "Invalid config line " << line_number << ": " << line << std::endl;
            continue;
        }
        
        std::string key = trim(line.substr(0, equal_pos));
        std::string value = trim(line.substr(equal_pos + 1));
        
        if (key.empty()) {
            std::cerr << "Empty key at line " << line_number << std::endl;
            continue;
        }
        
        config_data_[current_section][key] = value;
    }
    
    file.close();
    return true;
}

std::string ConfigParser::getString(const std::string& section, const std::string& key, const std::string& default_value) {
    auto section_it = config_data_.find(section);
    if (section_it == config_data_.end()) {
        return default_value;
    }
    
    auto key_it = section_it->second.find(key);
    if (key_it == section_it->second.end()) {
        return default_value;
    }
    
    return key_it->second;
}

int ConfigParser::getInt(const std::string& section, const std::string& key, int default_value) {
    std::string value = getString(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    try {
        return std::stoi(value);
    } catch (const std::exception& e) {
        std::cerr << "Invalid integer value for " << section << "." << key << ": " << value << std::endl;
        return default_value;
    }
}

bool ConfigParser::getBool(const std::string& section, const std::string& key, bool default_value) {
    std::string value = getString(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    return parseBool(value);
}

double ConfigParser::getDouble(const std::string& section, const std::string& key, double default_value) {
    std::string value = getString(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    try {
        return std::stod(value);
    } catch (const std::exception& e) {
        std::cerr << "Invalid double value for " << section << "." << key << ": " << value << std::endl;
        return default_value;
    }
}

void ConfigParser::setString(const std::string& section, const std::string& key, const std::string& value) {
    config_data_[section][key] = value;
}

void ConfigParser::setInt(const std::string& section, const std::string& key, int value) {
    config_data_[section][key] = std::to_string(value);
}

void ConfigParser::setBool(const std::string& section, const std::string& key, bool value) {
    config_data_[section][key] = value ? "true" : "false";
}

void ConfigParser::setDouble(const std::string& section, const std::string& key, double value) {
    config_data_[section][key] = std::to_string(value);
}

bool ConfigParser::saveConfig(const std::string& config_file) {
    std::ofstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file for writing: " << config_file << std::endl;
        return false;
    }
    
    for (const auto& section_pair : config_data_) {
        file << "[" << section_pair.first << "]" << std::endl;
        
        for (const auto& key_pair : section_pair.second) {
            file << key_pair.first << "=" << key_pair.second << std::endl;
        }
        
        file << std::endl;
    }
    
    file.close();
    return true;
}

bool ConfigParser::hasKey(const std::string& section, const std::string& key) {
    auto section_it = config_data_.find(section);
    if (section_it == config_data_.end()) {
        return false;
    }
    
    return section_it->second.find(key) != section_it->second.end();
}

void ConfigParser::printConfig() {
    std::cout << "=== Configuration ===" << std::endl;
    for (const auto& section_pair : config_data_) {
        std::cout << "[" << section_pair.first << "]" << std::endl;
        
        for (const auto& key_pair : section_pair.second) {
            std::cout << "  " << key_pair.first << " = " << key_pair.second << std::endl;
        }
        
        std::cout << std::endl;
    }
}

std::string ConfigParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ConfigParser::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool ConfigParser::parseBool(const std::string& value) {
    std::string lower_value = toLower(value);
    return (lower_value == "true" || lower_value == "1" || lower_value == "yes" || lower_value == "on");
}