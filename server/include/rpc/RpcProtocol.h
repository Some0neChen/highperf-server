#pragma once
#include <cstddef>
#include <cstring>
#include <netinet/in.h>
#include <cstdint>

// 为了方便二进制流传输，强制一字节对齐
#pragma pack(push, 1)
struct RpcHeader {
    // RPC这里为"CRPC"
    uint32_t magic;          // offset 0
    uint16_t version;        // offset 4
    uint16_t header_length;  // offset 6

    uint32_t request_id;     // offset 8

    uint32_t payload_length; // offset 12
    uint32_t status;         // offset 16

    uint16_t service_id;     // offset 20
    uint16_t method_id;      // offset 22
    uint8_t msg_type;        // offset 24
    uint8_t flags;           // offset 25
    uint8_t codec;           // offset 26
    uint8_t reserve1;        // offset 27
	uint8_t reserve2;        // offset 28
	uint8_t reserve3;        // offset 29
	uint8_t reserve4;        // offset 30
	uint8_t reserve5;        // offset 31
};
#pragma pack(pop) // restore default alignment

namespace RPC_HEADER_OFFSET {
	constexpr size_t MAGIC = 0;
	constexpr size_t VERSION = 4;
	constexpr size_t HEADER_LENGTH = 6;
	constexpr size_t REQUEST_ID = 8;
	constexpr size_t PAYLOAD_LENGTH = 12;
	constexpr size_t STATUS = 16;
	constexpr size_t SERVICE_ID = 20;
	constexpr size_t METHOD_ID = 22;
	constexpr size_t MSG_TYPE = 24;
	constexpr size_t FLAGS = 25;
	constexpr size_t CODEC = 26;
	constexpr size_t RESERVED = 27;
};

namespace RPC_HEADER_SPEC {
	constexpr size_t HEADER_SIZE = 32;			// RPC协议头部固定32字节
	constexpr uint32_t RPC_MAGIC = 0x43525043; 	// "CRPC" in ASCII
}