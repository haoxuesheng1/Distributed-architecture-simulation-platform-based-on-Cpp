
/*--------此代码为MySQL连接池源代码，使用时包含该头文件即可，切记需要使用支持C++17及以上的编译器--------*/

#ifndef DBCONNECTIONPOOL_H
#define DBCONNECTIONPOOL_H

#include <chrono>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <map>
#include <list>
#include <stdexcept>
#include <atomic>
#include <mysql/mysql.h>

// DBConn.h - 抽象数据库连接接口
class DBConn {
public:
    virtual ~DBConn() = default;

    // 连接数据库
    virtual bool connect() = 0;
    
    // 检查连接是否有效
    virtual bool ping() = 0;
    
    // 重置连接状态（如回滚事务）
    virtual void reset() = 0;
    
    // 关闭连接
    virtual void close() = 0;
    
    // 设置最后使用时间点
    void setLastUsed(std::chrono::steady_clock::time_point t) { last_used = t; }
    
    // 获取空闲时间
    auto getIdleDuration() const {
        return std::chrono::steady_clock::now() - last_used;
    }

protected:
    std::chrono::steady_clock::time_point last_used;
};

// MySQLConnection类 - MySQL具体实现
class MySQLConnection : public DBConn {
public:
    /**
     * @brief 构造函数，初始化MySQL连接参数
     * @param host 数据库主机地址
     * @param port 数据库端口
     * @param user 用户名
     * @param pass 密码
     * @param db 数据库名称
     */
    MySQLConnection(const std::string& host, unsigned port, 
                   const std::string& user, const std::string& pass, 
                   const std::string& db)
        : host_(host), port_(port), user_(user), 
          pass_(pass), db_(db), conn_(nullptr) {}
    
    /**
     * @brief 析构函数，确保关闭连接
     */
    ~MySQLConnection() override { close(); }
    
    /**
     * @brief 连接到MySQL数据库
     * @return 连接是否成功
     */
    bool connect() override {
        if (conn_ = mysql_init(nullptr); !conn_) {
            return false;
        }
        
        // 设置连接选项
        unsigned int connect_timeout = 5; // 5秒连接超时
        mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
        
        // 尝试建立实际连接
        auto* conn = mysql_real_connect(conn_, host_.c_str(), user_.c_str(),
                                       pass_.c_str(), db_.c_str(), port_, 
                                       nullptr, 0);
        return conn != nullptr;
    }
    
    /**
     * @brief 检查连接是否仍然有效
     * @return 连接是否有效
     */
    bool ping() override {
        return conn_ ? mysql_ping(conn_) == 0 : false;
    }
    
    /**
     * @brief 重置连接状态（回滚事务并设置自动重连）
     */
    void reset() override {
        if (conn_) {
            // 1. 回滚任何未提交的事务
            mysql_rollback(conn_);
            
            // 2. 设置自动重连选项（兼容不同MySQL版本）
            #ifdef MYSQL_OPT_RECONNECT
                // 使用标准bool类型替代已弃用的my_bool
                bool reconnect = true;
                mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);
            #endif
            
            // 3. 重置连接状态标志
            #ifdef MYSQL_RESET_CONNECTION
                mysql_reset_connection(conn_);
            #endif
        }
    }
    
    /**
     * @brief 关闭数据库连接
     */
    void close() override {
        if (conn_) {
            mysql_close(conn_);
            mysql_thread_end(); // 清理线程相关资源
            conn_ = nullptr;
        }
    }
    
    /**
     * @brief 获取原始的 MySQL 连接对象
     * @return 原始 MYSQL* 连接指针
     */
    MYSQL* getRawConnection() const { return conn_; }

private:
    MYSQL* conn_ = nullptr;     // MySQL连接句柄
    std::string host_;           // 数据库主机地址
    std::string user_;           // 用户名
    std::string pass_;           // 密码
    std::string db_;             // 数据库名称
    unsigned port_ = 3306;       // 端口号
};

// DBConnectionPool类 - MySQL连接池实现
class DBConnectionPool {
public:
    // C++11特性：类型别名简化代码
    using ConnectionPtr = std::shared_ptr<DBConn>;
    using Factory = std::function<ConnectionPtr()>;
    
    // C++11特性：删除拷贝构造函数和赋值操作符
    DBConnectionPool(const DBConnectionPool&) = delete;
    DBConnectionPool& operator=(const DBConnectionPool&) = delete;
    
    /**
     * @brief 构造函数
     * @param max_conn 最大连接数
     * @param factory 连接工厂函数
     * @param max_idle 最大空闲时间（默认10分钟）
     * @param connection_timeout 获取连接超时时间（默认5秒）
     */
    DBConnectionPool(size_t max_conn, Factory factory, 
                    std::chrono::milliseconds max_idle = std::chrono::minutes(10),
                    std::chrono::milliseconds connection_timeout = std::chrono::seconds(5))
        : max_connections_(max_conn), 
          connection_factory_(std::move(factory)), // C++11移动语义
          max_idle_time_(max_idle),
          connection_timeout_(connection_timeout),
          cleaner_running_(true) {
        // 启动清理线程 (C++11 lambda表达式)
        cleaner_thread_ = std::thread([this] { cleanIdleConnections(); });
    }
    
    /**
     * @brief 析构函数，停止清理线程
     */
    ~DBConnectionPool() {
        cleaner_running_ = false;
        cleaner_cv_.notify_one();
        if (cleaner_thread_.joinable()) {
            cleaner_thread_.join();
        }
    }
    
    /**
     * @brief 获取数据库连接
     * @return 共享指针管理的数据库连接
     * 
     * 使用C++11智能指针与lambda自定义删除器
     */
    ConnectionPtr getConnection() {
        std::unique_lock<std::mutex> lock(pool_mutex_);
        
        // 1. 尝试从空闲队列获取有效连接
        while (!idle_connections_.empty()) {
            auto conn = idle_connections_.front();
            idle_connections_.pop_front();
            
            if (conn->ping()) { // 检查连接是否有效
                active_connections_.emplace(conn.get(), conn);
                
                // C++11特性：lambda捕获的自定义删除器
                return {conn.get(), [this](DBConn* c) { releaseConnection(c); }};
            }
        }
        
        // 2. 创建新连接（如果未达上限）
        if (active_connections_.size() + idle_connections_.size() < max_connections_) {
            if (auto conn = connection_factory_(); conn && conn->connect()) {
                // 添加到活动连接
                active_connections_.emplace(conn.get(), conn);
                
                // 设置自定义删除器，连接使用后自动归还
                return {conn.get(), [this](DBConn* c) { releaseConnection(c); }};
            }
        }
        
        // 3. 等待可用连接（带超时）
        if (pool_cv_.wait_for(lock, connection_timeout_, [this] {
            return !idle_connections_.empty() || 
                   active_connections_.size() + idle_connections_.size() < max_connections_;
        })) {
            // 递归尝试获取连接
            return getConnection();
        }
        
        // 超时，返回空指针
        return nullptr;
    }

private:
    /**
     * @brief 释放连接回连接池
     * @param raw_conn 原始连接指针
     */
    void releaseConnection(DBConn* raw_conn) {
        std::unique_lock<std::mutex> lock(pool_mutex_);
        auto it = active_connections_.find(raw_conn);
        
        if (it != active_connections_.end() && it->second.get() == raw_conn) {
            // 重置连接状态
            raw_conn->reset();
            
            // 记录最后使用时间
            raw_conn->setLastUsed(std::chrono::steady_clock::now());
            
            // 放回空闲队列
            idle_connections_.push_back(it->second);
            active_connections_.erase(it);
            
            // 通知等待线程
            pool_cv_.notify_one();
        }
    }
    
    /**
     * @brief 清理空闲时间过长的连接
     */
    void cleanIdleConnections() {
        while (cleaner_running_) {
            std::unique_lock<std::mutex> lock(cleaner_mutex_);
            // 使用条件变量定时唤醒 (C++11)
            cleaner_cv_.wait_for(lock, std::chrono::seconds(30), 
                [this] { return !cleaner_running_; });
            
            if (!cleaner_running_) break;
            
            std::unique_lock<std::mutex> pool_lock(pool_mutex_);
            
            // 清理空闲时间过长的连接
            auto it = idle_connections_.begin();
            while (it != idle_connections_.end()) {
                if ((*it)->getIdleDuration() > max_idle_time_) {
                    (*it)->close();
                    it = idle_connections_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    
    const size_t max_connections_;                       // 最大连接数
    Factory connection_factory_;                         // 连接创建工厂
    const std::chrono::milliseconds max_idle_time_;      // 最大空闲时间
    const std::chrono::milliseconds connection_timeout_; // 获取连接超时时间
    
    std::list<ConnectionPtr> idle_connections_;          // 空闲连接队列
    std::map<DBConn*, ConnectionPtr> active_connections_; // 活动连接映射
    
    std::mutex pool_mutex_;                              // 连接池互斥锁
    std::condition_variable pool_cv_;                    // 连接池条件变量
    
    std::thread cleaner_thread_;                         // 清理线程
    std::mutex cleaner_mutex_;                           // 清理线程互斥锁
    std::condition_variable cleaner_cv_;                 // 清理线程条件变量
    std::atomic<bool> cleaner_running_;                  // C++11原子布尔值
};

#endif