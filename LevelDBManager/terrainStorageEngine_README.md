# LevelDB 地形数据存储引擎优化

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![LevelDB](https://img.shields.io/badge/LevelDB-1.23-green.svg)

该项目是一个针对海量地形数据优化的高性能存储引擎，基于LevelDB构建。通过创新的网格空间分区索引和热点数据缓存策略，实现了亿级地形数据的毫秒级查询，特别适合地理空间数据密集型应用。

## 核心特性

- 🌐 **网格空间分区索引**：将地理空间划分为均匀网格，实现高效数据定位
- 🔥 **热点数据缓存**：LRU缓存策略自动保留高频访问数据
- ⚡ **毫秒级查询**：热点区域查询0ms响应
- 🚀 **高性能写入**：支持批量操作，68万点/秒的插入速率
- 🔍 **高效范围查询**：33,185点查询仅需46ms
- 🧩 **模块化设计**：易于集成到现有系统

## 性能指标

| 操作类型 | 数据量 | 耗时 | 速率 |
|----------|--------|------|------|
| 数据插入 | 100,000点 | 147ms | 680,272点/秒 |
| 缓存命中查询 | 10,000次 | 34ms | 294,117次/秒 |
| 小范围查询 | 1km² | 0ms | - |
| 中范围查询 | 100km² | 0ms | - |
| 大范围查询 | 100×100km | 46ms | - |
| 热点区域查询 | 14点 | 0ms | - |

## 快速开始

### 依赖项

- C++17 编译器
- LevelDB (1.23 或更高版本)
- Google Test (用于测试)

### 构建项目

```bash
# 克隆仓库
git clone https://github.com/haoxuesheng1/Distributed-architecture-simulation-platform-based-on-Cpp.git
cd Distributed-architecture-simulation-platform-based-on-Cpp/levelDBmanager

# 创建构建目录
mkdir build
cd build

# 生成构建系统
cmake ..

# 编译项目
make
```

### 运行测试

```bash
./terrainStorageEngine_test
```

### 使用示例

```cpp
#include "TerrainStorageEngine.hpp"

int main() {
    // 初始化LevelDB管理器
    LevelDBManager& db_manager = LevelDBManager::getInstance();
    db_manager.initialize("/path/to/terrain_db");
    
    // 创建地形存储引擎（北京地区）
    TerrainStorageEngine terrain_store(
        db_manager,
        116.0, 39.0,   // 最小经度, 最小纬度
        117.5, 41.0,   // 最大经度, 最大纬度
        0.01,           // 网格大小(约1km)
        500             // 缓存500个网格
    );
    
    // 存储地形点
    terrain_store.put(116.405285, 39.904989, "43.5m");
    
    // 查询地形点
    std::string elevation;
    if (terrain_store.get(116.405285, 39.904989, elevation)) {
        std::cout << "海拔高度: " << elevation << std::endl;
    }
    
    // 批量存储
    std::vector<std::tuple<double, double, std::string>> batch_data;
    batch_data.emplace_back(116.406, 39.905, "42.8m");
    batch_data.emplace_back(116.407, 39.906, "41.3m");
    terrain_store.batchPut(batch_data);
    
    // 范围查询
    terrain_store.rangeQuery(
        116.40, 39.90,   // 最小经度, 最小纬度
        116.41, 39.91,   // 最大经度, 最大纬度
        [](double lon, double lat, const std::string& value) {
            std::cout << "点(" << lon << "," << lat << "): " << value << std::endl;
        }
    );
    
    // 预加载热点区域
    terrain_store.preloadGrid("G_090_040");
    
    return 0;
}
```

## 架构设计

### 核心组件

1. **LevelDBManager**
   - LevelDB连接的单例管理器
   - 封装基本操作（Put/Get/Delete）
   - 提供批量操作和迭代器接口

2. **TerrainStorageEngine**
   - 地形数据存储引擎核心
   - 实现网格空间分区索引
   - 管理热点数据缓存
   - 提供高效范围查询

3. **GridLRUCache**
   - 网格数据LRU缓存实现
   - 自动淘汰最少使用的网格
   - 支持手动缓存管理

### 优化策略

1. **键值结构优化**
   ```
   [网格ID]|[经度]|[纬度]
   示例: G_090_040|116.4052850|39.9049890
   ```

2. **网格空间分区**
   - 地理空间划分为均匀网格
   - 每个网格独立存储和缓存
   - 查询时仅加载相关网格

3. **热点数据缓存**
   - LRU自动管理高频访问网格
   - 支持手动预加载热点区域
   - 缓存失效自动更新

## 应用场景

- 地理信息系统（GIS）
- 遥感数据处理平台
- 三维地形渲染引擎
- 智慧城市空间数据管理
- 军事仿真地形数据服务


## 致谢

- LevelDB 团队提供出色的键值存储引擎
- Google Test 提供强大的测试框架支持
- 国家自然科学基金提供研究支持

---

**让海量地形数据查询飞起来！** 🚀
