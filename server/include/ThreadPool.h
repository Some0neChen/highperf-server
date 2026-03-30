#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <queue>
#include <semaphore.h>
#include <thread>
#include <vector>

template<typename T>
class ThreadPool
{
    std::mutex mutex_;
    sem_t sem_;
    bool running_state_;
    std::vector<std::thread> thread_pool_;
    std::queue<T, std::deque<T>> task_queue_;
    size_t thread_num;

    void worker_routine() {
        while (running_state_) {
            T task;
            sem_wait(&sem_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (task_queue_.empty()) {
                    continue;
                }
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
            // 执行任务
            task();
        }
    }
public:
    ThreadPool(size_t num = 8) : running_state_(true), thread_num(num) {
        sem_init(&sem_, PTHREAD_PROCESS_PRIVATE, 0);
        thread_pool_.reserve(thread_num);
        for (int i = 0; i < thread_num; ++i) {
            thread_pool_.emplace_back(std::thread(&ThreadPool::worker_routine, this));
        }
    }

    ~ThreadPool() {
        running_state_ = false;
        for (int i = 0; i < thread_num; ++i) {
            sem_post(&sem_);
        }
        for_each(thread_pool_.begin(), thread_pool_.end(), [](std::thread &t) {
            t.join();
        });
        sem_destroy(&sem_);
    }

    void add_task(T task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            task_queue_.push(std::move(task));
        }
        sem_post(&sem_);
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
};