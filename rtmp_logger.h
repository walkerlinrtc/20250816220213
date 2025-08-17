#ifndef RTMP_LOGGER_H
#define RTMP_LOGGER_H

#include <spdlog/spdlog.h>

// 前向声明
class RTMPClient;

// 日志宏定义，自动包含文件名和行号
#define RTMP_LOG_INFO(client, message) \
    (client).logInternal(spdlog::level::info, __FILE__, __LINE__, message)

#define RTMP_LOG_ERROR(client, message) \
    (client).logInternal(spdlog::level::err, __FILE__, __LINE__, message)

#define RTMP_LOG_DEBUG(client, message) \
    (client).logInternal(spdlog::level::debug, __FILE__, __LINE__, message)

#define RTMP_LOG_WARN(client, message) \
    (client).logInternal(spdlog::level::warn, __FILE__, __LINE__, message)

#define RTMP_LOG_INFO_F(client, format, ...) \
    (client).logInternalF(spdlog::level::info, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define RTMP_LOG_ERROR_F(client, format, ...) \
    (client).logInternalF(spdlog::level::err, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define RTMP_LOG_DEBUG_F(client, format, ...) \
    (client).logInternalF(spdlog::level::debug, __FILE__, __LINE__, format, ##__VA_ARGS__)

#endif // RTMP_LOGGER_H