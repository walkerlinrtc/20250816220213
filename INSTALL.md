# RTMP客户端安装和使用指南

## 系统要求

- **操作系统**: Linux (Ubuntu 18.04+, CentOS 7+, 或其他现代Linux发行版)
- **编译器**: GCC 7.0+ 或 Clang 6.0+ (支持C++11)
- **CMake**: 3.10或更高版本
- **依赖库**: pthread (通常系统自带)

## 安装步骤

### 1. 安装系统依赖

#### Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential cmake git pkg-config
```

#### CentOS/RHEL:
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake3 git pkgconfig
# 或者在较新版本中
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake git pkgconfig
```

### 2. 克隆或下载源码

```bash
# 如果从git仓库克隆
git clone <repository_url>
cd rtmp_client

# 或者直接使用现有源码目录
cd /path/to/rtmp_client
```

### 3. 编译项目

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make -j$(nproc)

# 可选：安装到系统
sudo make install
```

### 4. 验证安装

```bash
# 检查可执行文件
ls -la bin/rtmp_client

# 查看帮助信息
./bin/rtmp_client
```

## 使用方法

### 基本用法

```bash
./bin/rtmp_client <rtmp_url> <flv_file>
```

### 参数说明

- `rtmp_url`: RTMP服务器地址，格式为 `rtmp://host:port/app/stream_key`
- `flv_file`: 本地FLV文件路径

### 使用示例

```bash
# 推送到本地SRS服务器
./bin/rtmp_client rtmp://localhost:1935/live/stream test.flv

# 推送到远程服务器
./bin/rtmp_client rtmp://your-server.com:1935/live/mystream video.flv

# 推送到带认证的服务器
./bin/rtmp_client rtmp://user:pass@server.com:1935/live/stream content.flv
```

## 日志系统

### 日志文件位置
- 控制台输出：实时显示INFO级别及以上的日志
- 文件日志：`logs/rtmp_client.log`（自动轮转，最大5MB，保留3个文件）

### 日志级别
- **DEBUG**: 详细的调试信息
- **INFO**: 一般信息
- **WARN**: 警告信息
- **ERROR**: 错误信息

### 自定义日志级别
可以通过修改代码中的`setLogLevel()`调用来改变日志级别：

```cpp
client.setLogLevel("debug");  // 显示所有日志
client.setLogLevel("info");   // 默认级别
client.setLogLevel("warn");   // 只显示警告和错误
client.setLogLevel("error");  // 只显示错误
```

## 配置选项

### RTMPConfig结构体参数

```cpp
struct RTMPConfig {
    uint32_t connect_timeout_ms = 5000;      // 连接超时(毫秒)
    uint32_t read_timeout_ms = 3000;         // 读取超时(毫秒)
    uint32_t write_timeout_ms = 3000;        // 写入超时(毫秒)
    uint32_t max_retry_count = 3;            // 最大重试次数
    uint32_t retry_interval_ms = 1000;       // 重试间隔(毫秒)
    bool enable_heartbeat = true;            // 启用心跳
    uint32_t heartbeat_interval_ms = 30000;  // 心跳间隔(毫秒)
    bool enable_statistics = true;           // 启用统计
    uint32_t max_queue_size = 1000;          // 最大队列大小
};
```

## 故障排除

### 常见问题

#### 1. 编译错误
```
error: 'spdlog' not found
```
**解决方案**: CMake会自动下载spdlog，确保网络连接正常。如果仍有问题，可以手动安装：
```bash
# Ubuntu
sudo apt install libspdlog-dev

# CentOS (需要EPEL)
sudo yum install epel-release
sudo yum install spdlog-devel
```

#### 2. 连接失败
```
ERROR: Failed to connect to RTMP server
```
**检查项目**:
- 确认RTMP服务器地址和端口正确
- 检查网络连接
- 确认防火墙设置
- 验证RTMP服务器是否运行

#### 3. FLV文件错误
```
ERROR: Invalid FLV file header
```
**解决方案**:
- 确认文件是有效的FLV格式
- 检查文件是否损坏
- 验证文件路径正确

#### 4. 权限问题
```
ERROR: Failed to create logs directory
```
**解决方案**:
```bash
# 创建日志目录并设置权限
mkdir -p logs
chmod 755 logs
```

### 调试模式

启用详细日志进行调试：

```cpp
// 在代码中设置
client.setLogLevel("debug");

// 或者编译时启用调试
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### 性能优化

#### 1. 网络优化
- 调整chunk大小以适应网络带宽
- 启用心跳保活机制
- 设置合适的超时值

#### 2. 系统优化
```bash
# 增加文件描述符限制
ulimit -n 65536

# 优化网络缓冲区
echo 'net.core.rmem_max = 16777216' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 16777216' >> /etc/sysctl.conf
sysctl -p
```

## SRS服务器配置

### 基本配置示例

```nginx
# srs.conf
listen              1935;
max_connections     1000;
srs_log_tank        file;
srs_log_file        ./objs/srs.log;

vhost __defaultVhost__ {
    rtmp {
        enabled     on;
        drop_for_pt off;
    }
    
    http_remux {
        enabled     on;
        mount       [vhost]/[app]/[stream].flv;
    }
    
    hls {
        enabled         on;
        hls_fragment    10;
        hls_window      60;
        hls_path        ./objs/nginx/html;
        hls_m3u8_file   [app]/[stream].m3u8;
        hls_ts_file     [app]/[stream]-[seq].ts;
    }
}
```

### 启动SRS服务器

```bash
# 下载和编译SRS
git clone https://github.com/ossrs/srs.git
cd srs/trunk
./configure
make

# 启动服务器
./objs/srs -c conf/srs.conf
```

## 高级用法

### 编程接口使用

参考`example_usage.cpp`和`spdlog_example.cpp`文件了解如何在代码中使用RTMP客户端。

### 批量推流

```bash
#!/bin/bash
# batch_push.sh
for file in *.flv; do
    echo "Pushing $file..."
    ./bin/rtmp_client rtmp://localhost:1935/live/stream "$file"
    sleep 5
done
```

### 监控和统计

客户端提供详细的统计信息，包括：
- 发送/接收字节数
- 音视频帧计数
- 比特率统计
- 连接状态监控

## 技术支持

如果遇到问题，请：

1. 检查日志文件 `logs/rtmp_client.log`
2. 确认系统要求和依赖
3. 参考故障排除部分
4. 查看示例代码和配置

## 许可证

本项目采用MIT许可证，详见LICENSE文件。