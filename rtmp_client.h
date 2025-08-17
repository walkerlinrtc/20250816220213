#ifndef RTMP_CLIENT_H
#define RTMP_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

// RTMP消息类型
enum RTMPMessageType {
    RTMP_MSG_CHUNK_SIZE = 1,
    RTMP_MSG_ABORT = 2,
    RTMP_MSG_ACK = 3,
    RTMP_MSG_USER_CONTROL = 4,
    RTMP_MSG_WINDOW_ACK_SIZE = 5,
    RTMP_MSG_SET_PEER_BANDWIDTH = 6,
    RTMP_MSG_AUDIO = 8,
    RTMP_MSG_VIDEO = 9,
    RTMP_MSG_AMF3_META = 15,
    RTMP_MSG_AMF3_SHARED = 16,
    RTMP_MSG_AMF3_COMMAND = 17,
    RTMP_MSG_AMF0_META = 18,
    RTMP_MSG_AMF0_SHARED = 19,
    RTMP_MSG_AMF0_COMMAND = 20,
    RTMP_MSG_AGGREGATE = 22
};

// FLV标签类型
enum FLVTagType {
    FLV_TAG_AUDIO = 8,
    FLV_TAG_VIDEO = 9,
    FLV_TAG_SCRIPT = 18
};

// FLV标签结构
struct FLVTag {
    uint8_t type;
    uint32_t data_size;
    uint32_t timestamp;
    uint8_t timestamp_extended;
    uint32_t stream_id;
    std::vector<uint8_t> data;
};

// RTMP消息头结构
struct RTMPMessageHeader {
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type;
    uint32_t message_stream_id;
    
    RTMPMessageHeader() : timestamp(0), message_length(0), message_type(0), message_stream_id(0) {}
};

// AMF数据类型
enum AMFType {
    AMF0_NUMBER = 0x00,
    AMF0_BOOLEAN = 0x01,
    AMF0_STRING = 0x02,
    AMF0_OBJECT = 0x03,
    AMF0_MOVIECLIP = 0x04,
    AMF0_NULL = 0x05,
    AMF0_UNDEFINED = 0x06,
    AMF0_REFERENCE = 0x07,
    AMF0_ECMA_ARRAY = 0x08,
    AMF0_OBJECT_END = 0x09,
    AMF0_STRICT_ARRAY = 0x0A,
    AMF0_DATE = 0x0B,
    AMF0_LONG_STRING = 0x0C,
    AMF0_UNSUPPORTED = 0x0D,
    AMF0_RECORDSET = 0x0E,
    AMF0_XML_DOCUMENT = 0x0F,
    AMF0_TYPED_OBJECT = 0x10,
    AMF0_AVMPLUS = 0x11,
    
    AMF3_UNDEFINED = 0x00,
    AMF3_NULL = 0x01,
    AMF3_FALSE = 0x02,
    AMF3_TRUE = 0x03,
    AMF3_INTEGER = 0x04,
    AMF3_DOUBLE = 0x05,
    AMF3_STRING = 0x06,
    AMF3_XML_DOC = 0x07,
    AMF3_DATE = 0x08,
    AMF3_ARRAY = 0x09,
    AMF3_OBJECT = 0x0A,
    AMF3_XML = 0x0B,
    AMF3_BYTE_ARRAY = 0x0C
};

// AMF值结构
struct AMFValue {
    AMFType type;
    union {
        double number;
        bool boolean;
        int32_t integer;
    };
    std::string string_value;
    std::vector<AMFValue> array_value;
    std::map<std::string, AMFValue> object_value;
    std::vector<uint8_t> byte_array;
    
    AMFValue() : type(AMF0_NULL) {}
    AMFValue(double n) : type(AMF0_NUMBER), number(n) {}
    AMFValue(bool b) : type(AMF0_BOOLEAN), boolean(b) {}
    AMFValue(const std::string& s) : type(AMF0_STRING), string_value(s) {}
    AMFValue(int32_t i) : type(AMF3_INTEGER), integer(i) {}
};

// 连接状态枚举
enum ConnectionState {
    STATE_DISCONNECTED = 0,
    STATE_CONNECTING = 1,
    STATE_HANDSHAKING = 2,
    STATE_CONNECTED = 3,
    STATE_PUBLISHING = 4,
    STATE_ERROR = 5
};

// 配置结构
struct RTMPConfig {
    uint32_t connect_timeout_ms = 5000;
    uint32_t read_timeout_ms = 3000;
    uint32_t write_timeout_ms = 3000;
    uint32_t max_retry_count = 3;
    uint32_t retry_interval_ms = 1000;
    bool enable_heartbeat = true;
    uint32_t heartbeat_interval_ms = 30000;
    bool enable_statistics = true;
    uint32_t max_queue_size = 1000;
};

// 统计信息结构
struct RTMPStatistics {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    uint64_t audio_frames = 0;
    uint64_t video_frames = 0;
    uint64_t dropped_frames = 0;
    uint32_t current_bitrate = 0;
    uint32_t avg_bitrate = 0;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
};

// RTMP客户端类
class RTMPClient {
public:
    RTMPClient();
    ~RTMPClient();
    
    // 连接到RTMP服务器
    bool connect(const std::string& url);
    
    // 带重试的连接
    bool connectWithRetry(const std::string& url, uint32_t max_retries = 3);
    
    // 断开连接
    void disconnect();
    
    // 推送FLV文件
    bool pushFLVFile(const std::string& flv_file_path);
    
    // 设置推流参数
    void setStreamKey(const std::string& stream_key);
    void setChunkSize(uint32_t chunk_size);
    void setConfig(const RTMPConfig& config);
    
    // 状态查询
    ConnectionState getConnectionState() const;
    bool isConnected() const;
    RTMPStatistics getStatistics() const;
    
    // 心跳和保活
    bool sendHeartbeat();
    void startHeartbeatThread();
    void stopHeartbeatThread();
    
private:
    // 网络相关
    int socket_fd_;
    std::string server_host_;
    int server_port_;
    std::string app_name_;
    std::string stream_key_;
    
    // RTMP协议相关
    uint32_t chunk_size_;
    uint32_t bytes_read_;
    uint32_t bytes_read_last_ack_;
    uint32_t window_ack_size_;
    
    // 连接状态和配置
    ConnectionState connection_state_;
    RTMPConfig config_;
    RTMPStatistics statistics_;
    std::string last_error_;
    
    // 心跳和线程管理
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_;
    std::mutex state_mutex_;
    std::mutex statistics_mutex_;
    
    // AMF引用表
    std::vector<std::string> amf3_string_table_;
    std::vector<AMFValue> amf3_object_table_;
    std::vector<std::vector<AMFValue>> amf3_trait_table_;
    
    // 内部方法
    bool parseURL(const std::string& url);
    bool connectSocket();
    bool handshake();
    bool sendConnect();
    bool sendCreateStream();
    bool sendPublish();
    
    // FLV文件处理
    bool readFLVHeader(std::ifstream& file);
    bool readFLVTag(std::ifstream& file, FLVTag& tag);
    bool sendFLVTag(const FLVTag& tag);
    
    // RTMP消息发送
    bool sendRTMPMessage(uint8_t msg_type, uint32_t stream_id, 
                        const std::vector<uint8_t>& data, uint32_t timestamp = 0);
    bool sendChunk(uint8_t chunk_stream_id, uint8_t msg_type, 
                   uint32_t stream_id, const std::vector<uint8_t>& data, 
                   uint32_t timestamp = 0);
    
    // 数据接收和消息解析
    bool receiveData(std::vector<uint8_t>& buffer, size_t size);
    bool receiveResponse();
    bool parseRTMPMessage(const uint8_t*& data, size_t& remaining);
    bool parseMessageHeader(const uint8_t*& data, size_t& remaining, uint8_t fmt, RTMPMessageHeader& header);
    bool handleRTMPMessage(const RTMPMessageHeader& header, const std::vector<uint8_t>& data);
    
    // RTMP消息处理
    bool handleChunkSize(const std::vector<uint8_t>& data);
    bool sendChunkSizeAck();
    bool handleAcknowledgement(const std::vector<uint8_t>& data);
    bool handleWindowAckSize(const std::vector<uint8_t>& data);
    bool handleSetPeerBandwidth(const std::vector<uint8_t>& data);
    bool handleUserControl(const std::vector<uint8_t>& data);
    bool handleAMF0Command(const std::vector<uint8_t>& data);
    bool handleAMF3Command(const std::vector<uint8_t>& data);
    bool handleCommandResult(double transaction_id, const uint8_t* data, size_t remaining);
    bool handleCommandError(double transaction_id, const uint8_t* data, size_t remaining);
    bool handleOnStatus(const uint8_t* data, size_t remaining);
    
    // 工具方法
    void writeUint32BE(std::vector<uint8_t>& buffer, uint32_t value);
    void writeUint24BE(std::vector<uint8_t>& buffer, uint32_t value);
    void writeUint16BE(std::vector<uint8_t>& buffer, uint16_t value);
    uint32_t readUint32BE(const uint8_t* data);
    uint32_t readUint24BE(const uint8_t* data);
    uint16_t readUint16BE(const uint8_t* data);
    
    // AMF0编码
    void encodeAMF0Value(std::vector<uint8_t>& buffer, const AMFValue& value);
    void encodeAMF0String(std::vector<uint8_t>& buffer, const std::string& str);
    void encodeAMF0LongString(std::vector<uint8_t>& buffer, const std::string& str);
    void encodeAMF0Number(std::vector<uint8_t>& buffer, double number);
    void encodeAMF0Boolean(std::vector<uint8_t>& buffer, bool value);
    void encodeAMF0Null(std::vector<uint8_t>& buffer);
    void encodeAMF0Object(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj);
    void encodeAMF0Array(std::vector<uint8_t>& buffer, const std::vector<AMFValue>& arr);
    void encodeAMF0EcmaArray(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj);
    
    // AMF0解码
    AMFValue decodeAMF0Value(const uint8_t*& data, size_t& remaining);
    std::string decodeAMF0String(const uint8_t*& data, size_t& remaining);
    std::string decodeAMF0LongString(const uint8_t*& data, size_t& remaining);
    double decodeAMF0Number(const uint8_t*& data, size_t& remaining);
    bool decodeAMF0Boolean(const uint8_t*& data, size_t& remaining);
    std::map<std::string, AMFValue> decodeAMF0Object(const uint8_t*& data, size_t& remaining);
    std::vector<AMFValue> decodeAMF0Array(const uint8_t*& data, size_t& remaining);
    
    // AMF3编码
    void encodeAMF3Value(std::vector<uint8_t>& buffer, const AMFValue& value);
    void encodeAMF3Integer(std::vector<uint8_t>& buffer, int32_t value);
    void encodeAMF3Double(std::vector<uint8_t>& buffer, double value);
    void encodeAMF3String(std::vector<uint8_t>& buffer, const std::string& str);
    void encodeAMF3Array(std::vector<uint8_t>& buffer, const std::vector<AMFValue>& arr);
    void encodeAMF3Object(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj);
    void encodeAMF3ByteArray(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data);
    
    // AMF3解码
    AMFValue decodeAMF3Value(const uint8_t*& data, size_t& remaining);
    int32_t decodeAMF3Integer(const uint8_t*& data, size_t& remaining);
    double decodeAMF3Double(const uint8_t*& data, size_t& remaining);
    std::string decodeAMF3String(const uint8_t*& data, size_t& remaining);
    std::vector<AMFValue> decodeAMF3Array(const uint8_t*& data, size_t& remaining);
    std::map<std::string, AMFValue> decodeAMF3Object(const uint8_t*& data, size_t& remaining);
    std::vector<uint8_t> decodeAMF3ByteArray(const uint8_t*& data, size_t& remaining);
    
    // AMF3辅助方法
    void writeAMF3U29(std::vector<uint8_t>& buffer, uint32_t value);
    uint32_t readAMF3U29(const uint8_t*& data, size_t& remaining);
    int getAMF3StringReference(const std::string& str);
    void clearAMF3References();
    
    // 状态管理
    void setState(ConnectionState state);
    void setError(const std::string& error);
    bool checkConnection();
    
    // 统计和监控
    void updateStatistics(size_t bytes_sent, size_t bytes_received);
    void updateFrameCount(uint8_t frame_type);
    
    // 超时和重试
    bool setSocketTimeout(int timeout_ms);
    bool waitForData(int timeout_ms);
    
    // 心跳线程函数
    void heartbeatThreadFunc();

public:
    // 内部日志方法（不直接调用）
    void logInternal(spdlog::level::level_enum level, const char* file, int line, const std::string& message);
    void logInternalF(spdlog::level::level_enum level, const char* file, int line, const char* format, ...);
    
    
    
    // 日志控制方法
    bool initializeLogger();
    void setLogLevel(const std::string& level);
    void flushLogs();
    void shutdownLogger();
};

#endif // RTMP_CLIENT_H
