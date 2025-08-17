#include "rtmp_client.h"
#include <iostream>
#include <string>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

// 简单的文件系统操作替代std::filesystem
namespace fs {
    bool exists(const std::string& path) {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }
    
    size_t file_size(const std::string& path) {
        struct stat buffer;
        if (stat(path.c_str(), &buffer) == 0) {
            return buffer.st_size;
        }
        return 0;
    }
    
    bool create_directories(const std::string& path) {
        return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <rtmp_url> <flv_file>" << std::endl;
        std::cout << "Example: " << argv[0] << " rtmp://localhost:1935/live/stream test.flv" << std::endl;
        return 1;
    }
    
    std::string rtmp_url = argv[1];
    std::string flv_file = argv[2];
    
    // 创建日志目录
    fs::create_directories("logs");
    
    RTMPClient client;
    
    // 设置日志级别
    client.setLogLevel("info");
    
    // 配置客户端参数
    RTMPConfig config;
    config.connect_timeout_ms = 10000;
    config.max_retry_count = 3;
    config.enable_heartbeat = true;
    config.enable_statistics = true;
    client.setConfig(config);
    
    client.logInfo("RTMP客户端启动");
    client.logInfoF("参数: URL=%s, 文件=%s", rtmp_url.c_str(), flv_file.c_str());
    
    // 检查FLV文件是否存在
    if (!fs::exists(flv_file)) {
        client.logError("FLV文件不存在: " + flv_file);
        return 1;
    }
    
    auto file_size = fs::file_size(flv_file);
    client.logInfoF("FLV文件大小: %.2f MB", file_size / (1024.0 * 1024.0));
    
    // 使用重试机制连接
    client.logInfo("开始连接到RTMP服务器: " + rtmp_url);
    auto start_time = std::chrono::steady_clock::now();
    
    if (!client.connectWithRetry(rtmp_url, 3)) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        client.logPerformance("连接失败", duration);
        client.logError("连接RTMP服务器失败");
        client.flushLogs();
        return 1;
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    client.logPerformance("连接建立", duration);
    
    // 启动心跳线程
    client.startHeartbeatThread();
    
    // 推送FLV文件
    client.logInfo("开始推送FLV文件: " + flv_file);
    start_time = std::chrono::steady_clock::now();
    
    if (!client.pushFLVFile(flv_file)) {
        end_time = std::chrono::steady_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        client.logPerformance("推流失败", duration);
        client.logError("推送FLV文件失败");
        client.stopHeartbeatThread();
        client.flushLogs();
        return 1;
    }
    
    end_time = std::chrono::steady_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    client.logPerformance("推流完成", duration);
    
    // 停止心跳线程
    client.stopHeartbeatThread();
    
    // 显示最终统计
    client.logStatistics();
    client.logInfo("推流任务成功完成");
    
    // 刷新并关闭日志
    client.flushLogs();
    client.shutdownLogger();
    
    return 0;
}
