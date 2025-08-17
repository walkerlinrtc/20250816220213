#include "rtmp_client.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // 创建RTMP客户端
    RTMPClient client;
    
    // 配置客户端参数
    RTMPConfig config;
    config.connect_timeout_ms = 10000;      // 10秒连接超时
    config.read_timeout_ms = 5000;          // 5秒读取超时
    config.write_timeout_ms = 5000;         // 5秒写入超时
    config.max_retry_count = 5;             // 最大重试5次
    config.retry_interval_ms = 2000;        // 重试间隔2秒
    config.enable_heartbeat = true;         // 启用心跳
    config.heartbeat_interval_ms = 30000;   // 30秒心跳间隔
    config.enable_statistics = true;        // 启用统计
    
    client.setConfig(config);
    
    // 使用重试机制连接
    std::string rtmp_url = "rtmp://localhost:1935/live/stream";
    std::cout << "尝试连接到RTMP服务器: " << rtmp_url << std::endl;
    
    if (!client.connectWithRetry(rtmp_url, 3)) {
        std::cerr << "连接失败，退出程序" << std::endl;
        return 1;
    }
    
    // 启动心跳线程
    client.startHeartbeatThread();
    
    // 推送FLV文件
    std::string flv_file = "test.flv";
    std::cout << "开始推送FLV文件: " << flv_file << std::endl;
    
    if (!client.pushFLVFile(flv_file)) {
        std::cerr << "推送FLV文件失败" << std::endl;
        client.stopHeartbeatThread();
        return 1;
    }
    
    // 显示统计信息
    auto stats = client.getStatistics();
    std::cout << "\n=== 推流统计信息 ===" << std::endl;
    std::cout << "发送字节数: " << stats.bytes_sent << std::endl;
    std::cout << "接收字节数: " << stats.bytes_received << std::endl;
    std::cout << "发送包数: " << stats.packets_sent << std::endl;
    std::cout << "接收包数: " << stats.packets_received << std::endl;
    std::cout << "音频帧数: " << stats.audio_frames << std::endl;
    std::cout << "视频帧数: " << stats.video_frames << std::endl;
    std::cout << "丢帧数: " << stats.dropped_frames << std::endl;
    std::cout << "当前比特率: " << stats.current_bitrate << " bps" << std::endl;
    std::cout << "平均比特率: " << stats.avg_bitrate << " bps" << std::endl;
    
    // 停止心跳线程
    client.stopHeartbeatThread();
    
    std::cout << "推流完成" << std::endl;
    return 0;
}