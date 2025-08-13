# MySQL 连接池 (C++17)

一个高性能、线程安全的 MySQL 连接池实现，专为现代 C++ (C++17及以上) 设计。

![测试状态](https://img.shields.io/badge/tests-19%2F19%20passed-brightgreen)
![C++标准](https://img.shields.io/badge/C%2B%2B-17%2B-blue)

## 功能特性

- ✅ **连接复用** - 减少频繁创建/销毁连接的开销
- ⚡ **高性能** - 支持 1000+ QPS 并发查询
- 🛡️ **线程安全** - 完整的多线程支持
- ⏱️ **智能连接管理** - 自动验证、重置和回收连接
- 🔧 **可配置参数** - 最大连接数、空闲超时等
- 🧪 **全面测试覆盖** - 包含单元测试、集成测试和性能测试
- 🔄 **连接健康检查** - 自动剔除无效连接

## 依赖要求

- C++17 或更高版本编译器
- MySQL 客户端库 (libmysqlclient-dev)
- CMake 3.10+

## 安装 & 使用

### 1. 克隆仓库

```bash
git clone https://github.com/haoxuesheng1/Distributed-architecture-simulation-platform-based-on-Cpp.git
cd DBConnectionPool
```

### 2. 构建项目

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### 3. 运行测试

```bash
./tests/connection_pool_tests
```

### 4. 集成到项目

```cmake
# 在你的 CMakeLists.txt 中添加
add_subdirectory(path/to/mysql-connection-pool)
target_link_libraries(your_target PRIVATE DBConnectionPool)
```

## 快速开始

```cpp
#include "dbconnectionpool.hpp"

int main() {
    // 创建连接工厂
    auto factory = []() -> DBConnectionPool::ConnectionPtr {
        return std::make_shared<MySQLConnection>(
            "127.0.0.1", 3306, 
            "your_username", "your_password", 
            "your_database"
        );
    };

    // 初始化连接池 (最大10连接，空闲超时5分钟)
    DBConnectionPool pool(10, factory, std::chrono::minutes(5));
    
    // 获取连接
    auto conn = pool.getConnection();
    if (!conn) {
        std::cerr << "获取数据库连接失败!" << std::endl;
        return 1;
    }

    // 执行查询
    MYSQL* mysql_conn = dynamic_cast<MySQLConnection*>(conn.get())->getRawConnection();
    if (mysql_query(mysql_conn, "SELECT * FROM your_table")) {
        std::cerr << "查询失败: " << mysql_error(mysql_conn) << std::endl;
    } else {
        // 处理结果集...
    }

    // 连接自动归还 (RAII)
    return 0;
}
```

## 配置选项

| 参数 | 默认值 | 描述 |
|------|--------|------|
| `max_connections` | - | 最大连接数 (必需) |
| `max_idle_time` | 10分钟 | 连接最大空闲时间 |
| `connection_timeout` | 5秒 | 获取连接超时时长 |
| `cleanup_interval` | 30秒 | 空闲连接清理间隔 |

## 性能测试结果

| 线程数 | 操作/线程 | 吞吐量 (ops/秒) |
|--------|------------|------------------|
| 4      | 100        | 565.77          |
| 8      | 200        | 1161.1          |
| 16     | 500        | 待测试          |

## 测试覆盖率

```
=============================
测试总结:
  通过: 19
  失败: 0
  总计: 19
=============================
```

包含测试：
- 连接创建与复用
- 最大连接数限制
- 无效连接处理
- 连接超时控制
- MySQL CRUD 操作集成测试

## 项目结构

```
mysql-connection-pool/
├── include/
│   └── dbconnectionpool.hpp  # 主头文件
├── src/
│   └── dbconnectionpool.cpp  # 实现文件 （如果需要）
├── tests/
│   ├── unit_tests.cpp        # 单元测试
│   └── integration_tests.cpp # 集成测试
├── CMakeLists.txt（建议配置）
└── README.md
```

---

**提示**：使用前请修改测试代码中的数据库连接参数（127.0.0.1:3306, root/123456）为您实际的数据库配置。
