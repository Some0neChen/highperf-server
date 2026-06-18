#include "Buffer.h"
#include "RpcProtocol.h"
#include "RpcPub.h"
#include <cstddef>
#include <memory>
#include <netinet/in.h>
#include <utility>
#include <vector>

/* TODO
* 这里要注意的问题是，有些服务里TCP是长连接，那么如果中间有坏包，要如何把这条TCP管道中的错误信息清掉，这是个问题
*/
std::shared_ptr<TLVData> parse_rpc_buffer(std::shared_ptr<Buffer<char>> buf)
{
    size_t read_size = 0;
    std::shared_ptr<TLVData> data;
    auto read_pos = buf->get_data() + buf->get_read_pos();
    RpcHeader* temp = static_cast<RpcHeader*>(static_cast<void*>(read_pos));
    
    data->header.magic = ntohl(temp->magic);
    data->header.version = ntohs(temp->version);
    data->header.header_length = ntohs(temp->header_length);
    data->header.request_id = ntohl(temp->request_id);
    data->header.payload_length = ntohl(temp->payload_length);
    data->header.status = ntohl(temp->status);
    data->header.service_id = ntohl(temp->method_id);
    data->header.msg_type = ntohs(temp->msg_type);
    data->header.flags = ntohs(temp->flags);
    data->header.codec = ntohs(temp->codec);

    read_pos += RPC_HEADER_SPEC::HEADER_SIZE;
    read_size += RPC_HEADER_SPEC::HEADER_SIZE;

    while (read_pos + RPC_TLV_SPEC::DATA_OFFSET < buf->get_data() + buf->get_write_pos()) {
        auto type = ntohl_and_trans(read_pos);
        read_pos += RPC_TLV_SPEC::TYPE_SIZE;
        read_size += RPC_TLV_SPEC::TYPE_SIZE;
        auto length = ntohl_and_trans(read_pos);
        read_pos += RPC_TLV_SPEC::LENGTH_SIZE;
        read_size += RPC_TLV_SPEC::LENGTH_SIZE;
        

        if (read_pos + length > buf->get_data() + buf->writable_size()) {
            return nullptr;
        }
        auto value = std::vector<char>(read_pos, read_pos + length);
        data->payload.emplace(type, std::move(value));
        read_pos += length;
        read_size += length;
    }
    buf->update_read_pos(read_size);
    return data;
}

void hton_rpc_header(RpcHeader& header)
{
    
}