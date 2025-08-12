
/*--------此代码为测试文件代码，你也可以自行编辑测试代码--------*/

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cassert>
#include "dbconnectionpool.hpp" // 实现的连接池头文件

using namespace std;
using namespace std::chrono;

// 测试用例计数器
int tests_passed = 0;
int tests_failed = 0;

// 测试断言宏
#define TEST(condition) \
    do { \
        if (condition) { \
            cout << "\033[32m[PASS]\033[0m " << __func__ << endl; \
            tests_passed++; \
        } else { \
            cout << "\033[31m[FAIL]\033[0m " << __func__ << " (" << __FILE__ << ":" << __LINE__ << ")" << endl; \
            tests_failed++; \
        } \
    } while (0)

// 模拟数据库连接 - 用于单元测试
class MockDBConnection : public DBConn {
public:
    MockDBConnection() : connected(false), ping_success(true) {}
    
    bool connect() override {
        connected = true;
        return true;
    }
    
    bool ping() override {
        return ping_success && connected;
    }
    
    void reset() override {
        // 模拟重置操作
    }
    
    void close() override {
        connected = false;
    }
    
    // 测试辅助函数
    void setPingSuccess(bool success) { ping_success = success; }
    bool isConnected() const { return connected; }

private:
    bool connected;
    bool ping_success;
};

// ==================== 单元测试 ====================

void test_connection_creation() {
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        return make_shared<MockDBConnection>();
    };
    
    DBConnectionPool pool(5, factory);
    auto conn = pool.getConnection();
    TEST(conn != nullptr);
}

void test_connection_reuse() {
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        return make_shared<MockDBConnection>();
    };
    
    DBConnectionPool pool(2, factory);
    
    DBConn* conn1_ptr = nullptr;
    {
        auto conn1 = pool.getConnection();
        conn1_ptr = conn1.get();
    }
    
    auto conn2 = pool.getConnection();
    TEST(conn2.get() == conn1_ptr); // 应该是同一个连接
}

void test_max_connections() {
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        return make_shared<MockDBConnection>();
    };
    
    DBConnectionPool pool(2, factory);
    
    vector<DBConnectionPool::ConnectionPtr> connections;
    connections.push_back(pool.getConnection());
    connections.push_back(pool.getConnection());
    
    // 尝试获取第三个连接应该返回 nullptr
    auto conn3 = pool.getConnection();
    TEST(conn3 == nullptr);
}

void test_invalid_connection_replacement() {
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        auto conn = make_shared<MockDBConnection>();
        conn->setPingSuccess(false); // 模拟无效连接
        return conn;
    };
    
    DBConnectionPool pool(2, factory);
    
    // 获取的连接应该是无效的，但连接池应该创建新连接
    auto conn1 = pool.getConnection();
    TEST(conn1 != nullptr);
    
    // 连接池应该尝试创建另一个连接
    auto conn2 = pool.getConnection();
    TEST(conn2 != nullptr);
}

// ==================== MySQL 集成测试 ====================

void test_mysql_basic_operations() {
    // MySQL连接工厂
    auto mysql_factory = []() -> DBConnectionPool::ConnectionPtr {
        return make_shared<MySQLConnection>("127.0.0.1", 3306, 
                                           "root", "123456", "test_db");
    };
    
    DBConnectionPool mysql_pool(5, mysql_factory);
    
    // 获取连接
    auto conn = mysql_pool.getConnection();
    TEST(conn != nullptr);
    
    MYSQL* mysql_conn = dynamic_cast<MySQLConnection*>(conn.get())->getRawConnection();
    
    // 1. 创建测试表
    const char* create_table_sql = 
        "CREATE TABLE IF NOT EXISTS connection_pool_test ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "name VARCHAR(50) NOT NULL, "
        "value INT NOT NULL)";
    
    if (mysql_query(mysql_conn, create_table_sql)) {
        cerr << "创建表失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    
    // 2. 插入测试数据
    const char* insert_sql = 
        "INSERT INTO connection_pool_test (name, value) VALUES "
        "('test1', 100), "
        "('test2', 200), "
        "('test3', 300)";
    
    if (mysql_query(mysql_conn, insert_sql)) {
        cerr << "插入数据失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    
    // 验证插入的行数
    TEST(mysql_affected_rows(mysql_conn) == 3);
    
    // 3. 查询数据
    if (mysql_query(mysql_conn, "SELECT id, name, value FROM connection_pool_test")) {
        cerr << "查询失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(mysql_conn);
    TEST(result != nullptr);
    
    // 验证结果行数
    int row_count = mysql_num_rows(result);
    TEST(row_count == 3);
    
    // 验证具体数据
    MYSQL_ROW row;
    int found_test2 = 0;
    while ((row = mysql_fetch_row(result))) {
        if (string(row[1]) == "test2") {
            found_test2++;
            TEST(atoi(row[2]) == 200);
        }
    }
    TEST(found_test2 == 1);
    
    mysql_free_result(result);
    
    // 4. 更新数据
    const char* update_sql = "UPDATE connection_pool_test SET value = 250 WHERE name = 'test2'";
    if (mysql_query(mysql_conn, update_sql)) {
        cerr << "更新失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    TEST(mysql_affected_rows(mysql_conn) == 1);
    
    // 5. 验证更新
    if (mysql_query(mysql_conn, "SELECT value FROM connection_pool_test WHERE name = 'test2'")) {
        cerr << "查询失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    
    result = mysql_store_result(mysql_conn);
    TEST(result != nullptr);
    row = mysql_fetch_row(result);
    TEST(row != nullptr);
    TEST(atoi(row[0]) == 250);
    mysql_free_result(result);
    
    // 6. 删除数据
    const char* delete_sql = "DELETE FROM connection_pool_test WHERE name = 'test3'";
    if (mysql_query(mysql_conn, delete_sql)) {
        cerr << "删除失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
        return;
    }
    TEST(mysql_affected_rows(mysql_conn) == 1);
    
    // 7. 清理：删除测试表
    if (mysql_query(mysql_conn, "DROP TABLE connection_pool_test")) {
        cerr << "删除表失败: " << mysql_error(mysql_conn) << endl;
        TEST(false);
    } else {
        TEST(true);
    }
}

// ==================== 性能测试 ====================

void performance_test(int thread_count, int operations_per_thread) {
    // MySQL连接工厂
    auto mysql_factory = []() -> DBConnectionPool::ConnectionPtr {
        return make_shared<MySQLConnection>("127.0.0.1", 3306, 
                                           "root", "123456", "test_db");
    };
    
    DBConnectionPool pool(20, mysql_factory);
    
    vector<thread> threads;
    atomic<int> completed_operations(0);
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&, i] {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> sleep_dist(1, 10);
            
            for (int j = 0; j < operations_per_thread; j++) {
                try {
                    auto conn = pool.getConnection();
                    if (conn) {
                        // 模拟数据库操作
                        this_thread::sleep_for(milliseconds(sleep_dist(gen)));
                        completed_operations++;
                    }
                } catch (const exception& e) {
                    cerr << "Thread " << i << " error: " << e.what() << endl;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    cout << "性能测试结果 (" << thread_count << "线程, " 
         << operations_per_thread << "操作/线程):" << endl;
    cout << "  总操作数: " << completed_operations << endl;
    cout << "  总耗时: " << duration.count() << "ms" << endl;
    cout << "  吞吐量: " 
         << (completed_operations * 1000.0 / duration.count()) 
         << " ops/秒" << endl;
}

// ==================== 异常测试 ====================

void test_connection_timeout() {
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        // 模拟慢连接创建
        this_thread::sleep_for(milliseconds(100));
        return make_shared<MockDBConnection>();
    };
    
    // 设置连接池参数：最大连接数1，最大空闲时间100ms，连接超时50ms
    DBConnectionPool pool(1, factory, 100ms, 50ms);
    
    // 获取第一个连接
    auto conn1 = pool.getConnection();
    TEST(conn1 != nullptr);
    
    // 尝试获取第二个连接（应该超时，返回nullptr）
    auto conn2 = pool.getConnection();
    TEST(conn2 == nullptr);
}

// ==================== 主测试函数 ====================

int main() {
    cout << "=============================" << endl;
    cout << "开始MySQL连接池测试" << endl;
    cout << "=============================" << endl;
    
    // 单元测试
    cout << "\n[单元测试]" << endl;
    test_connection_creation();
    test_connection_reuse();
    test_max_connections();
    test_invalid_connection_replacement();
    test_connection_timeout();
    
    // 集成测试
    cout << "\n[集成测试]" << endl;
    test_mysql_basic_operations();
    
    // 性能测试
    cout << "\n[性能测试]" << endl;
    performance_test(4, 100);  // 4线程，每线程100操作
    performance_test(8, 200);  // 8线程，每线程200操作
    
    // 测试总结
    cout << "\n=============================" << endl;
    cout << "测试总结:" << endl;
    cout << "  通过: " << tests_passed << endl;
    cout << "  失败: " << tests_failed << endl;
    cout << "  总计: " << (tests_passed + tests_failed) << endl;
    cout << "=============================" << endl;
    
    return tests_failed > 0 ? 1 : 0;
}