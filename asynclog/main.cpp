#include <iostream>
#include <unistd.h>
#include "include/LogPub.h"
#include "include/Logger.h"
using namespace std;

int main(int argc, char** argv)
{
    LOG_INFO("server started, port=%d", 8080);
    LOG_INFO("hello %s", "world");
    LOG_ERR("something went wrong, errno=%d", 42);
    for (int i = 0; i < 1024; ++i) {
        LOG_ERR("Logging... %d", i);
        LOG_INFO("Logging... %d", i);
        usleep(20000);
    }
    cout << "Logger Start." << endl;
}