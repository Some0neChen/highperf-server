#pragma once

#include "RpcProtocol.h"
#include <cstdint>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

inline uint16_t ntohs_and_trans(char* data)
{
	uint32_t res;
	memcpy(&res, data, sizeof(res));
	return res;
}

inline uint32_t ntohl_and_trans(char* data)
{
	uint32_t res;
	memcpy(&res, data, sizeof(res));
	return res;
}

inline bool assert_rpc_header(char* data)
{
	return ntohl_and_trans(data + RPC_HEADER_OFFSET::MAGIC) == RPC_HEADER_SPEC::RPC_MAGIC;
}

inline uint32_t get_payload_len(char* data)
{
	return ntohl_and_trans(data + RPC_HEADER_OFFSET::PAYLOAD_LENGTH);
}

struct TLVData {
	RpcHeader header;
	std::unordered_map<uint8_t, std::vector<char>> payload;
};

namespace RPC_TLV_SPEC {
	constexpr unsigned short TYPE_SIZE = 4;
	constexpr unsigned short LENGTH_SIZE = 4;
	constexpr unsigned short DATA_OFFSET = TYPE_SIZE + LENGTH_SIZE;
}