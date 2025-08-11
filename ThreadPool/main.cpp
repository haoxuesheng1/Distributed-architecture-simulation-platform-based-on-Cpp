
/*--------以下测试代码为两种模式下的简易测试，你也可以自行编辑测试代码进行测试--------*/

#include "advancedthreadpool.hpp"
#include <random>
#include <sstream>

using namespace std;
using namespace std::chrono_literals;


/*
该测试代码是针对CACHED模式下的测试
*/


#if 0
// 时间戳格式化函数
std::string time_to_string(std::chrono::system_clock::time_point tp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    
    // 线程安全的时间转换
    #if defined(_WIN32)
        localtime_s(&tm, &t);
    #else
        localtime_r(&t, &tm);
    #endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.' 
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// 生成随机数用于模拟任务执行时间
int random_duration(int min, int max) {
    static mt19937 generator(random_device{}());
    uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

int main() {
    // 配置线程池
    ThreadPoolConfig config;
    config.min_threads = 2;                // 最小线程数
    config.max_threads = 4;                // 最大线程数
    config.max_tasks = 10;                 // 任务队列容量
    config.idle_timeout = 5s;              // 空闲线程超时时间
    config.mode = PoolMode::CACHED;        // 使用动态扩缩容模式

    // 创建线程池
    AdvancedThreadPool pool(config);

    cout << "=== 线程池启动 ===" << endl;
    cout << "初始线程数: " << pool.worker_count() << endl;

    vector<future<string>> results;

    // 提交少量任务测试基本功能
    for (int i = 0; i < 3; i++) {
        results.push_back(pool.submit([i] {
            auto dur = random_duration(100, 300);
            this_thread::sleep_for(chrono::milliseconds(dur));
            return "任务" + to_string(i) + "完成";
        }));
    }

    cout << "已提交 " << results.size() << " 个任务" << endl;

    // 等待所有任务完成
    for (auto& fut : results) {
        cout << fut.get() << endl;
    }

    cout << "\n=== 基本任务完成 ===" << endl;
    cout << "当前线程数: " << pool.worker_count() << endl;

    // 提交更多任务测试扩容
    vector<future<string>> more_results;
    cout << "\n提交更多任务测试扩容..." << endl;

    for (int i = 0; i < 8; i++) {
        more_results.push_back(pool.submit([i] {
            auto dur = random_duration(200, 500);
            this_thread::sleep_for(chrono::milliseconds(dur));
            return "扩容任务" + to_string(i) + "完成";
        }));
    }

    cout << "当前线程数: " << pool.worker_count()
        << " (应接近最大线程数: " << config.max_threads << ")" << endl;

    // 获取部分结果
    for (int i = 0; i < 4; i++) {
        cout << more_results[i].get() << endl;
    }

    // 测试缩容
    cout << "\n等待线程池缩容（空闲超时5秒）..." << endl;
    this_thread::sleep_for(6s);
    cout << "当前线程数: " << pool.worker_count()
        << " (应接近最小线程数: " << config.min_threads << ")" << endl;

    // 测试优先级 - 修复版本
    cout << "\n测试任务优先级（预期顺序：高->普通->低）..." << endl;
    vector<future<string>> priority_results(3);  // 预分配3个位置

    // 任务工厂函数
    auto make_task = [](TaskPriority prio, string name, int sleep_ms) {
        return [=] {
            auto start = std::chrono::system_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            return name + " 开始: " + time_to_string(start) + 
                   " 结束: " + time_to_string(std::chrono::system_clock::now());
        };
    };

    // 按优先级顺序提交任务（高->普通->低）
    priority_results[0] = pool.submit(TaskPriority::HIGH, 
        make_task(TaskPriority::HIGH, "高优先级任务", 10));
    priority_results[1] = pool.submit(TaskPriority::NORMAL, 
        make_task(TaskPriority::NORMAL, "普通优先级任务", 50));
    priority_results[2] = pool.submit(TaskPriority::LOW, 
        make_task(TaskPriority::LOW, "低优先级任务", 100));

    // 按优先级顺序获取结果
    cout << "按优先级顺序获取结果:" << endl;
    cout << priority_results[0].get() << endl;  // 高优先级
    cout << priority_results[1].get() << endl;  // 普通优先级
    cout << priority_results[2].get() << endl;  // 低优先级

    cout << "\n关闭线程池..." << endl;
    pool.shutdown();
    cout << "线程池已关闭" << endl;

    return 0;
}
#endif


/*
该测试代码是针对FIXED模式下的测试
*/


#if 1
// 打印当前时间（用于观察任务执行顺序）
void print_time() {
    auto now = chrono::system_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto now_time = chrono::system_clock::to_time_t(now);
    cout << put_time(localtime(&now_time), "%H:%M:%S")
         << '.' << setfill('0') << setw(3) << ms.count() << " ";
}

// 模拟工作任务
void work_task(int task_id, int duration_ms) {
    print_time();
    cout << "任务 " << task_id << " 开始执行 (线程ID: " 
         << this_thread::get_id() << ")" << endl;
    
    // 模拟工作
    this_thread::sleep_for(chrono::milliseconds(duration_ms));
    
    print_time();
    cout << "任务 " << task_id << " 执行完成" << endl;
}

int main() {
    // 配置线程池为FIXED模式
    ThreadPoolConfig config;
    config.min_threads = 3;               // 固定线程数
    config.max_threads = 3;               // 固定模式下，最大线程数应等于最小线程数
    config.max_tasks = 20;                // 任务队列容量
    config.mode = PoolMode::FIXED;        // 使用固定线程数模式
    
    // 创建线程池
    AdvancedThreadPool pool(config);
    
    cout << "=== 固定线程数模式测试 ===" << endl;
    cout << "配置的固定线程数: " << config.min_threads << endl;
    cout << "初始线程数: " << pool.worker_count() << endl;
    
    // 测试1: 提交少量任务(少于线程数)
    cout << "\n=== 测试1: 提交少量任务 ===" << endl;
    vector<future<void>> results1;
    for (int i = 0; i < 2; ++i) {
        results1.push_back(pool.submit(work_task, i, 500));
    }
    
    // 等待任务完成
    this_thread::sleep_for(100ms); // 等待任务开始
    cout << "任务执行中线程数: " << pool.worker_count() << endl;
    
    for (auto& fut : results1) {
        fut.get();
    }
    cout << "少量任务完成后线程数: " << pool.worker_count() << endl;
    
    // 测试2: 提交大量任务(多于线程数)
    cout << "\n=== 测试2: 提交大量任务 ===" << endl;
    vector<future<void>> results2;
    for (int i = 2; i < 10; ++i) {
        results2.push_back(pool.submit(work_task, i, 300));
    }
    
    // 等待部分任务开始执行
    this_thread::sleep_for(100ms);
    cout << "大量任务执行中线程数: " << pool.worker_count() << endl;
    cout << "等待中的任务数: " << pool.pending_tasks() << endl;
    
    // 等待所有任务完成
    for (auto& fut : results2) {
        fut.get();
    }
    cout << "大量任务完成后线程数: " << pool.worker_count() << endl;
    
    // 测试3: 空闲一段时间后检查线程数
    cout << "\n=== 测试3: 空闲后检查线程数 ===" << endl;
    cout << "等待10秒让线程空闲..." << endl;
    this_thread::sleep_for(10s);
    cout << "空闲后线程数: " << pool.worker_count() << endl;
    
    // 测试4: 再次提交任务，验证线程可复用
    cout << "\n=== 测试4: 验证线程复用 ===" << endl;
    vector<future<void>> results3;
    for (int i = 10; i < 12; ++i) {
        results3.push_back(pool.submit(work_task, i, 200));
    }
    
    // 等待任务完成
    for (auto& fut : results3) {
        fut.get();
    }
    cout << "再次提交任务后线程数: " << pool.worker_count() << endl;
    
    cout << "\n=== 所有测试完成 ===" << endl;
    pool.shutdown();
    cout << "线程池已关闭" << endl;
    
    return 0;
}
#endif