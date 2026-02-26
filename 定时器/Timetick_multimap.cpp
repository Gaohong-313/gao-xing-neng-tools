#include <functional>
#include <memory>

// 定义任务的回调函数类型
// 使用简单的无参数无返回值函数，其他类型可以使用C++新特性进行包装函数
using TaskCallback = std::function<void()>;  

struct TimerTask {
    uint64_t id; // 任务ID
    uint64_t expire_time; // 这个任务的到期时间戳 (毫秒/微秒)
    TaskCallback callback; // 包装好的函数
    bool repeat; // 是否是周期性任务 (如: 每隔1秒执行一次)
    uint64_t interval; // 如果是周期性任务，间隔是多少

    TimerTask(uint64_t expire, TaskCallback cb, bool rep = false, uint64_t inter = 0, uint64_t task_id = 0)
        : id(task_id), expire_time(expire), callback(std::move(cb)), repeat(rep), interval(inter) {}
};

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <functional>
#include <atomic>

class Timer {
public:
    Timer() : stop_(false), task_id_counter_(0) {
        // 构造时启动工作线程
        thread_ = std::thread(&Timer::run, this);
    }

    ~Timer() { // 阻塞
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cond_.notify_one(); // 唤醒线程，让它检测到 stop_ 退出
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // 对外提供的 Push 接口
    // delay_ms: 延迟多少毫秒后执行
    // cb: 回调函数
    // repeat: 是否重复
    uint64_t add(uint64_t delay_ms, TaskCallback cb, bool repeat = false) {
        auto expire = getNowMs() + delay_ms;
        uint64_t task_id = ++task_id_counter_; // 新任务的ID
        
        std::unique_lock<std::mutex> lock(mutex_);
        tasks_.emplace(expire, TimerTask(expire, std::move(cb), repeat, delay_ms, task_id));
        lock.unlock();
        
        // 插入后，可能改变了最近的到期时间，需要唤醒等待的线程重新计算等待时间
        cond_.notify_one(); 
        return task_id;
    }

    // 删除接口
    // task_id: 要删除的任务ID
    void remove(uint64_t task_id) {
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->second.id == task_id) {
                tasks_.erase(it);
                cond_.notify_one(); // 通知可能的等待线程
                break;
            }
        }
    }

private:
    // 获取当前时间戳 (毫秒)
    uint64_t getNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 工作线程的主循环
    void run() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 检查是否析构
            if (stop_) break;

            // 获取最近的一个任务
            if (!tasks_.empty()) {
                auto nearest = tasks_.begin(); // map/multimap 是有序的，begin 就是最近的
                uint64_t now = getNowMs();
                uint64_t expire_time = nearest->first;

                if (now >= expire_time) {
                    // 任务到期了！
                    auto task = nearest->second;
                    
                    // 在解锁状态下执行任务 (避免阻塞整个定时器)
                    lock.unlock();
                    task.callback(); 

                    // 处理周期性任务
                    if (task.repeat) {
                        // 重新计算到期时间并插入
                        add(task.interval, task.callback, true);
                    }
                    
                    // 删除旧任务
                    lock.lock();
                    tasks_.erase(nearest);
                } else {
                    // 任务还没到期，计算还要等多久
                    // 这里使用条件变量等待，避免忙等
                    auto wait_time = std::chrono::milliseconds(expire_time - now);
                    cond_.wait_for(lock, wait_time);
                }
            } else {
                // 任务队列为空，一直等待直到有新任务插入
                cond_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            }
        }
    }

private:
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_;
    
    // 任务ID计数器
    uint64_t task_id_counter_;
    
    // multimap<到期时间, 任务>
    // 注意：这里用 multimap 是因为可能有多个任务在同一时刻触发
    std::multimap<uint64_t, TimerTask> tasks_; 
};

// 测试用例
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
