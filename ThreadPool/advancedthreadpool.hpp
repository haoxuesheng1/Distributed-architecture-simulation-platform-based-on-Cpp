
/*-------此为线程池源代码，使用时包含该头文件即可，切记需使用支持C++20及以上的编译器--------*/

#ifndef ADVANCED_THREADPOOL_H
#define ADVANCED_THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <future>
#include <chrono>
#include <optional>
#include <unordered_set>
#include <stop_token>
#include <cassert>
#include <iomanip>

// 线程池支持的模式
enum class PoolMode {
    FIXED,   // 固定数量的线程
    CACHED,  // 线程数量可动态增长
};

// 任务优先级
enum class TaskPriority {
    HIGH,    // 数值最小
    NORMAL,
    LOW    // 数值最大（确保在最大堆中优先出队）
};

// 线程池配置参数
struct ThreadPoolConfig {
    size_t min_threads = std::thread::hardware_concurrency();  // 最小线程数
    size_t max_threads = 1024;           // 最大线程数（CACHED模式）
    size_t max_tasks = 1024;             // 任务队列最大容量
    std::chrono::seconds idle_timeout{ 60 }; // 空闲线程超时时间
    PoolMode mode = PoolMode::CACHED;    // 默认工作模式
};

class AdvancedThreadPool {
public:
    explicit AdvancedThreadPool(ThreadPoolConfig config = {})
        : config_(std::move(config)),
        running_(true) {

        // 启动初始线程
        for (size_t i = 0; i < config_.min_threads; ++i) {
            add_worker();
        }

        // 启动管理线程（仅CACHED模式需要）
        if (config_.mode == PoolMode::CACHED) {
            manager_thread_ = std::jthread([this](std::stop_token st) {
                manage_workers(st);
                });
        }
    }

    ~AdvancedThreadPool() {
        shutdown();
    }

    // 禁用拷贝和赋值
    AdvancedThreadPool(const AdvancedThreadPool&) = delete;
    AdvancedThreadPool& operator=(const AdvancedThreadPool&) = delete;

    // 提交任务（带优先级）
    template <typename F, typename... Args>
    auto submit(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;
        using TaskType = std::packaged_task<ReturnType()>;

        // 绑定任务和参数（使用完美转发）
        auto task = std::make_shared<TaskType>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future();

        {
            std::unique_lock lock(queue_mutex_);

            // 检查线程池状态
            if (!running_) {
                throw std::runtime_error("ThreadPool is shutdown");
            }

            // 等待队列有空位（带超时）
            if (task_queue_.size() >= config_.max_tasks) {
                if (!task_available_.wait_for(lock, std::chrono::seconds(1), [this] {
                    return task_queue_.size() < config_.max_tasks || !running_;
                    })) {
                    throw std::runtime_error("Task queue full, submit timeout");
                }
            }

            // 按优先级插入任务
            task_queue_.push({ priority, [task] { (*task)(); } });
        }

        // 通知工作线程有新任务
        task_available_.notify_one();

        // 动态扩容检查
        if (should_expand()) {
            expand_workers();
        }

        return result;
    }

    // 提交任务（默认优先级）
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        return submit(TaskPriority::NORMAL,
            std::forward<F>(f),
            std::forward<Args>(args)...);
    }

    // 优雅关闭线程池
    void shutdown() {
        if (!running_.exchange(false)) return;

        // 通知所有线程
        task_available_.notify_all();
        // 向管理线程发送停止请求（使用 jthread 内置方法）
        if (manager_thread_.joinable()) {
            manager_thread_.request_stop();  // 替代 stop_source_.request_stop()
        }
    
        // 等待管理线程结束
        if (manager_thread_.joinable()) {
            manager_thread_.join();
        }
       
        // 创建临时容器存放工作线程
        std::vector<std::thread> workers;
        {
            // 短暂锁定以复制工作线程
            std::unique_lock lock(queue_mutex_);
            workers = std::move(workers_);  // 转移所有权
            expired_workers_.clear();        // 清理过期线程集合
        }

        // 在无锁状态下等待工作线程结束
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 获取当前线程数
    size_t worker_count() const {
        std::unique_lock lock(queue_mutex_);
        return workers_.size();
    }

    // 获取等待任务数
    size_t pending_tasks() const {
        std::unique_lock lock(queue_mutex_);
        return task_queue_.size();
    }

private:
    // 任务包装结构
    struct TaskItem {
        TaskPriority priority;
        std::function<void()> task;
    };

    // 优先队列比较函数
    struct TaskCompare {
        bool operator()(const TaskItem& a, const TaskItem& b) const {
            // 数值越小优先级越高（HIGH=0, LOW=2）
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        }
    };

    // 任务队列类型（优先级队列）
    using TaskQueue = std::priority_queue<
        TaskItem,
        std::vector<TaskItem>,
        TaskCompare
    >;

    // 添加工作线程
    void add_worker() {
        workers_.emplace_back([this] {
            worker_routine();
            });
    }

    // 工作线程主循环
    void worker_routine() {
        auto last_active = std::chrono::steady_clock::now();

        while (running_) {
            std::optional<TaskItem> task;

            {
                std::unique_lock lock(queue_mutex_);

                // 等待任务或超时（CACHED模式）
                if (config_.mode == PoolMode::CACHED) {
                    // 使用wait_until避免虚假唤醒
                    auto timeout_point = last_active + config_.idle_timeout;

                    while (task_queue_.empty() && running_) {
                        if (task_available_.wait_until(lock, timeout_point) == std::cv_status::timeout) {
                            // 检查是否应该退出（空闲超时且超过最小线程数）
                            if (workers_.size() > config_.min_threads) {
                                // 标记当前线程为可移除
                                auto self = std::this_thread::get_id();
                                expired_workers_.insert(self);
                                return;
                            }
                            // 重置超时时间
                            last_active = std::chrono::steady_clock::now();
                            timeout_point = last_active + config_.idle_timeout;
                        }
                    }
                }
                // FIXED模式无限等待
                else {
                    task_available_.wait(lock, [this] {
                        return !task_queue_.empty() || !running_;
                        });
                }

                if (!running_ && task_queue_.empty()) {
                    return;  // 关闭线程池
                }

                if (!task_queue_.empty()) {
                    task = std::move(task_queue_.top());
                    task_queue_.pop();
                    last_active = std::chrono::steady_clock::now();
                }
            }

            // 执行任务
            if (task) {
                try {
                    // 添加任务开始执行日志
                    {
                        static std::mutex log_mutex;
                        std::scoped_lock lock(log_mutex);
                        auto now = std::chrono::system_clock::now();
                        std::time_t time = std::chrono::system_clock::to_time_t(now);
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;
                
                        std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S")
                                << '.' << std::setfill('0') << std::setw(3) << ms.count()
                                << "] Thread " << std::this_thread::get_id()
                                 << " 开始执行优先级: " 
                                << static_cast<int>(task->priority) << std::endl;
                    }
                    task->task();
                }
                catch (const std::exception& e) {
                    // 捕获标准异常（如std::runtime_error等）
                    std::string error_msg = "Task execution failed: " + std::string(e.what());

                    // 记录错误日志（包含线程ID、时间、错误信息）
                    {
                        static std::mutex log_mutex;  // 确保日志线程安全
                        std::scoped_lock lock(log_mutex);

                        auto now = std::chrono::system_clock::now();
                        std::time_t time = std::chrono::system_clock::to_time_t(now);

                        std::cerr << "[" << std::ctime(&time)
                            << "] Thread " << std::this_thread::get_id()
                            << " error: " << error_msg << std::endl;
                    }
                }
            }
        }
    }

    // 管理线程（动态调整线程池大小）
    void manage_workers(std::stop_token st) {
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // 清理过期线程
            cleanup_expired_workers();

            // 动态扩容检查
            if (should_expand()) {
                expand_workers();
            }
        }
    }

    // 清理过期线程
    void cleanup_expired_workers() {
        std::unique_lock lock(queue_mutex_);
        if (expired_workers_.empty()) return;
    
        auto it = workers_.begin();
        while (it != workers_.end() && workers_.size() > config_.min_threads) {
            if (expired_workers_.find(it->get_id()) != expired_workers_.end()) {
                if (it->joinable()) it->join();
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
        expired_workers_.clear();
    }

    // 判断是否需要扩容
    bool should_expand() const {
        if (config_.mode != PoolMode::CACHED) return false;
        if (!running_) return false;

        std::unique_lock lock(queue_mutex_);
        return !task_queue_.empty() &&
            workers_.size() < config_.max_threads;
    }

    // 扩容线程池
    void expand_workers() {
        std::unique_lock lock(queue_mutex_);
        if (!running_) return;

        const size_t max_to_add = config_.max_threads - workers_.size();
        const size_t tasks = task_queue_.size();
        const size_t needed = std::min(tasks, max_to_add);

        for (size_t i = 0; i < needed; ++i) {
            add_worker();
        }
    }

private:
    ThreadPoolConfig config_;             // 线程池配置
    std::vector<std::thread> workers_;    // 工作线程集合
    std::atomic_bool running_;            // 运行状态

    // 任务队列和同步原语
    TaskQueue task_queue_;                // 优先级任务队列
    mutable std::mutex queue_mutex_;      // 队列互斥锁
    std::condition_variable task_available_; // 任务可用通知

    // 过期线程管理
    std::unordered_set<std::thread::id> expired_workers_;

    // 管理线程（CACHED模式专用）
    std::jthread manager_thread_;         // C++20的jthread（自动管理）
};

#endif // ADVANCED_THREADPOOL_H
