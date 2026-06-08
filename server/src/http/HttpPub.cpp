#include "HttpPub.h"
#include "HttpParser.h"
#include "HttpRouter.h"
#include <cstring>

void Http_module_init()
{
    init_http_parse_fsm();
    init_http_response_constructor();
}

// 将报文头中key的字段全部转为小写
void key_to_lower(char* begin, char* end)
{
    while (begin != end) {
        if (*begin >= 'A' && *begin <= 'Z') {
            *begin = 'a' + (*begin - 'A');
        }
        ++begin;
    }
}