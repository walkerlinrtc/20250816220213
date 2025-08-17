#include "rtmp_client.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <iostream>
#include <cstdarg>

// 全局日志器实例
static std::shared_ptr<spdlog::logger> g_logger = nullptr;
static std::once_flag g_logger_init_flag;

// 初始化日志系统
void initializeLogger() {
    std::call_once(g_logger_init_flag, []() {
        try {
            // 创建控制台输出sink（带颜色）
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::info);
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            
            // 创建文件输出sink（轮转日志）
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "logs/rtmp_client.log", 1024 * 1024 * 5, 3);
            file_sink->set_level(spdlog::level::debug);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
            
            // 创建多sink日志器
            std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
            g_logger = std::make_shared<spdlog::logger>("rtmp_client", sinks.begin(), sinks.end());
            
            // 设置日志级别
            g_logger->set_level(spdlog::level::debug);
            g_logger->flush_on(spdlog::level::warn);
            
            // 注册为默认日志器
            spdlog::register_logger(g_logger);
            spdlog::set_default_logger(g_logger);
            
            // 设置异步刷新间隔
            spdlog::flush_every(std::chrono::seconds(3));
            
            SPDLOG_INFO("RTMP Client logger initialized successfully");
            
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        }
    });
}

// 获取日志器实例
std::shared_ptr<spdlog::logger> getLogger() {
    initializeLogger();
    return g_logger;
}

// RTMPClient日志方法实现
void RTMPClient::logInfo(const std::string& message) {
    auto logger = getLogger();
    if (logger) {
        logger->info(message);
    }
}

void RTMPClient::logError(const std::string& message) {
    auto logger = getLogger();
    if (logger) {
        logger->error(message);
    }
    last_error_ = message;
}

void RTMPClient::logDebug(const std::string& message) {
    auto logger = getLogger();
    if (logger) {
        logger->debug(message);
    }
}

void RTMPClient::logWarn(const std::string& message) {
    auto logger = getLogger();
    if (logger) {
        logger->warn(message);
    }
}

// 带格式化的日志方法
void RTMPClient::logInfoF(const char* format, ...) {
    auto logger = getLogger();
    if (logger) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        logger->info(buffer);
    }
}

void RTMPClient::logErrorF(const char* format, ...) {
    auto logger = getLogger();
    if (logger) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        logger->error(buffer);
        last_error_ = std::string(buffer);
    }
}

void RTMPClient::logDebugF(const char* format, ...) {
    auto logger = getLogger();
    if (logger) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        logger->debug(buffer);
    }
}

// 性能日志记录
void RTMPClient::logPerformance(const std::string& operation, 
                               std::chrono::milliseconds duration) {
    auto logger = getLogger();
    if (logger) {
        logger->info("PERF: {} took {}ms", operation, duration.count());
    }
}

// 网络状态日志
void RTMPClient::logNetworkStatus(const std::string& status, 
                                 const std::map<std::string, std::string>& details) {
    auto logger = getLogger();
    if (logger) {
        std::string detail_str;
        for (const auto& pair : details) {
            if (!detail_str.empty()) detail_str += ", ";
            detail_str += pair.first + "=" + pair.second;
        }
        logger->info("NETWORK: {} [{}]", status, detail_str);
    }
}

// RTMP协议日志
void RTMPClient::logRTMPMessage(const std::string& direction, 
                               uint8_t msg_type, 
                               uint32_t timestamp, 
                               size_t data_size) {
    auto logger = getLogger();
    if (logger) {
        logger->debug("RTMP: {} msg_type={} timestamp={} size={}", 
                     direction, msg_type, timestamp, data_size);
    }
}

// 统计信息日志
void RTMPClient::logStatistics() {
    auto logger = getLogger();
    if (logger && config_.enable_statistics) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - statistics_.start_time);
        
        logger->info("STATS: Runtime={}s, Sent={}KB, Recv={}KB, "
                    "AudioFrames={}, VideoFrames={}, Dropped={}, "
                    "AvgBitrate={}kbps", 
                    duration.count(),
                    statistics_.bytes_sent / 1024,
                    statistics_.bytes_received / 1024,
                    statistics_.audio_frames,
                    statistics_.video_frames,
                    statistics_.dropped_frames,
                    statistics_.avg_bitrate / 1000);
    }
}

// 设置日志级别
void RTMPClient::setLogLevel(const std::string& level) {
    auto logger = getLogger();
    if (logger) {
        if (level == "debug") {
            logger->set_level(spdlog::level::debug);
        } else if (level == "info") {
            logger->set_level(spdlog::level::info);
        } else if (level == "warn") {
            logger->set_level(spdlog::level::warn);
        } else if (level == "error") {
            logger->set_level(spdlog::level::err);
        }
        logger->info("Log level set to: {}", level);
    }
}

// 刷新日志缓冲区
void RTMPClient::flushLogs() {
    auto logger = getLogger();
    if (logger) {
        logger->flush();
    }
}

// 关闭日志系统
void RTMPClient::shutdownLogger() {
    if (g_logger) {
        g_logger->info("RTMP Client logger shutting down");
        g_logger->flush();
        spdlog::shutdown();
    }
}