#ifndef LEVELDBMANAGER_H
#define LEVELDBMANAGER_H

#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <functional>

// LevelDB 管理器异常类
class LevelDBException : public std::runtime_error {
public:
    LevelDBException(const std::string& msg, const leveldb::Status& status)
        : std::runtime_error(msg + ": " + status.ToString()) {}
};

// LevelDB 管理器类 - 单例模式
class LevelDBManager {
public:
    // 删除拷贝构造函数和赋值运算符
    LevelDBManager(const LevelDBManager&) = delete;
    LevelDBManager& operator=(const LevelDBManager&) = delete;
    
    /**
     * @brief 获取 LevelDB 管理器单例
     * @return LevelDBManager 实例
     */
    static LevelDBManager& getInstance() {
        static LevelDBManager instance;
        return instance;
    }
    
    /**
     * @brief 初始化 LevelDB 数据库
     * @param db_path 数据库路径
     * @param options 数据库选项 (可选)
     * 
     * 如果未提供 options，将使用优化过的默认选项：
     * - 创建缺失的数据库
     * - 100MB 块缓存
     * - 10位布隆过滤器
     * - 64MB 写缓冲区
     */
    void initialize(const std::string& db_path, 
                   const leveldb::Options* options = nullptr) {
        std::lock_guard<std::mutex> lock(init_mutex_);
        
        if (db_) {
            throw LevelDBException("LevelDB already initialized", leveldb::Status::OK());
        }
        
        leveldb::Options opts;
        if (options) {
            opts = *options;
        } else {
            // 设置优化的默认选项
            opts.create_if_missing = true;
            opts.block_cache = leveldb::NewLRUCache(100 * 1048576); // 100MB 缓存
            opts.filter_policy = leveldb::NewBloomFilterPolicy(10);  // 10位布隆过滤器
            opts.write_buffer_size = 64 * 1048576; // 64MB 写缓冲区
        }
        
        leveldb::DB* db_ptr = nullptr;
        leveldb::Status status = leveldb::DB::Open(opts, db_path, &db_ptr);
        
        if (!status.ok()) {
            throw LevelDBException("Failed to open LevelDB database", status);
        }
        
        db_.reset(db_ptr);
        db_path_ = db_path;
        initialized_ = true;
    }
    
    /**
     * @brief 关闭数据库并释放资源
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (db_) {
            db_.reset();
            initialized_ = false;
        }
    }
    
    /**
     * @brief 检查数据库是否已初始化
     * @return 是否已初始化
     */
    bool isInitialized() const {
        return initialized_;
    }
    
    /**
     * @brief 获取数据库路径
     * @return 数据库路径
     */
    std::string getDBPath() const {
        return db_path_;
    }
    
    /**
     * @brief 获取原始数据库对象指针
     * @return leveldb::DB 指针
     * 
     * 注意：使用前必须确保数据库已初始化
     */
    leveldb::DB* getDB() const {
        if (!initialized_ || !db_) {
            throw LevelDBException("LevelDB not initialized", leveldb::Status::IOError("Not initialized"));
        }
        return db_.get();
    }
    
    // 基本操作封装
    
    /**
     * @brief 写入键值对
     * @param key 键
     * @param value 值
     * @param sync 是否同步写入 (默认 false)
     */
    void put(const std::string& key, const std::string& value, bool sync = false) {
        leveldb::WriteOptions write_options;
        write_options.sync = sync;
        
        leveldb::Status status = db_->Put(write_options, key, value);
        if (!status.ok()) {
            throw LevelDBException("Put operation failed", status);
        }
    }
    
    /**
     * @brief 读取键值对
     * @param key 键
     * @param value 存储读取结果的字符串引用
     * @return 是否成功找到键值
     */
    bool get(const std::string& key, std::string& value) {
        leveldb::ReadOptions read_options;
        leveldb::Status status = db_->Get(read_options, key, &value);
        
        if (status.IsNotFound()) {
            return false;
        }
        else if (!status.ok()) {
            throw LevelDBException("Get operation failed", status);
        }
        
        return true;
    }
    
    /**
     * @brief 删除键值对
     * @param key 键
     * @param sync 是否同步删除 (默认 false)
     */
    void del(const std::string& key, bool sync = false) {
        leveldb::WriteOptions write_options;
        write_options.sync = sync;
        
        leveldb::Status status = db_->Delete(write_options, key);
        if (!status.ok() && !status.IsNotFound()) {
            throw LevelDBException("Delete operation failed", status);
        }
    }
    
    /**
     * @brief 检查键是否存在
     * @param key 键
     * @return 是否存在
     */
    bool exists(const std::string& key) {
        std::string value;
        leveldb::ReadOptions read_options;
        leveldb::Status status = db_->Get(read_options, key, &value);
        
        if (status.IsNotFound()) {
            return false;
        }
        else if (!status.ok()) {
            throw LevelDBException("Exists check failed", status);
        }
        
        return true;
    }
    
    // 批量操作接口
    
    /**
     * @brief 批量写入操作类
     */
    class BatchWriter {
    public:
        BatchWriter() = default;
        
        /**
         * @brief 添加写入操作
         * @param key 键
         * @param value 值
         */
        void put(const std::string& key, const std::string& value) {
            batch_.Put(key, value);
        }
        
        /**
         * @brief 添加删除操作
         * @param key 键
         */
        void del(const std::string& key) {
            batch_.Delete(key);
        }
        
        /**
         * @brief 执行批量操作
         * @param sync 是否同步写入 (默认 false)
         */
        void commit(bool sync = false) {
            leveldb::WriteOptions write_options;
            write_options.sync = sync;
            
            leveldb::Status status = LevelDBManager::getInstance().getDB()->Write(write_options, &batch_);
            if (!status.ok()) {
                throw LevelDBException("Batch commit failed", status);
            }
            
            batch_.Clear();
        }
        
        /**
         * @brief 清除所有待处理操作
         */
        void clear() {
            batch_.Clear();
        }
        
    private:
        leveldb::WriteBatch batch_;
    };
    
    /**
     * @brief 创建批量写入器
     * @return BatchWriter 实例
     */
    BatchWriter createBatch() {
        return BatchWriter();
    }
    
    // 迭代器接口
    
    /**
     * @brief 键值对迭代器
     */
    class Iterator {
    public:
        explicit Iterator(leveldb::DB* db) 
            : db_(db), it_(db->NewIterator(leveldb::ReadOptions())) {
            it_->SeekToFirst();
        }
        
        ~Iterator() {
            delete it_;
        }
        
        /**
         * @brief 检查迭代器是否有效
         * @return 是否有效
         */
        bool valid() const {
            return it_->Valid();
        }
        
        /**
         * @brief 移动到下一个键值对
         */
        void next() {
            it_->Next();
        }
        
        /**
         * @brief 获取当前键
         * @return 键
         */
        std::string key() const {
            return it_->key().ToString();
        }
        
        /**
         * @brief 获取当前值
         * @return 值
         */
        std::string value() const {
            return it_->value().ToString();
        }
        
        /**
         * @brief 移动到指定键
         * @param key 要查找的键
         */
        void seek(const std::string& key) {
            it_->Seek(key);
        }
        
        /**
         * @brief 移动到第一个键
         */
        void seekToFirst() {
            it_->SeekToFirst();
        }
        
        /**
         * @brief 移动到最后一个键
         */
        void seekToLast() {
            it_->SeekToLast();
        }
        
    private:
        leveldb::DB* db_;
        leveldb::Iterator* it_;
    };
    
    /**
     * @brief 创建迭代器
     * @return Iterator 实例
     */
    Iterator createIterator() {
        return Iterator(getDB());
    }
    
    // 范围查询
    
    /**
     * @brief 遍历指定范围内的键值对
     * @param start_key 起始键 (包含)
     * @param end_key 结束键 (不包含)
     * @param callback 处理每个键值对的回调函数
     */
    void rangeQuery(const std::string& start_key, 
                   const std::string& end_key,
                   std::function<void(const std::string&, const std::string&)> callback) {
        leveldb::ReadOptions read_options;
        std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options));
        
        for (it->Seek(start_key); it->Valid(); it->Next()) {
            std::string key = it->key().ToString();
            if (!end_key.empty() && key >= end_key) {
                break;
            }
            
            callback(key, it->value().ToString());
        }
        
        if (!it->status().ok()) {
            throw LevelDBException("Range query failed", it->status());
        }
    }
    
    // 前缀查询
    
    /**
     * @brief 遍历具有指定前缀的键值对
     * @param prefix 键前缀
     * @param callback 处理每个键值对的回调函数
     */
    void prefixQuery(const std::string& prefix,
                    std::function<void(const std::string&, const std::string&)> callback) {
        std::string end_key = prefix;
        if (!end_key.empty()) {
            // 找到前缀结束位置
            char last_char = end_key.back();
            end_key.back() = static_cast<char>(last_char + 1);
        }
        
        rangeQuery(prefix, end_key, callback);
    }
    
    // 性能统计
    
    /**
     * @brief 获取性能统计信息
     * @return 性能统计字符串
     */
    std::string getStats() {
        std::string stats;
        if (db_->GetProperty("leveldb.stats", &stats)) {
            return stats;
        }
        return "Statistics not available";
    }
    
    /**
     * @brief 压缩键范围
     * @param start_key 起始键
     * @param end_key 结束键
     */
    void compactRange(const std::string& start_key, const std::string& end_key) {
        leveldb::Slice start(start_key);
        leveldb::Slice end(end_key);
        db_->CompactRange(&start, &end);
    }

private:
    LevelDBManager() 
        : initialized_(false) {}
    
    ~LevelDBManager() {
        shutdown();
    }
    
    std::mutex init_mutex_;                  // 初始化互斥锁
    std::unique_ptr<leveldb::DB> db_;        // LevelDB 数据库实例
    std::string db_path_;                    // 数据库路径
    std::atomic<bool> initialized_{false};   // 初始化标志
};

#endif