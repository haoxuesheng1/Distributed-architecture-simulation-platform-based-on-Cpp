# LevelDB C++ 高性能封装模块

[![LevelDB Version](https://img.shields.io/badge/LevelDB-1.23%2B-brightgreen)](https://github.com/google/leveldb)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/w/cpp/17)

这是一个针对 Google LevelDB 的高性能 C++ 封装模块，提供简洁易用的 API 和卓越的性能表现。经过严格测试，该模块在百万级数据操作场景下仍能保持每秒 35万+ 的写入速度和 45万+ 的读取速度。

## 功能亮点

- ⚡ **卓越性能**：百万级操作秒级完成
- 🧵 **线程安全**：原生支持多线程并发访问
- 📦 **简洁 API**：提供符合现代 C++ 的接口设计
- 🔧 **开箱即用**：自动优化配置，无需复杂设置
- 📊 **全面监控**：内置性能统计和健康检查

## 性能基准

| 测试场景 | 数据量 | 耗时 | 吞吐量 |
|----------|--------|------|--------|
| **写入测试** | 1,000,000 | 2.8 秒 | 355K ops/sec |
| **读取测试** | 1,000,000 | 2.2 秒 | 458K ops/sec |
| **多线程测试** | 12,000 | 110 毫秒 | 109K ops/sec |

> 测试环境：AMD Ryzen 7 5800X, 32GB DDR4, 1TB NVMe SSD

## 快速开始

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libleveldb-dev

# CentOS/RHEL
sudo yum install epel-release
sudo yum install gcc-c++ cmake leveldb-devel
```

### 编译项目

```bash
git clone https://github.com/haoxuesheng1/Distributed-architecture-simulation-platform-based-on-Cpp.git
cd Distributed-architecture-simulation-platform-based-on-Cpp/levelDBmanager
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### 运行测试

```bash
./leveldb_test
```

## 核心 API 使用示例

### 基本操作

```cpp
#include "LevelDBManager.h"

// 初始化数据库
LevelDBManager::getInstance().initialize("./mydb");

// 写入数据
LevelDBManager::getInstance().put("username", "john_doe");

// 读取数据
std::string value;
if (LevelDBManager::getInstance().get("username", value)) {
    std::cout << "Username: " << value << std::endl;
}

// 删除数据
LevelDBManager::getInstance().del("username");
```

### 批量操作

```cpp
auto batch = LevelDBManager::getInstance().createBatch();

batch.put("user:1001:name", "Alice");
batch.put("user:1001:email", "alice@example.com");
batch.put("user:1002:name", "Bob");

batch.commit();
```

### 范围查询

```cpp
// 查询 user:1001 开头的所有键
LevelDBManager::getInstance().prefixQuery("user:1001", 
    [](const std::string& key, const std::string& value) {
        std::cout << key << " => " << value << std::endl;
    });
```

### 迭代器使用

```cpp
auto iter = LevelDBManager::getInstance().createIterator();
for (iter.seekToFirst(); iter.valid(); iter.next()) {
    std::cout << iter.key() << " => " << iter.value() << std::endl;
}
```

## 高级功能

### 性能监控

```cpp
// 获取数据库统计信息
std::cout << LevelDBManager::getInstance().getStats() << std::endl;

// 获取特定层级文件数
std::string level0_files = LevelDBManager::getInstance().getProperty(
    "leveldb.num-files-at-level0");
```

### 数据库维护

```cpp
// 执行全量压缩
LevelDBManager::getInstance().compactRange("", "");

// 关闭数据库
LevelDBManager::getInstance().shutdown();
```

## 性能优化建议

```cpp
leveldb::Options custom_options;
custom_options.create_if_missing = true;
custom_options.write_buffer_size = 128 * 1048576;  // 128MB 写缓冲区
custom_options.block_cache = leveldb::NewLRUCache(512 * 1048576);  // 512MB 缓存
custom_options.filter_policy = leveldb::NewBloomFilterPolicy(12);  // 12位布隆过滤器

LevelDBManager::getInstance().initialize("./optimized_db", &custom_options);
```

## 应用场景

1. **高速缓存系统**：替代 Redis/Memcached 的部分场景
2. **实时数据处理**：日志收集和实时分析
3. **会话存储**：Web 应用的用户会话管理
4. **配置存储**：应用程序的配置管理系统
5. **物联网(IoT)**：设备状态和时序数据存储


---

**让高性能存储变得简单** - 在您的下一个 C++ 项目中尝试这个 LevelDB 封装模块！
