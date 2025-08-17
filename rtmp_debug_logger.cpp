// RTMP客户端调试日志增强
#include "rtmp_client.h"
#include <iomanip>
#include <sstream>

// 为现有方法添加详细的debug日志
void RTMPClient::logConnectionDetails() {
    RTMP_LOG_DEBUG(*this, "=== 连接详细信息 ===");
    RTMP_LOG_DEBUG(*this, "Socket FD: " + std::to_string(socket_fd_));
    RTMP_LOG_DEBUG(*this, "服务器地址: " + host_ + ":" + std::to_string(port_));
    RTMP_LOG_DEBUG(*this, "应用名: " + app_);
    RTMP_LOG_DEBUG(*this, "流名: " + stream_key_);
    RTMP_LOG_DEBUG(*this, "块大小: " + std::to_string(chunk_size_));
    RTMP_LOG_DEBUG(*this, "窗口确认大小: " + std::to_string(window_ack_size_));
}

void RTMPClient::logHandshakeStep(const std::string& step, const std::vector<uint8_t>& data) {
    RTMP_LOG_DEBUG(*this, "握手步骤: " + step);
    RTMP_LOG_DEBUG(*this, "数据大小: " + std::to_string(data.size()) + " 字节");
    
    if (data.size() > 0) {
        std::ostringstream hex_stream;
        hex_stream << "数据内容(前32字节): ";
        size_t show_bytes = std::min(data.size(), static_cast<size_t>(32));
        for (size_t i = 0; i < show_bytes; ++i) {
            hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
        }
        if (data.size() > 32) {
            hex_stream << "...";
        }
        RTMP_LOG_DEBUG(*this, hex_stream.str());
    }
}

void RTMPClient::logRTMPChunk(const std::string& direction, uint8_t chunk_stream_id, 
                             uint8_t msg_type, uint32_t timestamp, size_t data_size) {
    std::ostringstream oss;
    oss << "RTMP块 " << direction << " - "
        << "ChunkStreamID=" << static_cast<int>(chunk_stream_id)
        << ", MsgType=" << static_cast<int>(msg_type)
        << ", Timestamp=" << timestamp
        << ", Size=" << data_size;
    
    RTMP_LOG_DEBUG(*this, oss.str());
    
    // 解释消息类型
    std::string msg_type_name;
    switch (msg_type) {
        case 1: msg_type_name = "Chunk Size"; break;
        case 2: msg_type_name = "Abort Message"; break;
        case 3: msg_type_name = "Acknowledgement"; break;
        case 4: msg_type_name = "User Control Message"; break;
        case 5: msg_type_name = "Window Acknowledgement Size"; break;
        case 6: msg_type_name = "Set Peer Bandwidth"; break;
        case 8: msg_type_name = "Audio Message"; break;
        case 9: msg_type_name = "Video Message"; break;
        case 15: msg_type_name = "AMF3 Data Message"; break;
        case 16: msg_type_name = "AMF3 Shared Object Message"; break;
        case 17: msg_type_name = "AMF3 Command Message"; break;
        case 18: msg_type_name = "AMF0 Data Message"; break;
        case 19: msg_type_name = "AMF0 Shared Object Message"; break;
        case 20: msg_type_name = "AMF0 Command Message"; break;
        case 22: msg_type_name = "Aggregate Message"; break;
        default: msg_type_name = "Unknown"; break;
    }
    RTMP_LOG_DEBUG(*this, "消息类型: " + msg_type_name);
}

void RTMPClient::logAMF0Data(const std::string& context, const std::vector<uint8_t>& data) {
    RTMP_LOG_DEBUG(*this, "AMF0数据 " + context + " - 大小: " + std::to_string(data.size()) + " 字节");
    
    if (data.size() > 0) {
        std::ostringstream hex_stream;
        hex_stream << "AMF0原始数据: ";
        size_t show_bytes = std::min(data.size(), static_cast<size_t>(64));
        for (size_t i = 0; i < show_bytes; ++i) {
            hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
        }
        if (data.size() > 64) {
            hex_stream << "...";
        }
        RTMP_LOG_DEBUG(*this, hex_stream.str());
        
        // 尝试解析AMF0数据
        const uint8_t* ptr = data.data();
        size_t remaining = data.size();
        int value_count = 0;
        
        while (remaining > 0 && value_count < 5) {
            if (remaining < 1) break;
            
            uint8_t type = *ptr;
            std::string type_name;
            
            switch (type) {
                case 0x00: type_name = "Number"; break;
                case 0x01: type_name = "Boolean"; break;
                case 0x02: type_name = "String"; break;
                case 0x03: type_name = "Object"; break;
                case 0x04: type_name = "MovieClip"; break;
                case 0x05: type_name = "Null"; break;
                case 0x06: type_name = "Undefined"; break;
                case 0x07: type_name = "Reference"; break;
                case 0x08: type_name = "ECMA Array"; break;
                case 0x09: type_name = "Object End"; break;
                case 0x0A: type_name = "Strict Array"; break;
                case 0x0B: type_name = "Date"; break;
                case 0x0C: type_name = "Long String"; break;
                default: type_name = "Unknown(0x" + std::to_string(type) + ")"; break;
            }
            
            RTMP_LOG_DEBUG(*this, "AMF0值[" + std::to_string(value_count) + "]: " + type_name);
            
            // 简单跳过这个值（不完整解析）
            ptr++;
            remaining--;
            
            if (type == 0x02 && remaining >= 2) { // String
                uint16_t len = (ptr[0] << 8) | ptr[1];
                ptr += 2;
                remaining -= 2;
                if (remaining >= len) {
                    std::string str(reinterpret_cast<const char*>(ptr), len);
                    RTMP_LOG_DEBUG(*this, "  字符串值: \"" + str + "\"");
                    ptr += len;
                    remaining -= len;
                }
            } else if (type == 0x00 && remaining >= 8) { // Number
                ptr += 8;
                remaining -= 8;
            } else if (type == 0x01 && remaining >= 1) { // Boolean
                bool val = (*ptr != 0);
                RTMP_LOG_DEBUG(*this, "  布尔值: " + std::string(val ? "true" : "false"));
                ptr++;
                remaining--;
            } else if (type == 0x05) { // Null
                RTMP_LOG_DEBUG(*this, "  空值");
            } else {
                // 其他类型，跳过剩余数据
                break;
            }
            
            value_count++;
        }
    }
}

void RTMPClient::logSocketOperation(const std::string& operation, ssize_t result, int error_code) {
    if (result >= 0) {
        RTMP_LOG_DEBUG(*this, "Socket " + operation + " 成功: " + std::to_string(result) + " 字节");
    } else {
        RTMP_LOG_ERROR(*this, "Socket " + operation + " 失败: " + std::string(strerror(error_code)));
    }
}

void RTMPClient::logFLVTag(const std::string& context, uint8_t tag_type, uint32_t timestamp, size_t data_size) {
    std::string tag_type_name;
    switch (tag_type) {
        case 8: tag_type_name = "Audio"; break;
        case 9: tag_type_name = "Video"; break;
        case 18: tag_type_name = "Script Data"; break;
        default: tag_type_name = "Unknown(" + std::to_string(tag_type) + ")"; break;
    }
    
    RTMP_LOG_DEBUG(*this, "FLV标签 " + context + " - 类型: " + tag_type_name + 
                   ", 时间戳: " + std::to_string(timestamp) + "ms" +
                   ", 大小: " + std::to_string(data_size) + " 字节");
}

void RTMPClient::logConnectionState(const std::string& from_state, const std::string& to_state, const std::string& reason) {
    RTMP_LOG_DEBUG(*this, "连接状态变化: " + from_state + " -> " + to_state + 
                   (reason.empty() ? "" : " (原因: " + reason + ")"));
}

void RTMPClient::logBufferStatus(size_t send_buffer_size, size_t recv_buffer_size, size_t queue_size) {
    RTMP_LOG_DEBUG(*this, "缓冲区状态 - 发送: " + std::to_string(send_buffer_size) + 
                   " 字节, 接收: " + std::to_string(recv_buffer_size) + 
                   " 字节, 队列: " + std::to_string(queue_size) + " 项");
}

void RTMPClient::logTimingInfo(const std::string& operation, uint32_t expected_time, uint32_t actual_time) {
    int32_t diff = static_cast<int32_t>(actual_time) - static_cast<int32_t>(expected_time);
    std::string status = (abs(diff) > 100) ? " [时间偏差大]" : " [正常]";
    
    RTMP_LOG_DEBUG(*this, "时间信息 " + operation + " - 期望: " + std::to_string(expected_time) + 
                   "ms, 实际: " + std::to_string(actual_time) + "ms, 偏差: " + 
                   std::to_string(diff) + "ms" + status);
}

void RTMPClient::logErrorDetails(const std::string& operation, int error_code, const std::string& additional_info) {
    RTMP_LOG_ERROR(*this, "错误详情 - 操作: " + operation + 
                   ", 错误码: " + std::to_string(error_code) + 
                   ", 错误信息: " + std::string(strerror(error_code)) +
                   (additional_info.empty() ? "" : ", 附加信息: " + additional_info));
}

void RTMPClient::logMemoryUsage() {
    // 简单的内存使用情况记录
    RTMP_LOG_DEBUG(*this, "内存使用情况:");
    RTMP_LOG_DEBUG(*this, "  AMF3字符串表大小: " + std::to_string(amf3_string_table_.size()));
    RTMP_LOG_DEBUG(*this, "  AMF3对象表大小: " + std::to_string(amf3_object_table_.size()));
    RTMP_LOG_DEBUG(*this, "  AMF3特征表大小: " + std::to_string(amf3_trait_table_.size()));
}

void RTMPClient::dumpHexData(const std::string& title, const std::vector<uint8_t>& data, size_t max_bytes) {
    if (data.empty()) {
        RTMP_LOG_DEBUG(*this, title + ": (空数据)");
        return;
    }
    
    size_t dump_size = std::min(data.size(), max_bytes);
    std::ostringstream oss;
    oss << title << " (" << data.size() << " 字节):\n";
    
    for (size_t i = 0; i < dump_size; i += 16) {
        // 地址
        oss << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        
        // 十六进制数据
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < dump_size) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]) << " ";
            } else {
                oss << "   ";
            }
        }
        
        oss << " ";
        
        // ASCII数据
        for (size_t j = 0; j < 16 && i + j < dump_size; ++j) {
            char c = static_cast<char>(data[i + j]);
            oss << (isprint(c) ? c : '.');
        }
        
        oss << "\n";
    }
    
    if (data.size() > max_bytes) {
        oss << "... (还有 " << (data.size() - max_bytes) << " 字节未显示)";
    }
    
    RTMP_LOG_DEBUG(*this, oss.str());
}