#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <iostream>

//单消费者单生产者无锁队列

template<typename T>
class LockFreeQueue {
private:
    // 队列节点结构
    struct Node {
        T data;
        Node* next;
        
        Node() : next(nullptr) {}
        Node(const T& d) : data(d), next(nullptr) {}
    };

    // 两个队列的头尾指针
    // A队列: 生产者写入队列 (current buffer)
    std::atomic<Node*> A_head;
    std::atomic<Node*> A_tail;
    
    // B队列: 消费者读取队列 (drained buffer)
    std::atomic<Node*> B_head;
    std::atomic<Node*> B_tail;

    // 辅助函数：创建一个空节点（哨兵节点）
    Node* create_sentinel() {
        return new Node();
    }

public:
    LockFreeQueue() {
        Node* sentinel = create_sentinel();
        // 初始化 A 队列 (生产者)
        A_head.store(sentinel, std::memory_order_relaxed);
        A_tail.store(sentinel, std::memory_order_relaxed);
        
        // 初始化 B 队列 (消费者)
        sentinel = create_sentinel();
        B_head.store(sentinel, std::memory_order_relaxed);
        B_tail.store(sentinel, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        // 清理 A 队列残留
        while (Node* h = A_head.load(std::memory_order_relaxed)) {
            Node* n = h->next;
            delete h;
            if (!n) break;
            h = n;
        }
        // 清理 B 队列残留
        while (Node* h = B_head.load(std::memory_order_relaxed)) {
            Node* n = h->next;
            delete h;
            if (!n) break;
            h = n;
        }
    }

    // 生产者接口：向 A 队列推入数据
    void push(const T& data) {
        Node* new_node = new Node(data);
        
        // 获取当前的尾节点 (Relaxed 因为只是读取指针，不涉及同步)
        Node* old_tail = A_tail.load(std::memory_order_relaxed);
        
        // 将新节点链接到旧尾节点后面
        // 这里不需要原子操作，因为只有生产者线程在修改链表的这一部分
        old_tail->next = new_node;
        
        // 更新尾指针
        // 使用 release 语义：确保上面的赋值 (old_tail->next) 对其他线程（在交换时）可见
        A_tail.store(new_node, std::memory_order_release);
    }

    // 消费者接口：从 B 队列获取数据
    // 返回值: true 表示成功获取，false 表示队列彻底空了（通常不会发生，除非交换后也空）
    bool get(T& result) {
        // 1. 尝试从 B 队列（本地缓冲）获取
        Node* local_head = B_head.load(std::memory_order_relaxed);
        Node* local_tail = B_tail.load(std::memory_order_relaxed);
        
        // 检查 B 队列是否为空
        if (local_head->next == nullptr) {
            // B 队列空了，需要执行“交换”操作
            if (!swap_buffers()) {
                // 交换后依然为空（极罕见情况，生产者还没写入）
                return false;
            }
            // 交换成功后，重新加载 B 队列的头
            local_head = B_head.load(std::memory_order_relaxed);
        }

        // 2. 从 B 队列取出数据
        Node* next_node = local_head->next;
        result = next_node->data; // 拷贝数据

        // 更新 B 队列的头指针
        // 使用 relaxed 即可，因为 B 队列只被当前消费者修改
        B_head.store(next_node, std::memory_order_relaxed);

        // 3. 可选：清理旧的头节点内存（哨兵模式下，我们不立即删除哨兵，只删除数据节点）
        // 这里简单处理，实际生产环境可能需要延迟删除 (Hazard Pointer 或 RCU)
        // delete local_head; // 注意：如果是哨兵节点，不能随便删，这里为了简化逻辑，假设节点可删
        // 但在我们的初始化中，head 是哨兵，所以这里不直接 delete local_head

        return true;
    }

private:
    // 核心交换逻辑：将 A 队列的内容“翻转”到 B 队列
    bool swap_buffers() {
        // 内存序关键点：
        // 1. 我们需要获取 A 队列的最新状态
        // 2. 我们需要将 A 队列清空，以便生产者继续写入
        
        // 加载 A 队列的头和尾
        // 使用 acquire 语义：确保我们能看到生产者在 push 时写入的所有数据
        Node* a_head = A_head.load(std::memory_order_acquire);
        Node* a_tail = A_tail.load(std::memory_order_acquire);

        // 检查 A 队列是否真的有数据 (a_head != a_tail)
        if (a_head == a_tail) {
            // A 队列为空，无需交换
            return false;
        }

        // 准备交换：
        // 1. 将 A 队列清空 (将 A_head 指向一个新的哨兵，或者指向 A_tail)
        // 2. 将 B 队列的尾部连接到 A 队列的头部
        
        // 这里我们采用“摘链”方式：
        // B 队列的当前尾部 (B_tail) 的 next 指向 A 队列的第一个有效节点
        Node* b_tail = B_tail.load(std::memory_order_relaxed);
        
        // 建立连接：将 A 队列整体挂到 B 队列后面
        b_tail->next = a_head->next; 

        // 更新 B 队列的尾指针为 A 队列的尾
        B_tail.store(a_tail, std::memory_order_relaxed);

        // 关键步骤：重置 A 队列
        // 将 A_head 移动到 A_tail 的位置，清空 A 队列
        // 这里使用 release 语义：确保上面的 b_tail->next 赋值对生产者可见（虽然生产者通常只看 tail，但为了严谨）
        A_head.store(a_tail, std::memory_order_release);

        // 注意：A_tail 不需要重置，因为生产者 push 时会更新 tail，且 tail 永远在 head 后面
        // 如果 A_tail 落后了，生产者 push 会修正它，或者我们也可以尝试 CAS 更新，但通常不需要

        return true;
    }
};

#endif // LOCK_FREE_QUEUE_H