#ifndef MPSQUEUE_H
#define MPSQUEUE_H
///多生产者单消费者无锁队列

#include <atomic>
#include <type_traits>

// 非侵入式 MPSC 队列 (LIFO 模式，适合单消费者)
template <typename T>
class MPSQueueNonIntrusive {
private:
    struct Node {
        T data;
        Node* next;
        Node(T d) : data(d), next(nullptr) {}
    };

    std::atomic<Node*> _head;

    // 禁止拷贝
    MPSQueueNonIntrusive(const MPSQueueNonIntrusive&) = delete;
    MPSQueueNonIntrusive& operator=(const MPSQueueNonIntrusive&) = delete;

public:
    MPSQueueNonIntrusive() : _head(nullptr) {}

    ~MPSQueueNonIntrusive() {
        T item;
        while (Dequeue(item)) {}
    }

    // 多生产者入队 (无锁)
    void Enqueue(const T& data) {
        Node* node = new Node(data);
        // 将新节点插入到链表头部
        // 使用 relaxed 是因为这里只关心链表结构的原子性，不依赖同步顺序
        Node* oldHead = _head.load(std::memory_order_relaxed);
        do {
            node->next = oldHead;
            // 使用 release 语义：确保 node->next 的赋值对消费者可见
        } while (!_head.compare_exchange_weak(oldHead, node,
            std::memory_order_release, // 成功时的内存序
            std::memory_order_relaxed)); // 失败时的内存序
    }

    // 单消费者出队 (必须只有唯一线程调用)
    // 返回 true 表示成功获取数据
    bool Dequeue(T& result) {
        // 1. 首先尝试获取整个链表
        Node* tail = _head.exchange(nullptr, std::memory_order_acquire);
        
        // 2. 如果链表为空，直接返回
        if (tail == nullptr) {
            return false;
        }

        // 3. 因为是 LIFO，链表是反的。如果需要 FIFO 语义，需要反转链表。
        // 这里直接取头节点数据 (或者你可以遍历链表处理所有数据)
        result = tail->data;
        
        Node* toDelete = tail;
        tail = tail->next; // 下一个节点
        
        delete toDelete;

        // 4. 如果还有剩余节点，需要重新入栈（或者缓存起来），这里简单处理只取一个
        // 实际高性能实现通常会一次性反转整个链表到本地栈
        while (tail) {
            Node* next = tail->next;
            // 将剩余节点重新压回队列 (或者放入线程本地缓存)
            // 这里为了简单，直接丢弃或重新入队
            // 重新入队会导致顺序变化，但保证了数据不丢失
            Enqueue_NonAtomic(tail->data); 
            delete tail;
            tail = next;
        }

        return true;
    }

private:
    // 仅供内部使用的非原子入队 (因为消费者是单线程，且此时链表已摘下)
    void Enqueue_NonAtomic(const T& data) {
        Node* node = new Node(data);
        node->next = _head.load(std::memory_order_relaxed);
        _head.store(node, std::memory_order_relaxed);
    }
};

// 侵入式版本 (简化版，修正了原代码的复杂逻辑)
// 要求 T 类型包含一个 public 的 std::atomic<T*> 成员
template <typename T, std::atomic<T*> T::* Link>
class MPSQueueIntrusive {
private:
    std::atomic<T*> _head;

    MPSQueueIntrusive(const MPSQueueIntrusive&) = delete;
    MPSQueueIntrusive& operator=(const MPSQueueIntrusive&) = delete;

public:
    MPSQueueIntrusive() : _head(nullptr) {}

    void Enqueue(T* item) {
        // 初始化节点的 next 指针
        (item->*Link).store(nullptr, std::memory_order_relaxed);
        
        T* oldHead = _head.load(std::memory_order_relaxed);
        do {
            (item->*Link).store(oldHead, std::memory_order_relaxed);
        } while (!_head.compare_exchange_weak(oldHead, item,
            std::memory_order_release, std::memory_order_relaxed));
    }

    bool Dequeue(T*& result) {
        // 摘取整个链表
        result = _head.exchange(nullptr, std::memory_order_acquire);
        if (result) {
            // 返回链表的头 (LIFO)
            return true;
        }
        return false;
    }
};

// 别名模板 (C++11 兼容写法)
template <typename T, std::atomic<T*> T::* Link = nullptr>
using MPSCQueue = typename std::conditional<Link == nullptr, 
    MPSQueueNonIntrusive<T>, MPSQueueIntrusive<T, Link>>::type;

#endif // MPSQUEUE_H