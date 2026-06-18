#pragma once
#include "Buffer.h"
#include "RpcPub.h"
#include <memory>

std::shared_ptr<TLVData> parse_rpc_buffer(std::shared_ptr<Buffer<char>>);
void hton_rpc_header(RpcHeader& header);

class RpcParser {
    
};