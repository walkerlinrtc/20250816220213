#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

// 配置解析器类
class ConfigParser {
public:
    ConfigParser();
    ~ConfigParser();
    
    // 加载配置文件
    bool loadConfig(const std::string& config_file);
    
    // 获取配置值
    std::string getString(const std::string& section, const std::string& key, const std::string& default_value = "");
    int getInt(const std::string& section, const std::string& key, int default_value = 0);
    bool getBool(const std::string& section, const std::string& key, bool default_value = false);
    double getDouble(const std::string& section, const std::string& key, double default_value = 0.0);
    
    // 设置配置值
    void setString(const std::string& section, const std::string& key, const std::string& value);
    void setInt(const std::string& section, const std::string& key, int value);
    void setBool(const std::string& section, const std::string& key, bool value);
    void setDouble(const std::string& section, const std::string& key, double value);
    
    // 保存配置文件
    bool saveConfig(const std::string& config_file);
    
    // 检查配置项是否存在
    bool hasKey(const std::string& section, const std::string& key);
    
    // 打印所有配置
    void printConfig();

private:
    std::map<std::string, std::map<std::string, std::string>> config_data_;
    
    // 辅助方法
    std::string trim(const std::string& str);
    std::string toLower(const std::string& str);
    bool parseBool(const std::string& value);
};

#endif // CONFIG_PARSER_H