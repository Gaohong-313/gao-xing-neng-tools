#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_THREADS 100
#define MAX_LOCKS 100

// 任务函数类型定义
typedef void* (*task_func_t)(void* arg);

// 任务包：包含任务函数、参数、线程ID、锁ID
typedef struct {
    task_func_t func;
    void* arg;
    int thread_id;
    int lock_id;
} task_package_t;

// 锁节点结构体
typedef struct mtxNode {
    int thread_id;
    struct mtxNode *next;
} mtxNode;

// 锁结构体
typedef struct {
    int is_locked;  // 0 表示未锁定，非0表示持有锁的线程ID
    mtxNode *head;  // 等待队列头
} Lock;

Lock locks[MAX_LOCKS];
pthread_mutex_t lock_mutex;
pthread_t search_thread;

// 有向图：表示线程间的等待关系（用于死锁检测）
int graph[MAX_THREADS][MAX_THREADS];

// 初始化系统
void init_system() {
    for (int i = 0; i < MAX_LOCKS; ++i) {
        locks[i].is_locked = 0;
        locks[i].head = NULL;
    }
    pthread_mutex_init(&lock_mutex, NULL);
    for (int i = 0; i < MAX_THREADS; ++i)
        for (int j = 0; j < MAX_THREADS; ++j)
            graph[i][j] = 0;
}

// 添加边
void add_edge(int src, int dest) {
    pthread_mutex_lock(&lock_mutex);
    graph[src][dest] = 1;
    pthread_mutex_unlock(&lock_mutex);
}

// 删除边
void remove_edge(int src, int dest) {
    pthread_mutex_lock(&lock_mutex);
    graph[src][dest] = 0;
    pthread_mutex_unlock(&lock_mutex);
}

// 检测环（DFS）
int is_cyclic_util(int v, int visited[], int rec_stack[]) {
    if (!visited[v]) {
        visited[v] = 1;
        rec_stack[v] = 1;

        for (int i = 0; i < MAX_THREADS; ++i) {
            if (graph[v][i]) {
                if (!visited[i] && is_cyclic_util(i, visited, rec_stack))
                    return 1;
                else if (rec_stack[i])
                    return 1;
            }
        }
    }
    rec_stack[v] = 0;
    return 0;
}

int detect_cycle() {
    int visited[MAX_THREADS];
    int rec_stack[MAX_THREADS];
    pthread_mutex_lock(&lock_mutex);
    for (int i = 0; i < MAX_THREADS; ++i) {
        visited[i] = 0;
        rec_stack[i] = 0;
    }
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (!visited[i] && is_cyclic_util(i, visited, rec_stack)) {
            pthread_mutex_unlock(&lock_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&lock_mutex);
    return 0;
}

// 死锁检测线程
void *deadlock_detector(void *v) {
    while (1) {
        sleep(1);
        if (detect_cycle()) {
            printf("💀 死锁 detected! 程序退出。\n");
            exit(EXIT_FAILURE);
        }
    }
    return NULL;
}

// 执行任务并自动管理锁
void execute_with_lock(int lock_id, int thread_id, task_func_t task, void* arg) {
    pthread_mutex_lock(&lock_mutex);

    // 如果锁被占用，进入等待队列，并建立等待边
    if (locks[lock_id].is_locked) {
        // 创建等待节点
        mtxNode *node = malloc(sizeof(mtxNode));
        node->thread_id = thread_id;
        node->next = locks[lock_id].head;
        locks[lock_id].head = node;

        // 建立等待关系：thread_id 等待持有锁的线程
        add_edge(thread_id, locks[lock_id].is_locked);

        // 释放全局锁，进入等待（这里简化为忙等+sleep，实际可用条件变量优化）
        pthread_mutex_unlock(&lock_mutex);

        // 等待锁被释放（轮询检查）
        while (1) {
            pthread_mutex_lock(&lock_mutex);
            if (locks[lock_id].is_locked == 0) {
                // 锁空闲，获取它
                locks[lock_id].is_locked = thread_id;
                // 从等待队列中移除自己（其实是队头，因为FIFO）
                mtxNode *node = locks[lock_id].head;
                if (node && node->thread_id == thread_id) {
                    locks[lock_id].head = node->next;
                    remove_edge(thread_id, node->thread_id);
                    free(node);
                }
                pthread_mutex_unlock(&lock_mutex);
                break;
            }
            pthread_mutex_unlock(&lock_mutex);
            usleep(1000); // 小延时避免忙等
        }

        // 执行任务
        task(arg);

        // 任务执行完，释放锁
        pthread_mutex_lock(&lock_mutex);
        locks[lock_id].is_locked = 0;
        pthread_mutex_unlock(&lock_mutex);

    } else {
        // 锁空闲，直接获取
        locks[lock_id].is_locked = thread_id;
        pthread_mutex_unlock(&lock_mutex);

        // 执行任务
        task(arg);

        // 释放锁
        pthread_mutex_lock(&lock_mutex);
        locks[lock_id].is_locked = 0;
        pthread_mutex_unlock(&lock_mutex);
    }
}

// 示例共享资源
int global_counter = 0;

// 示例任务函数：对 global_counter 累加
void* sum_task(void* arg) {
    int val = *(int*)arg;
    for (int i = 0; i < val; i++) {
        global_counter++;
    }
    printf("线程 %d 执行了 %d 次累加，当前 global_counter = %d\n", 
           ((task_package_t*)arg)->thread_id, val, global_counter);
    return NULL;
}

// 线程入口函数
void* thread_entry(void* arg) {
    task_package_t* pkg = (task_package_t*)arg;
    
    execute_with_lock(pkg->lock_id, pkg->thread_id, pkg->func, &pkg->thread_id);
    free(pkg); // 释放任务包
    return NULL;
}

int main() {
    init_system();

    // 启动死锁检测线程
    pthread_create(&search_thread, NULL, deadlock_detector, NULL);

    // 创建两个线程，使用锁0执行任务
    pthread_t t1, t2;

    task_package_t* pkg1 = malloc(sizeof(task_package_t));
    pkg1->lock_id = 0;
    pkg1->thread_id = 1;
    pkg1->func = sum_task;
    int val1 = 1000;
    // 我们传入 val1 作为参数，但这里简化，用 thread_id 模拟

    pthread_create(&t1, NULL, thread_entry, pkg1);

    task_package_t* pkg2 = malloc(sizeof(task_package_t));
    pkg2->lock_id = 0;
    pkg2->thread_id = 2;
    pkg2->func = sum_task;
    int val2 = 1000;

    pthread_create(&t2, NULL, thread_entry, pkg2);

    // 等待线程结束
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("最终结果: global_counter = %d\n", global_counter);

    return 0;
}