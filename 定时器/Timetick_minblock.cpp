#include <functional>
#include <memory>
#include <queue>
#include <vector>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

using TaskCallback = std::function<void()>;

struct TimerTask {
    uint64_t id;
    uint64_t expire_time;
    TaskCallback callback;
    bool repeat;
    uint64_t interval;

    TimerTask(uint64_t expire, TaskCallback cb, bool rep = false, uint64_t inter = 0, uint64_t task_id = 0)
        : id(task_id), expire_time(expire), callback(std::move(cb)), repeat(rep), interval(inter) {}
};

class Timer {
public:
    Timer() : stop_(false), task_id_counter_(0) {
        thread_ = std::thread(&Timer::run, this);
    }

    ~Timer() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cond_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint64_t add(uint64_t delay_ms, TaskCallback cb, bool repeat = false) {
        auto expire = getNowMs() + delay_ms;
        uint64_t task_id = ++task_id_counter_;

        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace(expire, TimerTask(expire, std::move(cb), repeat, delay_ms, task_id));
        // 只有当新插入的任务是最近（堆顶）的任务时，才需要通知
        // 但为了简单，每次都 notify 也是可以接受的
        cond_.notify_one(); 
        return task_id;
    }

    // 优化：使用惰性删除或更高效的方式
    // 当前实现虽然 O(n)，但为了逻辑正确，先修复死锁
    void remove(uint64_t task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 1. 将堆中所有元素取出到临时容器（vector 不会像 pop 那样有复杂的析构风险）
        std::vector<std::pair<uint64_t, TimerTask>> temp;
        temp.reserve(tasks_.size()); // 预分配内存，避免在锁内多次扩容
        
        while (!tasks_.empty()) {
            auto task = tasks_.top();
            if (task.second.id != task_id) {
                temp.push_back(std::move(task));
            }
            tasks_.pop(); // 这里虽然调用了 pop，但因为我们持有锁且 reserve 了空间，
                          // vector 的销毁是轻量的。更严谨的做法是使用 list，但为了简单保留。
        }
        
        // 2. 将剩余任务重新放回堆
        for (auto& t : temp) {
            tasks_.push(std::move(t));
        }
        // 注意：这里不再 notify，因为只是删除，不影响等待逻辑（除非删的是最近任务，但下次循环会重新计算）
    }

private:
    uint64_t getNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void run() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (stop_) break;

            if (tasks_.empty()) {
                // 队列空，一直等
                cond_.wait(lock, [this] { return stop_; });
                continue;
            }

            // 获取最近任务
            auto nearest = tasks_.top();
            uint64_t now = getNowMs();
            uint64_t expire_time = nearest.first;

            if (now >= expire_time) {
                // 到期了
                auto task = nearest.second;
                tasks_.pop(); // 先从队列移除
                lock.unlock(); // 立即释放锁，执行回调

                task.callback();

                // 处理周期性任务：重新入队，保留原 ID
                if (task.repeat) {
                    // 注意：这里直接操作堆，不调用 add (避免锁竞争和 ID 变化)
                    // 但是 add 是对外接口，内部为了复用代码可以加一个带 ID 的内部 add
                    // 为了简单，这里直接操作（或者调用 add，但 ID 会变，所以建议内部操作）
                    
                    // 重新计算过期时间
                    uint64_t next_expire = getNowMs() + task.interval;
                    std::lock_guard<std::mutex> relock(mutex_);
                    tasks_.emplace(next_expire, TimerTask(next_expire, task.callback, true, task.interval, task.id));
                    cond_.notify_one(); // 通知可能在等待新任务的线程
                }
            } else {
                // 还没到期，等待
                auto wait_time = std::chrono::milliseconds(expire_time - now);
                // 使用 wait_for，如果被虚假唤醒，会重新检查条件
                cond_.wait_for(lock, wait_time);
                // 注意：这里不需要 notify，因为是等待超时
            }
        }
    }

private:
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_;
    uint64_t task_id_counter_;
    
    // 1. 定义比较结构体
struct CompareTask {
    // 注意：priority_queue 默认是最大堆。我们要实现最小堆，所以当 a 的过期时间 > b 的过期时间时，返回 true
    bool operator()(const std::pair<uint64_t, TimerTask>& a, const std::pair<uint64_t, TimerTask>& b) {
        return a.first > b.first; // first 是 expire_time
    }
};

// 2. 使用自定义比较结构体定义优先队列
std::priority_queue<
    std::pair<uint64_t, TimerTask>,
    std::vector<std::pair<uint64_t, TimerTask>>,
    CompareTask> tasks_; // 注意：这里只写类型名，不要加括号
};

int main() {
    Timer timer;

    // 添加一个 1秒后执行的一次性任务
    uint64_t task1_id = timer.add(1000, []() {
        std::cout << "Hello world!" << std::endl;
    }, false);

    // 添加一个 每隔500ms 执行的周期性任务
    uint64_t task2_id = timer.add(500, []() {
        std::cout << "Tick..." << std::endl;
    }, true);

    // 删除一次性任务
    timer.remove(task1_id);

    // 主线程不阻塞，定时器在后台运行
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}