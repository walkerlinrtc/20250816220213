# RTMP客户端

这是一个用C++实现的RTMP客户端，专门用于将本地FLV文件推送到流媒体服务器（如SRS）。

## 功能特性

- 支持RTMP协议推流
- 读取本地FLV文件并推送
- 仅支持Linux平台
- 头文件和源文件分离的模块化设计
- 支持音频、视频和脚本数据推送

## 编译要求

- Linux操作系统
- GCC编译器（支持C++11）
- CMake 3.10或更高版本

## 编译方法

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译
make

# 或者使用cmake构建
cmake --build .
```

## 使用方法

```bash
./rtmp_client <rtmp_url> <flv_file>
```

### 参数说明

- `rtmp_url`: RTMP服务器地址，格式为 `rtmp://host:port/app/stream_key`
- `flv_file`: 本地FLV文件路径

### 使用示例

```bash
# 推送到本地SRS服务器
./rtmp_client rtmp://localhost:1935/live/stream test.flv

# 推送到远程服务器
./rtmp_client rtmp://your-server.com:1935/live/mystream video.flv
```

## SRS服务器配置

确保您的SRS服务器配置允许RTMP推流。基本配置示例：

```nginx
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

## 项目结构

```
├── rtmp_client.h    # RTMP客户端头文件
├── rtmp_client.cpp  # RTMP客户端实现
├── main.cpp         # 主程序入口
├── Makefile         # 编译配置
└── README.md        # 说明文档
```

## 技术实现

### RTMP协议支持
- RTMP握手协议
- 连接建立和流创建
- 发布流命令
- 音视频数据传输
- 完整的AMF0/AMF3编码解码
- 动态Chunk大小支持
- 服务器响应解析和处理

### FLV文件处理
- FLV文件头解析
- FLV标签读取和解析
- 时间戳处理
- 数据类型识别（音频/视频/脚本）

### 网络通信
- TCP Socket连接
- 数据发送和接收
- 连接状态管理
- 自动重连机制
- 超时处理

### 高级功能
- **连接状态管理**：实时监控连接状态
- **自动重连**：支持连接失败后自动重试
- **心跳保活**：定期发送心跳包保持连接
- **性能统计**：实时统计传输数据和性能指标
- **错误处理**：完善的错误检测和恢复机制
- **日志系统**：详细的运行日志记录
- **配置管理**：灵活的参数配置系统

## 注意事项

1. 确保FLV文件格式正确
2. 检查网络连接和防火墙设置
3. 验证RTMP服务器地址和端口
4. 确保有足够的网络带宽

## 编译和运行

```bash
# 创建构建目录并编译
mkdir build && cd build
cmake ..
make

# 运行（在build/bin目录下）
./bin/rtmp_client rtmp://localhost:1935/live/stream your_video.flv

# 安装到系统（可选）
sudo make install

# 清理构建文件
cd .. && rm -rf build
```

## 故障排除

如果遇到连接问题：
1. 检查RTMP服务器是否运行
2. 验证网络连接
3. 确认防火墙设置
4. 检查FLV文件是否存在且可读

如果推流失败：
1. 检查FLV文件格式
2. 验证服务器配置
3. 查看服务器日志