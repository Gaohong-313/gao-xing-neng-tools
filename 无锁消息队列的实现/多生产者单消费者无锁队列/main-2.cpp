#include "MPSQueue-1.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// 测试数据结构
struct Data {
    int id;
    long long timestamp;
    Data(int i) : id(i), timestamp(0) {}
};

// 全局队列
MPSCQueue<Data> queue; // 非侵入式队列

void producer(int id, int count) {
    for (int i = 0; i < count; ++i) {
        queue.Enqueue(Data(id * 1000 + i));
        // 模拟生产速度
        std::this_thread::yield();
    }
    std::cout << "Producer " << id << " done." << std::endl;
}

void consumer(int count) {
    int received = 0;
    while (received < count) {
        Data data = Data(0);
        if (queue.Dequeue(data)) {
            // std::cout << "Got: " << data.id << std::endl;
            ++received;
        } else {
            // 队列空了，稍微等待
            std::this_thread::yield();
        }
    }
    std::cout << "Consumer received: " << received << std::endl;
}

int main() {
    const int PRODUCER_COUNT = 4;
    const int ITEM_PER_PRODUCER = 10000;
    const int TOTAL_ITEMS = PRODUCER_COUNT * ITEM_PER_PRODUCER;

    std::vector<std::thread> producers;
    auto start = std::chrono::high_resolution_clock::now();

    // 启动生产者
    for (int i = 0; i < PRODUCER_COUNT; ++i) {
        producers.emplace_back(producer, i, ITEM_PER_PRODUCER);
    }

    // 启动消费者 (只能启动一个)
    std::thread consumer_thread(consumer, TOTAL_ITEMS);

    // 等待结束
    for (auto& p : producers) {
        p.join();
    }
    consumer_thread.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time: " << duration.count() << " ms" << std::endl;

    return 0;
}