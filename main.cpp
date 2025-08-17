#include "rtmp_client.h"
#include "rtmp_logger.h"
#include "config_parser.h"
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
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <rtmp_url> <flv_file> [config_file]" << std::endl;
        std::cerr << "Example: " << argv[0] << " rtmp://localhost:1935/live/stream test.flv" << std::endl;
        std::cerr << "         " << argv[0] << " rtmp://localhost:1935/live/stream test.flv rtmp_client.conf" << std::endl;
        return 1;
    }
    
    std::string rtmp_url = argv[1];
    std::string flv_file = argv[2];
    std::string config_file = (argc == 4) ? argv[3] : "rtmp_client.conf";
    
    // 创建日志目录
    fs::create_directories("logs");
    
    RTMPClient client;
    
    // 加载配置文件
    ConfigParser config;
    if (fs::exists(config_file)) {
        if (!config.loadConfig(config_file)) {
            std::cerr << "Failed to load config file: " << config_file << std::endl;
            return 1;
        }
        RTMP_LOG_INFO(client, "从配置文件加载: " + config_file);
    } else {
        RTMP_LOG_WARN(client, "配置文件未找到: " + config_file + ", 使用默认设置");
    }
    
    // 从配置文件设置日志级别
    std::string log_level = config.getString("logging", "log_level", "info");
    client.setLogLevel(log_level);
    
    // 从配置文件配置客户端参数
    RTMPConfig rtmp_config;
    rtmp_config.connect_timeout_ms = config.getInt("connection", "connect_timeout_ms", 10000);
    rtmp_config.read_timeout_ms = config.getInt("connection", "read_timeout_ms", 3000);
    rtmp_config.write_timeout_ms = config.getInt("connection", "write_timeout_ms", 3000);
    rtmp_config.max_retry_count = config.getInt("connection", "max_retry_count", 3);
    rtmp_config.retry_interval_ms = config.getInt("connection", "retry_interval_ms", 1000);
    rtmp_config.enable_heartbeat = config.getBool("rtmp", "enable_heartbeat", true);
    rtmp_config.heartbeat_interval_ms = config.getInt("rtmp", "heartbeat_interval_ms", 30000);
    rtmp_config.enable_statistics = config.getBool("statistics", "enable_statistics", true);
    rtmp_config.max_queue_size = config.getInt("performance", "max_queue_size", 1000);
    
    client.setConfig(rtmp_config);
    
    RTMP_LOG_INFO(client, "RTMP客户端启动");
    RTMP_LOG_INFO_F(client, "参数: URL=%s, 文件=%s", rtmp_url.c_str(), flv_file.c_str());
    
    // 检查FLV文件是否存在
    if (!fs::exists(flv_file)) {
        RTMP_LOG_ERROR(client, "FLV文件不存在: " + flv_file);
        return 1;
    }
    
    auto file_size = fs::file_size(flv_file);
    RTMP_LOG_INFO_F(client, "FLV文件大小: %.2f MB", file_size / (1024.0 * 1024.0));
    
    // 使用重试机制连接
    RTMP_LOG_INFO(client, "开始连接到RTMP服务器: " + rtmp_url);
    auto start_time = std::chrono::steady_clock::now();
    
    if (!client.connectWithRetry(rtmp_url, 3)) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        RTMP_LOG_INFO(client, "PERF: 连接失败 took " + std::to_string(duration.count()) + "ms");
        RTMP_LOG_ERROR(client, "连接RTMP服务器失败");
        client.flushLogs();
        return 1;
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    RTMP_LOG_INFO(client, "PERF: 连接建立 took " + std::to_string(duration.count()) + "ms");
    
    // 启动心跳线程
    client.startHeartbeatThread();
    
    // 推送FLV文件
    RTMP_LOG_INFO(client, "开始推送FLV文件: " + flv_file);
    start_time = std::chrono::steady_clock::now();
    
    if (!client.pushFLVFile(flv_file)) {
        end_time = std::chrono::steady_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        RTMP_LOG_INFO(client, "PERF: 推流失败 took " + std::to_string(duration.count()) + "ms");
        RTMP_LOG_ERROR(client, "推送FLV文件失败");
        client.stopHeartbeatThread();
        client.flushLogs();
        return 1;
    }
    
    end_time = std::chrono::steady_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    RTMP_LOG_INFO(client, "PERF: 推流完成 took " + std::to_string(duration.count()) + "ms");
    
    // 停止心跳线程
    client.stopHeartbeatThread();
    
    // 显示最终统计
    // 获取并打印统计信息
    auto stats = client.getStatistics();
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - stats.start_time);
    
    RTMP_LOG_INFO(client, "STATS: Runtime=" + std::to_string(runtime.count()) + "s" +
                  ", Sent=" + std::to_string(stats.bytes_sent / 1024) + "KB" +
                  ", Recv=" + std::to_string(stats.bytes_received / 1024) + "KB" +
                  ", AudioFrames=" + std::to_string(stats.audio_frames) +
                  ", VideoFrames=" + std::to_string(stats.video_frames) +
                  ", Dropped=" + std::to_string(stats.dropped_frames) +
                  ", AvgBitrate=" + std::to_string(stats.avg_bitrate / 1000) + "kbps");
    RTMP_LOG_INFO(client, "推流任务成功完成");
    
    // 刷新并关闭日志
    client.flushLogs();
    client.shutdownLogger();
    
    return 0;
}
