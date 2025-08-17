#include "rtmp_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>

RTMPClient::RTMPClient() 
    : socket_fd_(-1)
    , server_port_(1935)
    , chunk_size_(128)
    , bytes_read_(0)
    , bytes_read_last_ack_(0)
    , window_ack_size_(2500000) {
}

RTMPClient::~RTMPClient() {
    disconnect();
}

bool RTMPClient::connect(const std::string& url) {
    RTMP_LOG_DEBUG(*this, "开始连接到RTMP服务器: " + url);
    
    // 解析URL
    RTMP_LOG_DEBUG(*this, "解析RTMP URL");
    if (!parseURL(url)) {
        RTMP_LOG_ERROR(*this, "URL解析失败");
        return false;
    }
    RTMP_LOG_DEBUG(*this, "URL解析成功 - Host: " + server_host_ + ", Port: " + std::to_string(server_port_) + ", App: " + app_name_ + ", Stream: " + stream_key_);
    
    // 创建socket
    RTMP_LOG_DEBUG(*this, "创建TCP socket");
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        setError("Failed to create socket: " + std::string(strerror(errno)));
        RTMP_LOG_ERROR(*this, "Socket创建失败: " + std::string(strerror(errno)));
        return false;
    }
    RTMP_LOG_DEBUG(*this, "Socket创建成功, fd=" + std::to_string(socket_fd_));
    
    // 设置socket为非阻塞模式
    RTMP_LOG_DEBUG(*this, "设置socket为非阻塞模式");
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // 连接到服务器
    RTMP_LOG_DEBUG(*this, "准备连接到服务器地址");
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_);
    
    if (inet_pton(AF_INET, server_host_.c_str(), &server_addr.sin_addr) <= 0) {
        setErrror("Invalid server address: " + server_host_);
        RTMP_LOG_ERROR(*this, "无效的服务器地址: " + server_host_);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "服务器地址解析成功");
    
    RTMP_LOG_DEBUG(*this, "发起TCP连接");
    int result = ::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        setError("Failed to connect: " + std::string(strerror(errno)));
        RTMP_LOG_ERROR(*this, "TCP连接失败: " + std::string(strerror(errno)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "TCP连接已发起，等待完成");
    
    // 等待连接完成
    RTMP_LOG_DEBUG(*this, "使用select等待连接完成，超时: " + std::to_string(config_.connect_timeout_ms) + "ms");
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(socket_fd_, &write_fds);
    
    struct timeval timeout;
    timeout.tv_sec = config_.connect_timeout_ms / 1000;
    timeout.tv_usec = (config_.connect_timeout_ms % 1000) * 1000;
    
    result = select(socket_fd_ + 1, nullptr, &write_fds, nullptr, &timeout);
    if (result <= 0) {
        setError("Connection timeout or error");
        RTMP_LOG_ERROR(*this, "连接超时或错误, select result=" + std::to_string(result));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "select返回成功，检查连接状态");
    
    // 检查连接是否成功
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        setError("Connection failed: " + std::string(strerror(error)));
        RTMP_LOG_ERROR(*this, "连接失败: " + std::string(strerror(error)));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "TCP连接建立成功");
    
    // 设置socket为阻塞模式
    RTMP_LOG_DEBUG(*this, "设置socket为阻塞模式");
    flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
    
    // 设置socket超时
    RTMP_LOG_DEBUG(*this, "设置socket超时: " + std::to_string(config_.read_timeout_ms) + "ms");
    setSocketTimeout(config_.read_timeout_ms);
    
    setState(STATE_CONNECTED);
    
    // 执行RTMP握手
    RTMP_LOG_DEBUG(*this, "开始RTMP握手");
    if (!handshake()) {
        RTMP_LOG_ERROR(*this, "RTMP握手失败");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "RTMP握手完成");
    
    // 发送connect命令
    RTMP_LOG_DEBUG(*this, "发送RTMP connect命令");
    if (!sendConnect()) {
        RTMP_LOG_ERROR(*this, "发送connect命令失败");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "connect命令发送成功");
    
    // 发送createStream命令
    RTMP_LOG_DEBUG(*this, "发送RTMP createStream命令");
    if (!sendCreateStream()) {
        RTMP_LOG_ERROR(*this, "发送createStream命令失败");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "createStream命令发送成功");
    
    // 发送publish命令
    RTMP_LOG_DEBUG(*this, "发送RTMP publish命令");
    if (!sendPublish()) {
        RTMP_LOG_ERROR(*this, "发送publish命令失败");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    RTMP_LOG_DEBUG(*this, "publish命令发送成功");
    
    setState(STATE_PUBLISHING);
    RTMP_LOG_DEBUG(*this, "RTMP连接和初始化完成，进入推流状态");
    return true;
}

bool RTMPClient::handshake() {
    // RTMP握手过程
    std::vector<uint8_t> c0c1(1537);
    c0c1[0] = 0x03; // RTMP版本
    
    // 生成随机数据
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 1; i < 1537; i++) {
        c0c1[i] = dis(gen);
    }
    
    // 发送C0+C1
    if (send(socket_fd_, c0c1.data(), c0c1.size(), 0) != c0c1.size()) {
        return false;
    }
    
    // 接收S0+S1
    std::vector<uint8_t> s0s1(1537);
    if (!receiveData(s0s1, 1537)) {
        return false;
    }
    
    // 发送C2 (回显S1的时间戳和随机数据)
    std::vector<uint8_t> c2(1536);
    memcpy(c2.data(), s0s1.data() + 1, 1536);
    
    if (send(socket_fd_, c2.data(), c2.size(), 0) != c2.size()) {
        return false;
    }
    
    // 接收S2
    std::vector<uint8_t> s2(1536);
    if (!receiveData(s2, 1536)) {
        return false;
    }
    
    return true;
}

bool RTMPClient::sendConnect() {
    std::vector<uint8_t> data;
    
    // 直接编码AMF0数据，不使用AMFValue包装
    // 1. 命令名 "connect"
    data.push_back(AMF0_STRING);
    std::string command = "connect";
    writeUint16BE(data, command.length());
    data.insert(data.end(), command.begin(), command.end());
    
    // 2. 事务ID (1.0)
    data.push_back(AMF0_NUMBER);
    double transaction_id = 1.0;
    uint64_t* num_ptr = reinterpret_cast<uint64_t*>(&transaction_id);
    uint64_t num = *num_ptr;
    for (int i = 7; i >= 0; i--) {
        data.push_back((num >> (i * 8)) & 0xFF);
    }
    
    // 3. 连接对象
    data.push_back(AMF0_OBJECT);
    
    // app
    std::string app_key = "app";
    writeUint16BE(data, app_key.length());
    data.insert(data.end(), app_key.begin(), app_key.end());
    data.push_back(AMF0_STRING);
    writeUint16BE(data, app_name_.length());
    data.insert(data.end(), app_name_.begin(), app_name_.end());
    
    // type
    std::string type_key = "type";
    writeUint16BE(data, type_key.length());
    data.insert(data.end(), type_key.begin(), type_key.end());
    data.push_back(AMF0_STRING);
    std::string type_val = "nonprivate";
    writeUint16BE(data, type_val.length());
    data.insert(data.end(), type_val.begin(), type_val.end());
    
    // flashVer
    std::string flash_key = "flashVer";
    writeUint16BE(data, flash_key.length());
    data.insert(data.end(), flash_key.begin(), flash_key.end());
    data.push_back(AMF0_STRING);
    std::string flash_val = "FMLE/3.0 (compatible; FMSc/1.0)";
    writeUint16BE(data, flash_val.length());
    data.insert(data.end(), flash_val.begin(), flash_val.end());
    
    // tcUrl
    std::string tcurl_key = "tcUrl";
    writeUint16BE(data, tcurl_key.length());
    data.insert(data.end(), tcurl_key.begin(), tcurl_key.end());
    data.push_back(AMF0_STRING);
    std::string tcurl_val = "rtmp://" + server_host_ + ":" + std::to_string(server_port_) + "/" + app_name_;
    writeUint16BE(data, tcurl_val.length());
    data.insert(data.end(), tcurl_val.begin(), tcurl_val.end());
    
    // 对象结束标记
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(AMF0_OBJECT_END);
    
    if (!sendRTMPMessage(RTMP_MSG_AMF0_COMMAND, 0, data)) {
        return false;
    }
    
    return receiveResponse();
}

bool RTMPClient::sendCreateStream() {
    std::vector<uint8_t> data;
    
    // 1. 命令名 "createStream"
    data.push_back(AMF0_STRING);
    std::string command = "createStream";
    writeUint16BE(data, command.length());
    data.insert(data.end(), command.begin(), command.end());
    
    // 2. 事务ID (2.0)
    data.push_back(AMF0_NUMBER);
    double transaction_id = 2.0;
    uint64_t* num_ptr = reinterpret_cast<uint64_t*>(&transaction_id);
    uint64_t num = *num_ptr;
    for (int i = 7; i >= 0; i--) {
        data.push_back((num >> (i * 8)) & 0xFF);
    }
    
    // 3. null值
    data.push_back(AMF0_NULL);
    
    if (!sendRTMPMessage(RTMP_MSG_AMF0_COMMAND, 0, data)) {
        return false;
    }
    
    return receiveResponse();
}

bool RTMPClient::sendPublish() {
    std::vector<uint8_t> data;
    
    // 1. 命令名 "publish"
    data.push_back(AMF0_STRING);
    std::string command = "publish";
    writeUint16BE(data, command.length());
    data.insert(data.end(), command.begin(), command.end());
    
    // 2. 事务ID (3.0)
    data.push_back(AMF0_NUMBER);
    double transaction_id = 3.0;
    uint64_t* num_ptr = reinterpret_cast<uint64_t*>(&transaction_id);
    uint64_t num = *num_ptr;
    for (int i = 7; i >= 0; i--) {
        data.push_back((num >> (i * 8)) & 0xFF);
    }
    
    // 3. null值
    data.push_back(AMF0_NULL);
    
    // 4. 流名称
    data.push_back(AMF0_STRING);
    writeUint16BE(data, stream_key_.length());
    data.insert(data.end(), stream_key_.begin(), stream_key_.end());
    
    // 5. 发布类型 "live"
    data.push_back(AMF0_STRING);
    std::string publish_type = "live";
    writeUint16BE(data, publish_type.length());
    data.insert(data.end(), publish_type.begin(), publish_type.end());
    
    if (!sendRTMPMessage(RTMP_MSG_AMF0_COMMAND, 1, data)) {
        return false;
    }
    
    return receiveResponse();
}

bool RTMPClient::pushFLVFile(const std::string& flv_file_path) {
    std::ifstream file(flv_file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open FLV file: " << flv_file_path << std::endl;
        return false;
    }
    
    if (!readFLVHeader(file)) {
        std::cerr << "Invalid FLV file header" << std::endl;
        return false;
    }
    
    FLVTag tag;
    uint32_t start_time = 0;
    bool first_tag = true;
    
    while (readFLVTag(file, tag)) {
        if (first_tag) {
            start_time = tag.timestamp;
            first_tag = false;
        }
        
        // 调整时间戳为相对时间
        uint32_t relative_timestamp = tag.timestamp - start_time;
        
        if (!sendFLVTag(tag)) {
            std::cerr << "Failed to send FLV tag" << std::endl;
            return false;
        }
        
        // 精确的时间戳控制
        static uint32_t last_timestamp = 0;
        static auto start_time = std::chrono::steady_clock::now();
        
        if (relative_timestamp > last_timestamp) {
            uint32_t time_diff = relative_timestamp - last_timestamp;
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            
            if (elapsed < relative_timestamp) {
                uint32_t sleep_time = relative_timestamp - elapsed;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
            }
        }
        last_timestamp = relative_timestamp;
    }
    
    file.close();
    RTMP_LOG_INFO(*this, "FLV文件推送成功");
    return true;
}

bool RTMPClient::readFLVHeader(std::ifstream& file) {
    uint8_t header[9];
    file.read(reinterpret_cast<char*>(header), 9);
    
    if (file.gcount() != 9) {
        return false;
    }
    
    // 检查FLV签名
    if (header[0] != 'F' || header[1] != 'L' || header[2] != 'V') {
        return false;
    }
    
    // 跳过第一个previous tag size
    uint32_t prev_tag_size;
    file.read(reinterpret_cast<char*>(&prev_tag_size), 4);
    
    return file.good();
}

bool RTMPClient::readFLVTag(std::ifstream& file, FLVTag& tag) {
    uint8_t tag_header[11];
    file.read(reinterpret_cast<char*>(tag_header), 11);
    
    if (file.gcount() != 11) {
        return false;
    }
    
    tag.type = tag_header[0];
    tag.data_size = readUint24BE(tag_header + 1);
    tag.timestamp = readUint24BE(tag_header + 4);
    tag.timestamp_extended = tag_header[7];
    tag.stream_id = readUint24BE(tag_header + 8);
    
    // 组合完整时间戳
    tag.timestamp |= (tag.timestamp_extended << 24);
    
    // 读取标签数据
    tag.data.resize(tag.data_size);
    file.read(reinterpret_cast<char*>(tag.data.data()), tag.data_size);
    
    if (file.gcount() != tag.data_size) {
        return false;
    }
    
    // 跳过previous tag size
    uint32_t prev_tag_size;
    file.read(reinterpret_cast<char*>(&prev_tag_size), 4);
    
    return file.good();
}

bool RTMPClient::sendFLVTag(const FLVTag& tag) {
    uint8_t msg_type;
    
    switch (tag.type) {
        case FLV_TAG_AUDIO:
            msg_type = RTMP_MSG_AUDIO;
            break;
        case FLV_TAG_VIDEO:
            msg_type = RTMP_MSG_VIDEO;
            break;
        case FLV_TAG_SCRIPT:
            msg_type = RTMP_MSG_AMF0_META;
            break;
        default:
            return true; // 跳过未知类型
    }
    
    return sendRTMPMessage(msg_type, 1, tag.data, tag.timestamp);
}

bool RTMPClient::sendRTMPMessage(uint8_t msg_type, uint32_t stream_id, 
                                const std::vector<uint8_t>& data, uint32_t timestamp) {
    return sendChunk(2, msg_type, stream_id, data, timestamp);
}

bool RTMPClient::sendChunk(uint8_t chunk_stream_id, uint8_t msg_type, 
                          uint32_t stream_id, const std::vector<uint8_t>& data, 
                          uint32_t timestamp) {
    size_t data_size = data.size();
    size_t sent = 0;
    
    while (sent < data_size) {
        std::vector<uint8_t> chunk;
        
        // Chunk基本头
        if (sent == 0) {
            chunk.push_back(chunk_stream_id); // fmt=0, chunk stream id
            
            // 消息头 (Type 0 - 11字节)
            writeUint24BE(chunk, timestamp);
            writeUint24BE(chunk, data_size);
            chunk.push_back(msg_type);
            
            // 修复：使用小端序写入stream_id
            chunk.push_back(stream_id & 0xFF);
            chunk.push_back((stream_id >> 8) & 0xFF);
            chunk.push_back((stream_id >> 16) & 0xFF);
            chunk.push_back((stream_id >> 24) & 0xFF);
        } else {
            chunk.push_back(0xC0 | chunk_stream_id); // fmt=3, chunk stream id
        }
        
        // 数据部分
        size_t chunk_data_size = std::min(static_cast<size_t>(chunk_size_), data_size - sent);
        chunk.insert(chunk.end(), data.begin() + sent, data.begin() + sent + chunk_data_size);
        
        if (send(socket_fd_, chunk.data(), chunk.size(), 0) != chunk.size()) {
            return false;
        }
        
        sent += chunk_data_size;
    }
    
    return true;
}

bool RTMPClient::receiveData(std::vector<uint8_t>& buffer, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(socket_fd_, buffer.data() + received, size - received, 0);
        if (n <= 0) {
            return false;
        }
        received += n;
    }
    return true;
}

bool RTMPClient::receiveResponse() {
    std::vector<uint8_t> buffer(4096);
    ssize_t n = recv(socket_fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
    
    if (n <= 0) {
        // 非阻塞接收，可能暂时没有数据
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return true; // 暂时没有数据，但连接正常
        }
        return false; // 连接错误
    }
    
    // 解析接收到的RTMP消息
    const uint8_t* data = buffer.data();
    size_t remaining = n;
    
    while (remaining > 0) {
        if (!parseRTMPMessage(data, remaining)) {
            std::cerr << "Failed to parse RTMP message" << std::endl;
            return false;
        }
    }
    
    return true;
}

bool RTMPClient::parseRTMPMessage(const uint8_t*& data, size_t& remaining) {
    if (remaining < 1) {
        return false;
    }
    
    // 解析Chunk基本头
    uint8_t basic_header = *data++;
    remaining--;
    
    uint8_t fmt = (basic_header >> 6) & 0x03;
    uint8_t chunk_stream_id = basic_header & 0x3F;
    
    // 处理扩展的chunk stream id
    if (chunk_stream_id == 0) {
        if (remaining < 1) return false;
        chunk_stream_id = *data++ + 64;
        remaining--;
    } else if (chunk_stream_id == 1) {
        if (remaining < 2) return false;
        chunk_stream_id = (*data++ * 256) + *data++ + 64;
        remaining -= 2;
    }
    
    // 根据fmt解析消息头
    RTMPMessageHeader msg_header = {};
    if (!parseMessageHeader(data, remaining, fmt, msg_header)) {
        return false;
    }
    
    // 读取消息数据
    size_t chunk_data_size = std::min(static_cast<size_t>(chunk_size_), 
                                     static_cast<size_t>(msg_header.message_length));
    
    if (remaining < chunk_data_size) {
        return false; // 数据不完整
    }
    
    std::vector<uint8_t> chunk_data(data, data + chunk_data_size);
    data += chunk_data_size;
    remaining -= chunk_data_size;
    
    // 处理消息
    return handleRTMPMessage(msg_header, chunk_data);
}

bool RTMPClient::parseMessageHeader(const uint8_t*& data, size_t& remaining, 
                                   uint8_t fmt, RTMPMessageHeader& header) {
    switch (fmt) {
        case 0: // Type 0 - 11字节消息头
            if (remaining < 11) return false;
            header.timestamp = readUint24BE(data);
            data += 3;
            header.message_length = readUint24BE(data);
            data += 3;
            header.message_type = *data++;
            header.message_stream_id = readUint32BE(data);
            data += 4;
            remaining -= 11;
            
            // 处理扩展时间戳
            if (header.timestamp == 0xFFFFFF) {
                if (remaining < 4) return false;
                header.timestamp = readUint32BE(data);
                data += 4;
                remaining -= 4;
            }
            break;
            
        case 1: // Type 1 - 7字节消息头
            if (remaining < 7) return false;
            header.timestamp = readUint24BE(data);
            data += 3;
            header.message_length = readUint24BE(data);
            data += 3;
            header.message_type = *data++;
            remaining -= 7;
            
            if (header.timestamp == 0xFFFFFF) {
                if (remaining < 4) return false;
                header.timestamp = readUint32BE(data);
                data += 4;
                remaining -= 4;
            }
            break;
            
        case 2: // Type 2 - 3字节消息头
            if (remaining < 3) return false;
            header.timestamp = readUint24BE(data);
            data += 3;
            remaining -= 3;
            
            if (header.timestamp == 0xFFFFFF) {
                if (remaining < 4) return false;
                header.timestamp = readUint32BE(data);
                data += 4;
                remaining -= 4;
            }
            break;
            
        case 3: // Type 3 - 无消息头
            // 使用之前的消息头信息
            break;
    }
    
    return true;
}

bool RTMPClient::handleRTMPMessage(const RTMPMessageHeader& header, 
                                  const std::vector<uint8_t>& data) {
    switch (header.message_type) {
        case RTMP_MSG_CHUNK_SIZE:
            return handleChunkSize(data);
            
        case RTMP_MSG_ACK:
            return handleAcknowledgement(data);
            
        case RTMP_MSG_WINDOW_ACK_SIZE:
            return handleWindowAckSize(data);
            
        case RTMP_MSG_SET_PEER_BANDWIDTH:
            return handleSetPeerBandwidth(data);
            
        case RTMP_MSG_USER_CONTROL:
            return handleUserControl(data);
            
        case RTMP_MSG_AMF0_COMMAND:
            return handleAMF0Command(data);
            
        case RTMP_MSG_AMF3_COMMAND:
            return handleAMF3Command(data);
            
        default:
            // 忽略其他类型的消息
            RTMP_LOG_DEBUG(*this, "接收到消息类型: " + std::to_string(static_cast<int>(header.message_type)) + 
                          ", 长度: " + std::to_string(header.message_length));
            return true;
    }
}

bool RTMPClient::handleChunkSize(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    
    uint32_t new_chunk_size = readUint32BE(data.data());
    
    // 验证chunk大小的合理性
    if (new_chunk_size < 1 || new_chunk_size > 0xFFFFFF) {
        std::cerr << "Invalid chunk size received: " << new_chunk_size << std::endl;
        return false;
    }
    
    uint32_t old_chunk_size = chunk_size_;
    chunk_size_ = new_chunk_size;
    
    RTMP_LOG_INFO(*this, "服务器更改块大小从 " + std::to_string(old_chunk_size) + 
                  " 到 " + std::to_string(chunk_size_) + " 字节");
    
    // 如果需要，可以向服务器发送确认
    return sendChunkSizeAck();
}

bool RTMPClient::sendChunkSizeAck() {
    // 向服务器发送我们的chunk大小设置
    std::vector<uint8_t> data(4);
    writeUint32BE(data, chunk_size_);
    
    return sendRTMPMessage(RTMP_MSG_CHUNK_SIZE, 0, data);
}

bool RTMPClient::handleAcknowledgement(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    
    uint32_t bytes_received = readUint32BE(data.data());
    RTMP_LOG_DEBUG(*this, "服务器确认: " + std::to_string(bytes_received) + " 字节");
    return true;
}

bool RTMPClient::handleWindowAckSize(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    
    window_ack_size_ = readUint32BE(data.data());
    RTMP_LOG_INFO(*this, "服务器设置窗口确认大小为: " + std::to_string(window_ack_size_));
    return true;
}

bool RTMPClient::handleSetPeerBandwidth(const std::vector<uint8_t>& data) {
    if (data.size() < 5) {
        return false;
    }
    
    uint32_t bandwidth = readUint32BE(data.data());
    uint8_t limit_type = data[4];
    
    RTMP_LOG_DEBUG(*this, "服务器设置对等带宽: " + std::to_string(bandwidth) + 
                   ", 限制类型: " + std::to_string(static_cast<int>(limit_type)));
    return true;
}

bool RTMPClient::handleUserControl(const std::vector<uint8_t>& data) {
    if (data.size() < 2) {
        return false;
    }
    
    uint16_t event_type = readUint16BE(data.data());
    RTMP_LOG_DEBUG(*this, "用户控制事件: " + std::to_string(event_type));
    
    switch (event_type) {
        case 0: // Stream Begin
            if (data.size() >= 6) {
                uint32_t stream_id = readUint32BE(data.data() + 2);
                RTMP_LOG_DEBUG(*this, "流开始: " + std::to_string(stream_id));
            }
            break;
        case 1: // Stream EOF
            if (data.size() >= 6) {
                uint32_t stream_id = readUint32BE(data.data() + 2);
                RTMP_LOG_DEBUG(*this, "流结束: " + std::to_string(stream_id));
            }
            break;
        case 2: // Stream Dry
            if (data.size() >= 6) {
                uint32_t stream_id = readUint32BE(data.data() + 2);
                RTMP_LOG_DEBUG(*this, "流干涸: " + std::to_string(stream_id));
            }
            break;
        default:
            break;
    }
    
    return true;
}

bool RTMPClient::handleAMF0Command(const std::vector<uint8_t>& data) {
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();
    
    // 解析命令名
    AMFValue command = decodeAMF0Value(ptr, remaining);
    if (command.type != AMF0_STRING) {
        return false;
    }
    
    RTMP_LOG_DEBUG(*this, "接收到AMF0命令: " + command.string_value);
    
    // 解析事务ID
    AMFValue transaction_id = decodeAMF0Value(ptr, remaining);
    if (transaction_id.type != AMF0_NUMBER) {
        return false;
    }
    
    // 处理不同的命令响应
    if (command.string_value == "_result") {
        return handleCommandResult(transaction_id.number, ptr, remaining);
    } else if (command.string_value == "_error") {
        return handleCommandError(transaction_id.number, ptr, remaining);
    } else if (command.string_value == "onStatus") {
        return handleOnStatus(ptr, remaining);
    }
    
    return true;
}

bool RTMPClient::handleAMF3Command(const std::vector<uint8_t>& data) {
    // AMF3命令处理（类似AMF0，但使用AMF3解码）
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();
    
    // 跳过AMF3标记
    if (remaining > 0 && *ptr == 0x00) {
        ptr++;
        remaining--;
    }
    
    // 解析命令名
    AMFValue command = decodeAMF3Value(ptr, remaining);
    if (command.type != AMF3_STRING) {
        return false;
    }
    
    RTMP_LOG_DEBUG(*this, "接收到AMF3命令: " + command.string_value);
    return true;
}

bool RTMPClient::handleCommandResult(double transaction_id, const uint8_t* data, size_t remaining) {
    RTMP_LOG_DEBUG(*this, "事务命令结果 " + std::to_string(transaction_id));
    
    if (transaction_id == 1.0) {
        // connect命令的响应
        RTMP_LOG_INFO(*this, "连接命令成功");
    } else if (transaction_id == 2.0) {
        // createStream命令的响应
        if (remaining > 0) {
            AMFValue stream_id = decodeAMF0Value(data, remaining);
            if (stream_id.type == AMF0_NUMBER) {
                RTMP_LOG_INFO(*this, "创建流ID: " + std::to_string(stream_id.number));
            }
        }
    }
    
    return true;
}

bool RTMPClient::handleCommandError(double transaction_id, const uint8_t* data, size_t remaining) {
    RTMP_LOG_ERROR(*this, "事务命令错误 " + std::to_string(transaction_id));
    
    // 解析错误信息
    if (remaining > 0) {
        AMFValue error_info = decodeAMF0Value(data, remaining);
        // 处理错误信息...
    }
    
    return false;
}

bool RTMPClient::handleOnStatus(const uint8_t* data, size_t remaining) {
    if (remaining > 0) {
        AMFValue status_obj = decodeAMF0Value(data, remaining);
        if (status_obj.type == AMF0_OBJECT) {
            auto it = status_obj.object_value.find("code");
            if (it != status_obj.object_value.end() && it->second.type == AMF0_STRING) {
                RTMP_LOG_DEBUG(*this, "状态码: " + it->second.string_value);
                
                if (it->second.string_value == "NetStream.Publish.Start") {
                    RTMP_LOG_INFO(*this, "发布开始成功");
                    return true;
                } else if (it->second.string_value.find("Error") != std::string::npos) {
                    std::cerr << "Publish error: " << it->second.string_value << std::endl;
                    return false;
                }
            }
        }
    }
    
    return true;
}

void RTMPClient::setStreamKey(const std::string& stream_key) {
    stream_key_ = stream_key;
}

void RTMPClient::setChunkSize(uint32_t chunk_size) {
    chunk_size_ = chunk_size;
}

// 工具方法实现
void RTMPClient::writeUint32BE(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back((value >> 24) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
}

void RTMPClient::writeUint24BE(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
}

void RTMPClient::writeUint16BE(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
}

uint32_t RTMPClient::readUint32BE(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

uint32_t RTMPClient::readUint24BE(const uint8_t* data) {
    return (data[0] << 16) | (data[1] << 8) | data[2];
}

uint16_t RTMPClient::readUint16BE(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

// AMF0编码实现
void RTMPClient::encodeAMF0Value(std::vector<uint8_t>& buffer, const AMFValue& value) {
    switch (value.type) {
        case AMF0_NUMBER:
            encodeAMF0Number(buffer, value.number);
            break;
        case AMF0_BOOLEAN:
            encodeAMF0Boolean(buffer, value.boolean);
            break;
        case AMF0_STRING:
            encodeAMF0String(buffer, value.string_value);
            break;
        case AMF0_OBJECT:
            encodeAMF0Object(buffer, value.object_value);
            break;
        case AMF0_NULL:
            encodeAMF0Null(buffer);
            break;
        case AMF0_STRICT_ARRAY:
            encodeAMF0Array(buffer, value.array_value);
            break;
        case AMF0_ECMA_ARRAY:
            encodeAMF0EcmaArray(buffer, value.object_value);
            break;
        case AMF0_LONG_STRING:
            encodeAMF0LongString(buffer, value.string_value);
            break;
        default:
            encodeAMF0Null(buffer);
            break;
    }
}

void RTMPClient::encodeAMF0String(std::vector<uint8_t>& buffer, const std::string& str) {
    buffer.push_back(AMF0_STRING);
    writeUint16BE(buffer, str.length());
    buffer.insert(buffer.end(), str.begin(), str.end());
}

void RTMPClient::encodeAMF0LongString(std::vector<uint8_t>& buffer, const std::string& str) {
    buffer.push_back(AMF0_LONG_STRING);
    writeUint32BE(buffer, str.length());
    buffer.insert(buffer.end(), str.begin(), str.end());
}

void RTMPClient::encodeAMF0Number(std::vector<uint8_t>& buffer, double number) {
    buffer.push_back(AMF0_NUMBER);
    uint64_t* num_ptr = reinterpret_cast<uint64_t*>(&number);
    uint64_t num = *num_ptr;
    
    // 转换为网络字节序
    for (int i = 7; i >= 0; i--) {
        buffer.push_back((num >> (i * 8)) & 0xFF);
    }
}

void RTMPClient::encodeAMF0Boolean(std::vector<uint8_t>& buffer, bool value) {
    buffer.push_back(AMF0_BOOLEAN);
    buffer.push_back(value ? 0x01 : 0x00);
}

void RTMPClient::encodeAMF0Null(std::vector<uint8_t>& buffer) {
    buffer.push_back(AMF0_NULL);
}

void RTMPClient::encodeAMF0Object(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj) {
    buffer.push_back(AMF0_OBJECT);
    
    for (const auto& pair : obj) {
        // 属性名（不带类型标记）
        writeUint16BE(buffer, pair.first.length());
        buffer.insert(buffer.end(), pair.first.begin(), pair.first.end());
        
        // 属性值
        encodeAMF0Value(buffer, pair.second);
    }
    
    // 对象结束标记
    buffer.push_back(0x00);
    buffer.push_back(0x00);
    buffer.push_back(AMF0_OBJECT_END);
}

void RTMPClient::encodeAMF0Array(std::vector<uint8_t>& buffer, const std::vector<AMFValue>& arr) {
    buffer.push_back(AMF0_STRICT_ARRAY);
    writeUint32BE(buffer, arr.size());
    
    for (const auto& value : arr) {
        encodeAMF0Value(buffer, value);
    }
}

void RTMPClient::encodeAMF0EcmaArray(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj) {
    buffer.push_back(AMF0_ECMA_ARRAY);
    writeUint32BE(buffer, obj.size());
    
    for (const auto& pair : obj) {
        // 属性名
        writeUint16BE(buffer, pair.first.length());
        buffer.insert(buffer.end(), pair.first.begin(), pair.first.end());
        
        // 属性值
        encodeAMF0Value(buffer, pair.second);
    }
    
    // 数组结束标记
    buffer.push_back(0x00);
    buffer.push_back(0x00);
    buffer.push_back(AMF0_OBJECT_END);
}

// AMF0解码实现
AMFValue RTMPClient::decodeAMF0Value(const uint8_t*& data, size_t& remaining) {
    if (remaining < 1) {
        return AMFValue();
    }
    
    uint8_t type = *data++;
    remaining--;
    
    switch (type) {
        case AMF0_NUMBER:
            return AMFValue(decodeAMF0Number(data, remaining));
        case AMF0_BOOLEAN:
            return AMFValue(decodeAMF0Boolean(data, remaining));
        case AMF0_STRING:
            return AMFValue(decodeAMF0String(data, remaining));
        case AMF0_OBJECT: {
            AMFValue obj;
            obj.type = AMF0_OBJECT;
            obj.object_value = decodeAMF0Object(data, remaining);
            return obj;
        }
        case AMF0_NULL:
            return AMFValue();
        case AMF0_STRICT_ARRAY: {
            AMFValue arr;
            arr.type = AMF0_STRICT_ARRAY;
            arr.array_value = decodeAMF0Array(data, remaining);
            return arr;
        }
        case AMF0_LONG_STRING:
            return AMFValue(decodeAMF0LongString(data, remaining));
        default:
            return AMFValue();
    }
}

std::string RTMPClient::decodeAMF0String(const uint8_t*& data, size_t& remaining) {
    if (remaining < 2) {
        return "";
    }
    
    uint16_t length = readUint16BE(data);
    data += 2;
    remaining -= 2;
    
    if (remaining < length) {
        return "";
    }
    
    std::string result(reinterpret_cast<const char*>(data), length);
    data += length;
    remaining -= length;
    
    return result;
}

std::string RTMPClient::decodeAMF0LongString(const uint8_t*& data, size_t& remaining) {
    if (remaining < 4) {
        return "";
    }
    
    uint32_t length = readUint32BE(data);
    data += 4;
    remaining -= 4;
    
    if (remaining < length) {
        return "";
    }
    
    std::string result(reinterpret_cast<const char*>(data), length);
    data += length;
    remaining -= length;
    
    return result;
}

double RTMPClient::decodeAMF0Number(const uint8_t*& data, size_t& remaining) {
    if (remaining < 8) {
        return 0.0;
    }
    
    uint64_t num = 0;
    for (int i = 0; i < 8; i++) {
        num = (num << 8) | *data++;
    }
    remaining -= 8;
    
    return *reinterpret_cast<double*>(&num);
}

bool RTMPClient::decodeAMF0Boolean(const uint8_t*& data, size_t& remaining) {
    if (remaining < 1) {
        return false;
    }
    
    bool result = (*data++ != 0);
    remaining--;
    
    return result;
}

std::map<std::string, AMFValue> RTMPClient::decodeAMF0Object(const uint8_t*& data, size_t& remaining) {
    std::map<std::string, AMFValue> result;
    
    while (remaining >= 3) {
        // 检查对象结束标记
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == AMF0_OBJECT_END) {
            data += 3;
            remaining -= 3;
            break;
        }
        
        // 读取属性名
        if (remaining < 2) break;
        uint16_t name_length = readUint16BE(data);
        data += 2;
        remaining -= 2;
        
        if (remaining < name_length) break;
        std::string name(reinterpret_cast<const char*>(data), name_length);
        data += name_length;
        remaining -= name_length;
        
        // 读取属性值
        AMFValue value = decodeAMF0Value(data, remaining);
        result[name] = value;
    }
    
    return result;
}

std::vector<AMFValue> RTMPClient::decodeAMF0Array(const uint8_t*& data, size_t& remaining) {
    std::vector<AMFValue> result;
    
    if (remaining < 4) {
        return result;
    }
    
    uint32_t count = readUint32BE(data);
    data += 4;
    remaining -= 4;
    
    for (uint32_t i = 0; i < count && remaining > 0; i++) {
        result.push_back(decodeAMF0Value(data, remaining));
    }
    
    return result;
}

// AMF3编码实现
void RTMPClient::encodeAMF3Value(std::vector<uint8_t>& buffer, const AMFValue& value) {
    switch (value.type) {
        case AMF3_NULL:
            buffer.push_back(AMF3_NULL);
            break;
        case AMF3_FALSE:
            buffer.push_back(AMF3_FALSE);
            break;
        case AMF3_TRUE:
            buffer.push_back(AMF3_TRUE);
            break;
        case AMF3_INTEGER:
            encodeAMF3Integer(buffer, value.integer);
            break;
        case AMF3_DOUBLE:
            encodeAMF3Double(buffer, value.number);
            break;
        case AMF3_STRING:
            encodeAMF3String(buffer, value.string_value);
            break;
        case AMF3_ARRAY:
            encodeAMF3Array(buffer, value.array_value);
            break;
        case AMF3_OBJECT:
            encodeAMF3Object(buffer, value.object_value);
            break;
        case AMF3_BYTE_ARRAY:
            encodeAMF3ByteArray(buffer, value.byte_array);
            break;
        default:
            buffer.push_back(AMF3_NULL);
            break;
    }
}

void RTMPClient::encodeAMF3Integer(std::vector<uint8_t>& buffer, int32_t value) {
    buffer.push_back(AMF3_INTEGER);
    writeAMF3U29(buffer, static_cast<uint32_t>(value));
}

void RTMPClient::encodeAMF3Double(std::vector<uint8_t>& buffer, double value) {
    buffer.push_back(AMF3_DOUBLE);
    uint64_t* num_ptr = reinterpret_cast<uint64_t*>(&value);
    uint64_t num = *num_ptr;
    
    for (int i = 7; i >= 0; i--) {
        buffer.push_back((num >> (i * 8)) & 0xFF);
    }
}

void RTMPClient::encodeAMF3String(std::vector<uint8_t>& buffer, const std::string& str) {
    buffer.push_back(AMF3_STRING);
    
    // 检查字符串引用
    int ref = getAMF3StringReference(str);
    if (ref >= 0) {
        writeAMF3U29(buffer, (ref << 1) | 0);
        return;
    }
    
    // 新字符串
    writeAMF3U29(buffer, (str.length() << 1) | 1);
    buffer.insert(buffer.end(), str.begin(), str.end());
    
    // 添加到引用表
    if (!str.empty()) {
        amf3_string_table_.push_back(str);
    }
}

void RTMPClient::encodeAMF3Array(std::vector<uint8_t>& buffer, const std::vector<AMFValue>& arr) {
    buffer.push_back(AMF3_ARRAY);
    
    // 检查数组引用
    for (size_t i = 0; i < amf3_object_table_.size(); i++) {
        if (amf3_object_table_[i].type == AMF3_ARRAY && 
            amf3_object_table_[i].array_value.size() == arr.size()) {
            // 简单比较，实际应该深度比较
            bool same = true;
            for (size_t j = 0; j < arr.size(); j++) {
                if (amf3_object_table_[i].array_value[j].type != arr[j].type) {
                    same = false;
                    break;
                }
            }
            if (same) {
                writeAMF3U29(buffer, (i << 1) | 0);
                return;
            }
        }
    }
    
    // 新数组
    writeAMF3U29(buffer, (arr.size() << 1) | 1);
    
    // 添加到引用表
    AMFValue array_ref;
    array_ref.type = AMF3_ARRAY;
    array_ref.array_value = arr;
    amf3_object_table_.push_back(array_ref);
    
    // 关联数组部分（空）
    writeAMF3U29(buffer, 1); // 空字符串引用
    
    // 密集数组部分
    for (const auto& value : arr) {
        encodeAMF3Value(buffer, value);
    }
}

void RTMPClient::encodeAMF3Object(std::vector<uint8_t>& buffer, const std::map<std::string, AMFValue>& obj) {
    buffer.push_back(AMF3_OBJECT);
    
    // 检查对象引用
    for (size_t i = 0; i < amf3_object_table_.size(); i++) {
        if (amf3_object_table_[i].type == AMF3_OBJECT && 
            amf3_object_table_[i].object_value.size() == obj.size()) {
            // 简单比较对象键
            bool same = true;
            for (const auto& pair : obj) {
                if (amf3_object_table_[i].object_value.find(pair.first) == 
                    amf3_object_table_[i].object_value.end()) {
                    same = false;
                    break;
                }
            }
            if (same) {
                writeAMF3U29(buffer, (i << 1) | 0);
                return;
            }
        }
    }
    
    // 新对象 - 特征引用处理
    std::vector<std::string> trait_keys;
    for (const auto& pair : obj) {
        trait_keys.push_back(pair.first);
    }
    
    // 检查特征引用
    int trait_ref = -1;
    for (size_t i = 0; i < amf3_trait_table_.size(); i++) {
        if (amf3_trait_table_[i].size() == trait_keys.size()) {
            bool same = true;
            for (size_t j = 0; j < trait_keys.size(); j++) {
                if (j >= amf3_trait_table_[i].size() || 
                    amf3_trait_table_[i][j].string_value != trait_keys[j]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                trait_ref = i;
                break;
            }
        }
    }
    
    if (trait_ref >= 0) {
        // 使用现有特征
        writeAMF3U29(buffer, (trait_ref << 2) | 1);
    } else {
        // 新特征
        writeAMF3U29(buffer, (trait_keys.size() << 4) | 3);
        
        // 类名（空）
        writeAMF3U29(buffer, 1); // 空字符串
        
        // 属性名
        std::vector<AMFValue> trait_values;
        for (const auto& key : trait_keys) {
            encodeAMF3String(buffer, key);
            AMFValue key_val(key);
            key_val.type = AMF3_STRING;
            trait_values.push_back(key_val);
        }
        
        // 添加到特征表
        amf3_trait_table_.push_back(trait_values);
    }
    
    // 添加对象到引用表
    AMFValue obj_ref;
    obj_ref.type = AMF3_OBJECT;
    obj_ref.object_value = obj;
    amf3_object_table_.push_back(obj_ref);
    
    // 属性值
    for (const auto& key : trait_keys) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            encodeAMF3Value(buffer, it->second);
        } else {
            encodeAMF3Value(buffer, AMFValue()); // null
        }
    }
}

void RTMPClient::encodeAMF3ByteArray(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data) {
    buffer.push_back(AMF3_BYTE_ARRAY);
    writeAMF3U29(buffer, (data.size() << 1) | 1);
    buffer.insert(buffer.end(), data.begin(), data.end());
}

// AMF3解码实现
AMFValue RTMPClient::decodeAMF3Value(const uint8_t*& data, size_t& remaining) {
    if (remaining < 1) {
        return AMFValue();
    }
    
    uint8_t type = *data++;
    remaining--;
    
    switch (type) {
        case AMF3_NULL:
        case AMF3_UNDEFINED: {
            AMFValue val;
            val.type = static_cast<AMFType>(type);
            return val;
        }
        case AMF3_FALSE: {
            AMFValue val(false);
            val.type = AMF3_FALSE;
            return val;
        }
        case AMF3_TRUE: {
            AMFValue val(true);
            val.type = AMF3_TRUE;
            return val;
        }
        case AMF3_INTEGER:
            return AMFValue(decodeAMF3Integer(data, remaining));
        case AMF3_DOUBLE:
            return AMFValue(decodeAMF3Double(data, remaining));
        case AMF3_STRING: {
            AMFValue val(decodeAMF3String(data, remaining));
            val.type = AMF3_STRING;
            return val;
        }
        case AMF3_ARRAY: {
            AMFValue val;
            val.type = AMF3_ARRAY;
            val.array_value = decodeAMF3Array(data, remaining);
            return val;
        }
        case AMF3_OBJECT: {
            AMFValue val;
            val.type = AMF3_OBJECT;
            val.object_value = decodeAMF3Object(data, remaining);
            return val;
        }
        case AMF3_BYTE_ARRAY: {
            AMFValue val;
            val.type = AMF3_BYTE_ARRAY;
            val.byte_array = decodeAMF3ByteArray(data, remaining);
            return val;
        }
        default:
            return AMFValue();
    }
}

int32_t RTMPClient::decodeAMF3Integer(const uint8_t*& data, size_t& remaining) {
    uint32_t value = readAMF3U29(data, remaining);
    
    // 处理符号扩展
    if (value & 0x10000000) {
        return static_cast<int32_t>(value | 0xE0000000);
    }
    
    return static_cast<int32_t>(value);
}

double RTMPClient::decodeAMF3Double(const uint8_t*& data, size_t& remaining) {
    if (remaining < 8) {
        return 0.0;
    }
    
    uint64_t num = 0;
    for (int i = 0; i < 8; i++) {
        num = (num << 8) | *data++;
    }
    remaining -= 8;
    
    return *reinterpret_cast<double*>(&num);
}

std::string RTMPClient::decodeAMF3String(const uint8_t*& data, size_t& remaining) {
    uint32_t ref = readAMF3U29(data, remaining);
    
    // 检查是否为引用
    if ((ref & 1) == 0) {
        uint32_t index = ref >> 1;
        if (index < amf3_string_table_.size()) {
            return amf3_string_table_[index];
        }
        return "";
    }
    
    // 新字符串
    uint32_t length = ref >> 1;
    if (remaining < length) {
        return "";
    }
    
    std::string result(reinterpret_cast<const char*>(data), length);
    data += length;
    remaining -= length;
    
    // 添加到引用表
    if (!result.empty()) {
        amf3_string_table_.push_back(result);
    }
    
    return result;
}

std::vector<AMFValue> RTMPClient::decodeAMF3Array(const uint8_t*& data, size_t& remaining) {
    std::vector<AMFValue> result;
    
    uint32_t ref = readAMF3U29(data, remaining);
    
    // 检查是否为引用
    if ((ref & 1) == 0) {
        // 处理引用（简化实现）
        return result;
    }
    
    uint32_t count = ref >> 1;
    
    // 跳过关联数组部分
    while (remaining > 0) {
        std::string key = decodeAMF3String(data, remaining);
        if (key.empty()) break;
        decodeAMF3Value(data, remaining); // 跳过值
    }
    
    // 读取密集数组
    for (uint32_t i = 0; i < count && remaining > 0; i++) {
        result.push_back(decodeAMF3Value(data, remaining));
    }
    
    return result;
}

std::map<std::string, AMFValue> RTMPClient::decodeAMF3Object(const uint8_t*& data, size_t& remaining) {
    std::map<std::string, AMFValue> result;
    
    uint32_t ref = readAMF3U29(data, remaining);
    
    // 检查是否为引用
    if ((ref & 1) == 0) {
        // 处理引用（简化实现）
        return result;
    }
    
    // 跳过类信息
    std::string class_name = decodeAMF3String(data, remaining);
    
    // 读取属性
    while (remaining > 0) {
        std::string key = decodeAMF3String(data, remaining);
        if (key.empty()) break;
        
        AMFValue value = decodeAMF3Value(data, remaining);
        result[key] = value;
    }
    
    return result;
}

std::vector<uint8_t> RTMPClient::decodeAMF3ByteArray(const uint8_t*& data, size_t& remaining) {
    std::vector<uint8_t> result;
    
    uint32_t ref = readAMF3U29(data, remaining);
    
    // 检查是否为引用
    if ((ref & 1) == 0) {
        // 处理引用（简化实现）
        return result;
    }
    
    uint32_t length = ref >> 1;
    if (remaining < length) {
        return result;
    }
    
    result.assign(data, data + length);
    data += length;
    remaining -= length;
    
    return result;
}

// AMF3辅助方法
void RTMPClient::writeAMF3U29(std::vector<uint8_t>& buffer, uint32_t value) {
    if (value < 0x80) {
        buffer.push_back(value & 0x7F);
    } else if (value < 0x4000) {
        buffer.push_back(((value >> 7) & 0x7F) | 0x80);
        buffer.push_back(value & 0x7F);
    } else if (value < 0x200000) {
        buffer.push_back(((value >> 14) & 0x7F) | 0x80);
        buffer.push_back(((value >> 7) & 0x7F) | 0x80);
        buffer.push_back(value & 0x7F);
    } else {
        buffer.push_back(((value >> 22) & 0x7F) | 0x80);
        buffer.push_back(((value >> 15) & 0x7F) | 0x80);
        buffer.push_back(((value >> 8) & 0x7F) | 0x80);
        buffer.push_back(value & 0xFF);
    }
}

uint32_t RTMPClient::readAMF3U29(const uint8_t*& data, size_t& remaining) {
    uint32_t result = 0;
    int bytes = 0;
    
    while (bytes < 4 && remaining > 0) {
        uint8_t byte = *data++;
        remaining--;
        bytes++;
        
        if (bytes < 4) {
            result = (result << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0) {
                break;
            }
        } else {
            result = (result << 8) | byte;
        }
    }
    
    return result;
}

int RTMPClient::getAMF3StringReference(const std::string& str) {
    for (size_t i = 0; i < amf3_string_table_.size(); i++) {
        if (amf3_string_table_[i] == str) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void RTMPClient::clearAMF3References() {
    amf3_string_table_.clear();
    amf3_object_table_.clear();
    amf3_trait_table_.clear();
}
