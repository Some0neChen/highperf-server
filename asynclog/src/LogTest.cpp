#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
#include <vector>
#include "LogPub.h"
#include "Logger.h"

constexpr int THREAD_NUM   = 8;      // 并发线程数
constexpr int LOG_PER_THREAD = 10000; // 每个线程写多少条

std::atomic<int> total_logged{0};
std::atomic<int> total_dropped{0};

void log_worker(int thread_id) {
    for (int i = 0; i < LOG_PER_THREAD; ++i) {
        auto ret = LOG_INFO("thread=%d seq=%d payload=benchmark_test", thread_id, i);
        if (ret == RET_FLAG::OK) {
            ++total_logged;
        } else {
            ++total_dropped;
        }
    }
}

int main() {
    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(log_worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    int total = THREAD_NUM * LOG_PER_THREAD;
    std::cout << "总条数:   " << total       << "\n";
    std::cout << "成功写入: " << total_logged << "\n";
    std::cout << "丢弃:     " << total_dropped << "\n";
    std::cout << "耗时:     " << elapsed_ms   << " ms\n";
    std::cout << "吞吐量:   "
              << (total_logged * 1000 / elapsed_ms)
              << " 条/秒\n";

    // 等 Flusher 把剩余数据落盘
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}