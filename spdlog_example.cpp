#include "rtmp_client.h"
#include <iostream>

int main() {
    // 创建RTMP客户端
    RTMPClient client;
    
    // 设置日志级别为调试模式
    client.setLogLevel("debug");
    
    // 配置客户端参数
    RTMPConfig config;
    config.connect_timeout_ms = 10000;
    config.max_retry_count = 3;
    config.enable_heartbeat = true;
    config.enable_statistics = true;
    
    client.setConfig(config);
    
    // 记录启动信息
    client.logInfo("RTMP客户端启动");
    client.logInfoF("配置参数: 连接超时=%dms, 最大重试=%d次", 
                   config.connect_timeout_ms, config.max_retry_count);
    
    // 尝试连接
    std::string rtmp_url = "rtmp://localhost:1935/live/stream";
    client.logInfo("开始连接到RTMP服务器: " + rtmp_url);
    
    auto start_time = std::chrono::steady_clock::now();
    
    if (client.connectWithRetry(rtmp_url, 3)) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        client.logPerformance("连接建立", duration);
        client.logInfo("RTMP连接成功建立");
        
        // 记录网络状态
        std::map<std::string, std::string> network_details;
        network_details["server"] = "localhost:1935";
        network_details["app"] = "live";
        network_details["stream"] = "stream";
        client.logNetworkStatus("连接已建立", network_details);
        
        // 启动心跳
        client.startHeartbeatThread();
        client.logInfo("心跳线程已启动");
        
        // 推送FLV文件
        std::string flv_file = "test.flv";
        client.logInfo("开始推送FLV文件: " + flv_file);
        
        start_time = std::chrono::steady_clock::now();
        
        if (client.pushFLVFile(flv_file)) {
            end_time = std::chrono::steady_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            client.logPerformance("FLV推送", duration);
            client.logInfo("FLV文件推送成功");
            
            // 记录统计信息
            client.logStatistics();
            
        } else {
            client.logError("FLV文件推送失败");
        }
        
        // 停止心跳
        client.stopHeartbeatThread();
        client.logInfo("心跳线程已停止");
        
    } else {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        client.logPerformance("连接失败", duration);
        client.logError("RTMP连接失败");
        
        // 记录网络状态
        std::map<std::string, std::string> network_details;
        network_details["server"] = "localhost:1935";
        network_details["error"] = "连接超时或被拒绝";
        client.logNetworkStatus("连接失败", network_details);
        
        return 1;
    }
    
    // 最终统计
    client.logStatistics();
    client.logInfo("RTMP客户端正常退出");
    
    // 刷新并关闭日志
    client.flushLogs();
    client.shutdownLogger();
    
    return 0;
}