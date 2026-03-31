/*
一个简易的TCP客户端连接
*/

#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

atomic<bool> client_running;

void signal_handler(int sig) {
    client_running.store(false);
}

// 消息接收监听线程
void* receive_messages(void* arg) {
    int* socket_fd = static_cast<int*>(arg);
    char buf[1024];
    int rcv_len;
    while (client_running.load()) {
        rcv_len = read(*socket_fd, buf, 1023); // 留一个字节放'\0'
        if (rcv_len == -1) {
            if (errno == EINTR) {
                // 这只是被调试器或信号打断了，不是真错误，继续下一次循环即可
                continue; 
            }
            cerr << "Socket read error" << endl;
            break;
        } 
        if (rcv_len == 0) {
            cout << "Server closed the connection." << endl;
            break;
        }
        buf[rcv_len] = '\0';
        if (strcmp(buf, "exit\n") == 0) {
            cout << "Server closed the connection." << endl;
            break;
        }
        cout << buf << endl;
    }
    return nullptr;
}

// 消息发送监听线程
void* send_messages(void* arg) {
    int* socket_fd = static_cast<int*>(arg);
    char buf[1024];
    int send_len;
    while (client_running.load()) {
        send_len = read(STDIN_FILENO, buf, 1023);
        if (send_len == -1) {
            if (errno == EINTR) {
                // 这只是被调试器或信号打断了，不是真错误，继续下一次循环即可
                continue; 
            }
            cerr << "Socket read error" << endl;
            break;
        }
        buf[send_len] = '\0';
        write(*socket_fd, buf, send_len);
        if (strcmp(buf, "exit\n") == 0) {
            client_running.store(false);
            break;
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    // 程序启动方式为 程序名 <服务器IP地址> <服务器端口号>
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <server_ip> <server_port>" << endl;
        exit(EXIT_FAILURE);
    }
    client_running.store(true);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    cout << "Welcome using WebClient！" << endl;

    // 注册socket并连接到程序启动时输入的目的IP和端口号
    auto socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        cerr << "Socket creation failed" << endl;
        exit(EXIT_FAILURE);
    }
    sockaddr_in server_addr;
    unsigned short connect_port = static_cast<unsigned short>(stoi(argv[2]));
    server_addr.sin_port = htons(connect_port);
    server_addr.sin_family = AF_INET;
    auto ret = inet_pton(AF_INET, argv[1], &server_addr.sin_addr.s_addr);
    if (ret <= 0) {
        cerr << "Invalid server IP address" << endl;
        exit(EXIT_FAILURE);
    }
    ret = connect(socket_fd, static_cast<sockaddr*>(static_cast<void*>(&server_addr)), sizeof(sockaddr_in));
    if (ret == -1) {
        cerr << "Socket connection failed" << endl;
        exit(EXIT_FAILURE);
    }

    // 双工队列，一个负责实时发送，一个负责实时接收
    pthread_t recv_thread, send_thread;
    pthread_create(&recv_thread, nullptr, receive_messages, static_cast<void*>(&socket_fd));
    pthread_create(&send_thread, nullptr, send_messages, static_cast<void*>(&socket_fd));
    
    pthread_join(recv_thread, nullptr);
    pthread_join(send_thread, nullptr);
    
    close(socket_fd);
}