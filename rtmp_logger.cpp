#include "rtmp_client.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/fmt/fmt.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdarg>

// 静态日志器实例
static std::shared_ptr<spdlog::logger> g_logger = nullptr;

// 初始化日志系统
bool RTMPClient::initializeLogger() {
    try {
        // 创建控制台sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
        
        // 创建文件sink
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/rtmp_client.log", 1024 * 1024 * 10, 5);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
        
        // 创建多sink日志器
        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        g_logger = std::make_shared<spdlog::logger>("rtmp_client", sinks.begin(), sinks.end());
        
        // 设置日志级别
        g_logger->set_level(spdlog::level::trace);
        g_logger->flush_on(spdlog::level::info);
        
        // 注册全局日志器
        spdlog::register_logger(g_logger);
        
        SPDLOG_LOGGER_INFO(g_logger, "RTMP Client logger initialized successfully");
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return false;
    }
}

// 设置日志级别
void RTMPClient::setLogLevel(const std::string& level) {
    if (!g_logger) {
        if (!initializeLogger()) {
            return;
        }
    }
    
    spdlog::level::level_enum log_level = spdlog::level::info;
    
    if (level == "trace") {
        log_level = spdlog::level::trace;
    } else if (level == "debug") {
        log_level = spdlog::level::debug;
    } else if (level == "info") {
        log_level = spdlog::level::info;
    } else if (level == "warn" || level == "warning") {
        log_level = spdlog::level::warn;
    } else if (level == "error") {
        log_level = spdlog::level::err;
    } else if (level == "critical") {
        log_level = spdlog::level::critical;
    } else if (level == "off") {
        log_level = spdlog::level::off;
    }
    
    g_logger->set_level(log_level);
    SPDLOG_LOGGER_INFO(g_logger, "Log level set to: {}", level);
}

// 基础日志方法 - 内部使用，包含文件名和行号
void RTMPClient::logInternal(spdlog::level::level_enum level, const char* file, int line, const std::string& message) {
    if (!g_logger) {
        if (!initializeLogger()) {
            return;
        }
    }
    
    // 提取文件名（去掉路径）
    const char* filename = strrchr(file, '/');
    if (!filename) {
        filename = strrchr(file, '\\');
    }
    filename = filename ? filename + 1 : file;
    
    // 使用spdlog的source_loc来设置文件名和行号
    spdlog::source_loc loc{filename, line, ""};
    g_logger->log(loc, level, message);
}

// 公共日志方法
void RTMPClient::logInfo(const std::string& message) {
    logInternal(spdlog::level::info, __FILE__, __LINE__, message);
}

void RTMPClient::logError(const std::string& message) {
    logInternal(spdlog::level::err, __FILE__, __LINE__, message);
}

void RTMPClient::logDebug(const std::string& message) {
    logInternal(spdlog::level::debug, __FILE__, __LINE__, message);
}

void RTMPClient::logWarn(const std::string& message) {
    logInternal(spdlog::level::warn, __FILE__, __LINE__, message);
}

// 格式化日志方法
void RTMPClient::logInfoF(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(spdlog::level::info, __FILE__, __LINE__, std::string(buffer));
}

void RTMPClient::logErrorF(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(spdlog::level::err, __FILE__, __LINE__, std::string(buffer));
}

void RTMPClient::logDebugF(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(spdlog::level::debug, __FILE__, __LINE__, std::string(buffer));
}

// 专用日志方法
void RTMPClient::logPerformance(const std::string& operation, std::chrono::milliseconds duration) {
    std::string message = "PERF: " + operation + " took " + std::to_string(duration.count()) + "ms";
    logInternal(spdlog::level::info, __FILE__, __LINE__, message);
}

void RTMPClient::logNetworkStatus(const std::string& status, const std::map<std::string, std::string>& details) {
    std::ostringstream oss;
    oss << "NETWORK: " << status;
    
    if (!details.empty()) {
        oss << " [";
        bool first = true;
        for (const auto& pair : details) {
            if (!first) oss << ", ";
            oss << pair.first << "=" << pair.second;
            first = false;
        }
        oss << "]";
    }
    
    logInternal(spdlog::level::info, __FILE__, __LINE__, oss.str());
}

void RTMPClient::logRTMPMessage(const std::string& direction, uint8_t msg_type, uint32_t timestamp, size_t data_size) {
    std::ostringstream oss;
    oss << "RTMP: " << direction << " MsgType=" << static_cast<int>(msg_type) 
        << " Timestamp=" << timestamp << " Size=" << data_size;
    
    logInternal(spdlog::level::debug, __FILE__, __LINE__, oss.str());
}

void RTMPClient::logStatistics() {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(statistics_mutex_));
    
    auto now = std::chrono::steady_clock::now();
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(now - statistics_.start_time);
    
    std::ostringstream oss;
    oss << "STATS: Runtime=" << runtime.count() << "s"
        << ", Sent=" << (statistics_.bytes_sent / 1024) << "KB"
        << ", Recv=" << (statistics_.bytes_received / 1024) << "KB"
        << ", AudioFrames=" << statistics_.audio_frames
        << ", VideoFrames=" << statistics_.video_frames
        << ", Dropped=" << statistics_.dropped_frames
        << ", AvgBitrate=" << (statistics_.avg_bitrate / 1000) << "kbps";
    
    logInternal(spdlog::level::info, __FILE__, __LINE__, oss.str());
}

// 日志控制方法
void RTMPClient::flushLogs() {
    if (g_logger) {
        g_logger->flush();
    }
}

void RTMPClient::shutdownLogger() {
    if (g_logger) {
        SPDLOG_LOGGER_INFO(g_logger, "RTMP Client logger shutting down");
        g_logger->flush();
        spdlog::drop("rtmp_client");
        g_logger.reset();
    }
}

// 格式化日志方法 - 内部使用，包含文件名和行号
void RTMPClient::logInternalF(spdlog::level::level_enum level, const char* file, int line, const char* format, ...) {
    if (!g_logger) {
        if (!initializeLogger()) {
            return;
        }
    }
    
    // 处理可变参数
    va_list args;
    va_start(args, format);
    
    // 计算需要的缓冲区大小
    int size = vsnprintf(nullptr, 0, format, args);
    va_end(args);
    
    if (size <= 0) {
        return;
    }
    
    // 分配缓冲区并格式化字符串
    std::vector<char> buffer(size + 1);
    va_start(args, format);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    
    // 提取文件名（去掉路径）
    const char* filename = strrchr(file, '/');
    if (!filename) {
        filename = strrchr(file, '\\');
    }
    filename = filename ? filename + 1 : file;
    
    // 使用spdlog的source_loc来设置文件名和行号
    spdlog::source_loc loc{filename, line, ""};
    g_logger->log(loc, level, std::string(buffer.data()));
}
