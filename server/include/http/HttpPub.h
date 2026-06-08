#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <string_view>

void Http_module_init();

// 将报文头中key的字段全部转为小写
void key_to_lower(char*, char*);
// 4字节比较，提升性能
inline bool match_4_bytes(const char* data, const uint32_t& value)
{
    uint32_t data_val;
    memcpy(&data_val, data, 4);
    return data_val == value;
}
// 2字节比较，提升性能
inline bool match_2_bytes(const char* data, const uint16_t& value)
{
    uint16_t data_val;
    memcpy(&data_val, data, 2);
    return data_val == value;
}

namespace HttpConst {
    // 分隔符
    constexpr std::string_view CRLF       = "\r\n";
    constexpr std::string_view HEADER_END = "\r\n\r\n";
    constexpr std::string_view SP         = " ";
    constexpr std::string_view COLON_SP   = ": ";

    // 请求方法
    constexpr std::string_view METHOD_GET  = "GET";
    constexpr std::string_view METHOD_POST = "POST";

    // 版本
    constexpr std::string_view VERSION_11 = "HTTP/1.1";

    // 请求头 key（统一小写，解析时 key 转小写后比较）
    constexpr std::string_view HEADER_CONTENT_LENGTH    = "content-length";
    constexpr std::string_view HEADER_CONTENT_TYPE      = "content-type";
    constexpr std::string_view HEADER_CONNECTION        = "connection";
    constexpr std::string_view HEADER_HOST              = "host";

    // Connection 头的值
    constexpr std::string_view CONN_KEEP_ALIVE = "keep-alive";
    constexpr std::string_view CONN_CLOSE      = "close";

    // 常用路径
    constexpr std::string_view PATH_PING    = "/ping";
    constexpr std::string_view PATH_ECHO    = "/echo";
    constexpr std::string_view PATH_ERR     = "/err";

    // 响应状态
    constexpr std::string_view STATUS_200 = "200 OK";
    constexpr std::string_view STATUS_400 = "400 Bad Request";
    constexpr std::string_view STATUS_404 = "404 Not Found";
    constexpr std::string_view STATUS_500 = "500 Internal Server Error";

    // Content-Type 值
    constexpr std::string_view CONTENT_TYPE_TEXT = "text/plain";
    constexpr std::string_view CONTENT_TYPE_JSON = "application/json";
    constexpr std::string_view CONTENT_TYPE_HTML = "text/html";

    // 响应信息
    constexpr std::string_view MSG_200  = "OK";
    constexpr std::string_view MSG_400  = "Bad Request";
    constexpr std::string_view MSG_404  = "Not Found";
    constexpr std::string_view MSG_500  = "Internal Server Error";
    constexpr std::string_view MSG_PONG = "pong";
}

namespace HttpRespond {
    // 未找到
    constexpr std::string_view NOT_FOUND = "HTTP/1.1 Not Found";
    // Parse Err
    constexpr std::string_view PARSE_FAULT = "GET /err";
    // GET Ping
    constexpr std::string_view GET_PING = "GET /ping";
    // Post Echo
    constexpr std::string_view POST_PING = "POST /echo";
}


namespace HttpTokens {
    constexpr uint16_t kLineEnd     = 0x0A0D;           // "\r\n"
    constexpr uint32_t kHeaderEnd   = 0x0A0D0A0D;       // "\r\n\r\n"
    constexpr uint16_t kHeaderSep   = 0x203A;           // ": "
}

enum class HttpHandleCode {
    OK = 0,
    ERR,
    TCP_CLOSED,
    REGISER_HANDLER_ERR,
    RESPOND_TYPE_REPEATED,
    FILE_NOT_FOUND,
    END
};

struct RequestContent {
    std::string method;
    std::string url;
    std::string version;
    bool keep_alive;
    size_t content_length;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};