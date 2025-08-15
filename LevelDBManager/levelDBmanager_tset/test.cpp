
/*--------这是levelDBmanager模块的测试leveldb_test，建议：根据项目情况自行编写测试代码--------*/

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <iomanip>
#include "levelDBmanager.hpp"

using namespace std;
using namespace std::chrono;

// 测试数据生成器
class TestDataGenerator {
public:
    static string randomString(int length) {
        static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        
        static mt19937 rng(random_device{}());
        uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);
        
        string result;
        result.reserve(length);
        for (int i = 0; i < length; ++i) {
            result += alphanum[dist(rng)];
        }
        return result;
    }
};

// 性能测试结果
struct PerformanceResult {
    long operations;
    long duration_ms;
    double ops_per_sec;
};

// 测试函数声明
void testBasicOperations();
void testBatchOperations();
void testIterator();
void testRangeQueries();
void testMultiThreadedAccess();
PerformanceResult testWritePerformance(int num_operations, int key_size, int value_size);
PerformanceResult testReadPerformance(int num_operations, int key_size);

int main() {
    const string test_db_path = "./test_leveldb";
    
    try {
        // 初始化 LevelDB
        cout << "=== Initializing LevelDB ===" << endl;
        LevelDBManager::getInstance().initialize(test_db_path);
        cout << "Database initialized at: " << test_db_path << endl;
        
        // 运行测试
        cout << "\n=== Testing Basic Operations ===" << endl;
        testBasicOperations();
        
        cout << "\n=== Testing Batch Operations ===" << endl;
        testBatchOperations();
        
        cout << "\n=== Testing Iterator ===" << endl;
        testIterator();
        
        cout << "\n=== Testing Range Queries ===" << endl;
        testRangeQueries();
        
        cout << "\n=== Performance Tests ===" << endl;
        auto writeResult = testWritePerformance(1000000, 16, 256);
        cout << "Write Performance: " 
             << writeResult.operations << " ops in " 
             << writeResult.duration_ms << "ms (" 
             << fixed << setprecision(2) << writeResult.ops_per_sec << " ops/sec)" << endl;
        
        auto readResult = testReadPerformance(1000000, 16);
        cout << "Read Performance: " 
             << readResult.operations << " ops in " 
             << readResult.duration_ms << "ms (" 
             << fixed << setprecision(2) << readResult.ops_per_sec << " ops/sec)" << endl;
        
        cout << "\n=== Testing Multi-threaded Access ===" << endl;
        testMultiThreadedAccess();
        
        // 显示统计信息
        cout << "\n=== Database Statistics ===" << endl;
        cout << LevelDBManager::getInstance().getStats() << endl;
        
        // 关闭数据库
        cout << "\n=== Shutting Down LevelDB ===" << endl;
        LevelDBManager::getInstance().shutdown();
        cout << "Database shutdown successfully." << endl;
    } 
    catch (const LevelDBException& e) {
        cerr << "LevelDB ERROR: " << e.what() << endl;
        return 1;
    }
    catch (const exception& e) {
        cerr << "ERROR: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}

// 测试基本操作：put, get, exists, delete
void testBasicOperations() {
    LevelDBManager& db = LevelDBManager::getInstance();
    
    // 测试 put 和 get
    db.put("test_key", "test_value");
    string value;
    if (db.get("test_key", value)) {
        cout << "Get operation successful: test_key = " << value << endl;
    } else {
        cout << "Get operation failed!" << endl;
    }
    
    // 测试 exists
    if (db.exists("test_key")) {
        cout << "Exists check successful: test_key exists" << endl;
    } else {
        cout << "Exists check failed!" << endl;
    }
    
    // 测试 delete
    db.del("test_key");
    if (!db.exists("test_key")) {
        cout << "Delete operation successful: test_key removed" << endl;
    } else {
        cout << "Delete operation failed!" << endl;
    }
    
    // 测试不存在的键
    if (db.get("non_existent_key", value)) {
        cout << "ERROR: Unexpected success for non-existent key" << endl;
    } else {
        cout << "Correctly handled non-existent key: key not found" << endl;
    }
}

// 测试批量操作
void testBatchOperations() {
    LevelDBManager& db = LevelDBManager::getInstance();
    auto batch = db.createBatch();
    
    // 添加批量操作
    batch.put("batch_key1", "batch_value1");
    batch.put("batch_key2", "batch_value2");
    batch.put("batch_key3", "batch_value3");
    batch.del("batch_key2"); // 删除其中一个键
    
    // 执行批量操作
    batch.commit();
    
    // 验证结果
    string value;
    if (db.get("batch_key1", value)) {
        cout << "Batch put successful: batch_key1 = " << value << endl;
    }
    
    if (db.exists("batch_key2")) {
        cout << "ERROR: batch_key2 should have been deleted" << endl;
    } else {
        cout << "Batch delete successful: batch_key2 removed" << endl;
    }
    
    // 清理
    db.del("batch_key1");
    db.del("batch_key3");
}

// 测试迭代器功能
void testIterator() {
    LevelDBManager& db = LevelDBManager::getInstance();
    
    // 准备测试数据
    db.put("iter_key1", "value1");
    db.put("iter_key2", "value2");
    db.put("iter_key3", "value3");
    db.put("a_key", "value_a");
    db.put("z_key", "value_z");
    
    // 使用迭代器
    auto iter = db.createIterator();
    cout << "All keys in database:" << endl;
    for (; iter.valid(); iter.next()) {
        cout << "  " << iter.key() << " => " << iter.value() << endl;
    }
    
    // 测试 seek
    cout << "\nSeeking to 'iter_key2':" << endl;
    iter.seek("iter_key2");
    if (iter.valid()) {
        cout << "  Found: " << iter.key() << " => " << iter.value() << endl;
    }
    
    // 清理
    db.del("iter_key1");
    db.del("iter_key2");
    db.del("iter_key3");
    db.del("a_key");
    db.del("z_key");
}

// 测试范围查询
void testRangeQueries() {
    LevelDBManager& db = LevelDBManager::getInstance();
    
    // 准备测试数据
    for (int i = 1; i <= 10; i++) {
        db.put("range_key" + to_string(i), "value" + to_string(i));
    }
    
    // 范围查询 (key3-key8)
    cout << "Keys from range_key3 to range_key8:" << endl;
    db.rangeQuery("range_key3", "range_key9", 
        [](const string& key, const string& value) {
            cout << "  " << key << " => " << value << endl;
        });
    
    // 前缀查询
    cout << "\nKeys with prefix 'range_key':" << endl;
    db.prefixQuery("range_key", 
        [](const string& key, const string& value) {
            cout << "  " << key << " => " << value << endl;
        });
    
    // 清理
    for (int i = 1; i <= 10; i++) {
        db.del("range_key" + to_string(i));
    }
}

// 测试多线程访问
void testMultiThreadedAccess() {
    LevelDBManager& db = LevelDBManager::getInstance();
    const int num_threads = 4;
    const int ops_per_thread = 1000;
    vector<thread> threads;
    
    cout << "Starting " << num_threads << " threads with " 
         << ops_per_thread << " operations each..." << endl;
    
    auto worker = [&](int thread_id) {
        string prefix = "thread" + to_string(thread_id) + "_key";
        
        // 写入操作
        for (int i = 0; i < ops_per_thread; i++) {
            string key = prefix + to_string(i);
            string value = "value_" + to_string(i);
            db.put(key, value);
        }
        
        // 读取操作
        string value;
        for (int i = 0; i < ops_per_thread; i++) {
            string key = prefix + to_string(i);
            if (!db.get(key, value)) {
                cerr << "Thread " << thread_id << " failed to read key: " << key << endl;
            }
        }
        
        // 删除操作
        for (int i = 0; i < ops_per_thread; i++) {
            string key = prefix + to_string(i);
            db.del(key);
        }
    };
    
    // 启动线程
    auto start = high_resolution_clock::now();
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, i);
    }
    
    // 等待线程完成
    for (auto& t : threads) {
        t.join();
    }
    auto end = high_resolution_clock::now();
    
    auto duration = duration_cast<milliseconds>(end - start).count();
    long total_ops = num_threads * ops_per_thread * 3; // 每个线程：写、读、删各1000次
    
    cout << "Completed " << total_ops << " operations in " 
         << duration << "ms (" 
         << fixed << setprecision(2) 
         << (total_ops * 1000.0 / duration) << " ops/sec)" << endl;
}

// 写入性能测试
PerformanceResult testWritePerformance(int num_operations, int key_size, int value_size) {
    LevelDBManager& db = LevelDBManager::getInstance();
    vector<pair<string, string>> data;
    
    // 准备测试数据
    for (int i = 0; i < num_operations; i++) {
        string key = "perf_key_" + TestDataGenerator::randomString(key_size) + "_" + to_string(i);
        string value = "perf_value_" + TestDataGenerator::randomString(value_size);
        data.emplace_back(key, value);
    }
    
    // 执行写入测试
    auto start = high_resolution_clock::now();
    
    for (const auto& kv : data) {
        db.put(kv.first, kv.second);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start).count();
    
    // 清理测试数据
    for (const auto& kv : data) {
        db.del(kv.first);
    }
    
    return {num_operations, static_cast<long>(duration), 
            num_operations * 1000.0 / duration};
}

// 读取性能测试
PerformanceResult testReadPerformance(int num_operations, int key_size) {
    LevelDBManager& db = LevelDBManager::getInstance();
    vector<string> keys;
    
    // 准备测试数据
    for (int i = 0; i < num_operations; i++) {
        string key = "read_key_" + TestDataGenerator::randomString(key_size) + "_" + to_string(i);
        db.put(key, "test_value");
        keys.push_back(key);
    }
    
    // 执行读取测试
    auto start = high_resolution_clock::now();
    
    string value;
    for (const auto& key : keys) {
        if (!db.get(key, value)) {
            cerr << "Failed to read key: " << key << endl;
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start).count();
    
    // 清理测试数据
    for (const auto& key : keys) {
        db.del(key);
    }
    
    return {num_operations, static_cast<long>(duration), 
            num_operations * 1000.0 / duration};
}