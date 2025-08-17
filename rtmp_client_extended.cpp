// RTMP客户端扩展功能实现
#include "rtmp_client.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <iomanip>
#include <sstream>

// 带重试的连接
bool RTMPClient::connectWithRetry(const std::string& url, uint32_t max_retries) {
    for (uint32_t attempt = 0; attempt <= max_retries; ++attempt) {
        RTMP_LOG_INFO(*this, "Connection attempt " + std::to_string(attempt + 1) + "/" + std::to_string(max_retries + 1));
        
        if (connect(url)) {
            RTMP_LOG_INFO(*this, "Connected successfully on attempt " + std::to_string(attempt + 1));
            return true;
        }
        
        if (attempt < max_retries) {
            RTMP_LOG_INFO(*this, "Connection failed, retrying in " + std::to_string(config_.retry_interval_ms) + "ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_interval_ms));
        }
    }
    
    setError("Failed to connect after " + std::to_string(max_retries + 1) + " attempts");
    return false;
}

// 状态管理
void RTMPClient::setState(ConnectionState state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    connection_state_ = state;
    
    switch (state) {
        case STATE_DISCONNECTED:
            RTMP_LOG_INFO(*this, "State: DISCONNECTED");
            break;
        case STATE_CONNECTING:
            RTMP_LOG_INFO(*this, "State: CONNECTING");
            break;
        case STATE_HANDSHAKING:
            RTMP_LOG_INFO(*this, "State: HANDSHAKING");
            break;
        case STATE_CONNECTED:
            RTMP_LOG_INFO(*this, "State: CONNECTED");
            break;
        case STATE_PUBLISHING:
            RTMP_LOG_INFO(*this, "State: PUBLISHING");
            break;
        case STATE_ERROR:
            RTMP_LOG_ERROR(*this, "State: ERROR - " + last_error_);
            break;
    }
}

void RTMPClient::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_ = error;
    connection_state_ = STATE_ERROR;
    RTMP_LOG_ERROR(*this, "Error: " + error);
}

void RTMPClient::setConfig(const RTMPConfig& config) {
    config_ = config;
    RTMP_LOG_INFO(*this, "Configuration updated");
}

bool RTMPClient::isConnected() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return connection_state_ == STATE_CONNECTED || connection_state_ == STATE_PUBLISHING;
}

// 检查连接状态
bool RTMPClient::checkConnection() {
    if (socket_fd_ < 0) {
        setError("Socket is not valid");
        return false;
    }
    
    // 使用select检查socket状态
    fd_set read_fds, error_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&error_fds);
    FD_SET(socket_fd_, &read_fds);
    FD_SET(socket_fd_, &error_fds);
    
    struct timeval timeout = {0, 0}; // 非阻塞检查
    int result = select(socket_fd_ + 1, &read_fds, nullptr, &error_fds, &timeout);
    
    if (result < 0) {
        setError("Socket select error: " + std::string(strerror(errno)));
        return false;
    }
    
    if (FD_ISSET(socket_fd_, &error_fds)) {
        setError("Socket error detected");
        return false;
    }
    
    return true;
}

// 统计信息更新
void RTMPClient::updateStatistics(size_t bytes_sent, size_t bytes_received) {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    statistics_.bytes_sent += bytes_sent;
    statistics_.bytes_received += bytes_received;
    
    if (bytes_sent > 0) statistics_.packets_sent++;
    if (bytes_received > 0) statistics_.packets_received++;
    
    // 更新比特率
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - statistics_.last_update);
    
    if (duration.count() >= 1) {
        statistics_.current_bitrate = static_cast<uint32_t>(bytes_sent * 8 / duration.count());
        
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(now - statistics_.start_time);
        if (total_duration.count() > 0) {
            statistics_.avg_bitrate = static_cast<uint32_t>(statistics_.bytes_sent * 8 / total_duration.count());
        }
        
        statistics_.last_update = now;
    }
}

void RTMPClient::updateFrameCount(uint8_t frame_type) {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    switch (frame_type) {
        case FLV_TAG_AUDIO:
            statistics_.audio_frames++;
            break;
        case FLV_TAG_VIDEO:
            statistics_.video_frames++;
            break;
    }
}

RTMPStatistics RTMPClient::getStatistics() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(statistics_mutex_));
    return statistics_;
}

// 配置设置
void RTMPClient::setConfig(const RTMPConfig& config) {
    config_ = config;
    RTMP_LOG_INFO(*this, "Configuration updated");
}

// Socket超时设置
bool RTMPClient::setSocketTimeout(int timeout_ms) {
    if (socket_fd_ < 0) return false;
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        RTMP_LOG_ERROR(*this, "Failed to set receive timeout: " + std::string(strerror(errno)));
        return false;
    }
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        RTMP_LOG_ERROR(*this, "Failed to set send timeout: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

// 等待数据可读
bool RTMPClient::waitForData(int timeout_ms) {
    if (socket_fd_ < 0) return false;
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_fd_, &read_fds);
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    
    if (result < 0) {
        RTMP_LOG_ERROR(*this, "Select error: " + std::string(strerror(errno)));
        return false;
    }
    
    return result > 0 && FD_ISSET(socket_fd_, &read_fds);
}

// 心跳功能
bool RTMPClient::sendHeartbeat() {
    if (!isConnected()) {
        return false;
    }
    
    // 发送Ping消息
    std::vector<uint8_t> ping_data(6);
    writeUint16BE(ping_data, 6); // Ping事件类型
    writeUint32BE(ping_data, static_cast<uint32_t>(std::time(nullptr))); // 时间戳
    
    bool result = sendRTMPMessage(RTMP_MSG_USER_CONTROL, 0, ping_data);
    if (result) {
        RTMP_LOG_DEBUG(*this, "Heartbeat sent");
    } else {
        RTMP_LOG_ERROR(*this, "Failed to send heartbeat");
    }
    
    return result;
}

void RTMPClient::startHeartbeatThread() {
    if (!config_.enable_heartbeat || heartbeat_running_) {
        return;
    }
    
    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread(&RTMPClient::heartbeatThreadFunc, this);
    RTMP_LOG_INFO(*this, "Heartbeat thread started");
}

void RTMPClient::stopHeartbeatThread() {
    if (!heartbeat_running_) {
        return;
    }
    
    heartbeat_running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    RTMP_LOG_INFO(*this, "Heartbeat thread stopped");
}

void RTMPClient::heartbeatThreadFunc() {
    while (heartbeat_running_) {
        if (isConnected()) {
            if (!sendHeartbeat()) {
                RTMP_LOG_ERROR(*this, "Heartbeat failed, connection may be lost");
                setError("Heartbeat failed");
                break;
            }
        }
        
        // 等待心跳间隔
        for (uint32_t i = 0; i < config_.heartbeat_interval_ms / 100 && heartbeat_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// 注意：日志方法的实现现在在rtmp_logger.cpp中，这里不再重复实现
