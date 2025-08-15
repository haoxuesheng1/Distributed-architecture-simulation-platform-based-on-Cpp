// test_performance.cpp
#include "test_terrain_storage.hpp"

// 测试大规模数据插入性能
TEST_F(TerrainStorageTest, MassiveInsertPerformance) {
    // 生成10万个点
    auto data = generateBatchData(100000);
    
    // 测量批量存储时间
    auto duration = measureTime("插入100,000点", [&]{
        terrain_store_->batchPut(data);
    });
    
    // 验证数据量
    size_t count = 0;
    terrain_store_->rangeQuery(116.0, 39.0, 117.5, 41.0, 
        [&](double, double, const std::string&) {
            count++;
        });
    
    EXPECT_GE(count, 100000);
    
    // 记录插入速率
    double rate = 100000.0 / (duration.count() / 1000.0);
    std::string rate_str = "插入速率: " + std::to_string(static_cast<int>(rate)) + " 点/秒";
    std::cout << rate_str << std::endl;
    if (report_file_.is_open()) {
        report_file_ << rate_str << std::endl;
    }
}

// 测试缓存命中性能
TEST_F(TerrainStorageTest, CacheHitPerformance) {
    // 生成1万个点
    auto data = generateBatchData(10000);
    terrain_store_->batchPut(data);
    
    // 预热缓存 - 加载所有网格
    for (const auto& point : data) {
        double lon = std::get<0>(point);
        double lat = std::get<1>(point);
        std::string grid_id = terrain_store_->computeGridId(lon, lat);
        terrain_store_->preloadGrid(grid_id);
    }
    
    // 测试缓存命中查询
    auto cache_duration = measureTime("10,000次缓存命中查询", [&]{
        for (const auto& point : data) {
            double lon = std::get<0>(point);
            double lat = std::get<1>(point);
            std::string value;
            terrain_store_->get(lon, lat, value);
        }
    });
    
    // 清除缓存
    terrain_store_->clearCache();
    
    // 测试缓存未命中查询
    auto nocache_duration = measureTime("10,000次缓存未命中查询", [&]{
        for (const auto& point : data) {
            double lon = std::get<0>(point);
            double lat = std::get<1>(point);
            std::string value;
            terrain_store_->get(lon, lat, value);
        }
    });
    
    // 计算性能提升比例
    double improvement = (nocache_duration.count() - cache_duration.count()) / 
                         static_cast<double>(nocache_duration.count());
    
    std::string improvement_str = "缓存提升比例: " + 
        std::to_string(static_cast<int>(improvement * 100)) + "%";
    
    std::cout << improvement_str << std::endl;
    if (report_file_.is_open()) {
        report_file_ << improvement_str << std::endl;
    }
}

// 测试范围查询性能
TEST_F(TerrainStorageTest, RangeQueryPerformance) {
    // 生成10万个点
    auto data = generateBatchData(100000);
    terrain_store_->batchPut(data);
    
    // 测试小范围查询 (1km x 1km)
    int small_count = 0;
    auto small_duration = measureTime("小范围查询(1km x 1km)", [&]{
        terrain_store_->rangeQuery(116.40, 39.90, 116.41, 39.91, 
            [&](double, double, const std::string&) {
                small_count++;
            });
    });
    std::cout << "小范围查询点数: " << small_count << std::endl;
    
    // 测试中范围查询 (10km x 10km)
    int medium_count = 0;
    auto medium_duration = measureTime("中范围查询(10km x 10km)", [&]{
        terrain_store_->rangeQuery(116.40, 39.90, 116.50, 40.00, 
            [&](double, double, const std::string&) {
                medium_count++;
            });
    });
    std::cout << "中范围查询点数: " << medium_count << std::endl;
    
    // 测试大范围查询 (100km x 100km)
    int large_count = 0;
    auto large_duration = measureTime("大范围查询(100km x 100km)", [&]{
        terrain_store_->rangeQuery(116.0, 39.0, 117.0, 40.0, 
            [&](double, double, const std::string&) {
                large_count++;
            });
    });
    std::cout << "大范围查询点数: " << large_count << std::endl;
}

// 测试热点数据优化
TEST_F(TerrainStorageTest, HotspotOptimization) {
    // 生成10万个点
    auto data = generateBatchData(100000);
    terrain_store_->batchPut(data);
    
    // 定义热点区域 (天安门广场周边)
    const double hotspot_min_lon = 116.39;
    const double hotspot_min_lat = 39.90;
    const double hotspot_max_lon = 116.41;
    const double hotspot_max_lat = 39.92;
    
    // 预加载热点区域网格
    std::set<std::string> hotspot_grids;
    terrain_store_->rangeQuery(hotspot_min_lon, hotspot_min_lat, 
                             hotspot_max_lon, hotspot_max_lat,
                             [&](double lon, double lat, const std::string&) {
                                 hotspot_grids.insert(terrain_store_->computeGridId(lon, lat));
                             });
    
    for (const auto& grid_id : hotspot_grids) {
        terrain_store_->preloadGrid(grid_id);
    }
    
    // 测试热点区域查询性能
    int hotspot_count = 0;
    auto duration = measureTime("热点区域查询", [&]{
        terrain_store_->rangeQuery(hotspot_min_lon, hotspot_min_lat, 
                                hotspot_max_lon, hotspot_max_lat,
                                [&](double, double, const std::string&) {
                                    hotspot_count++;
                                });
    });
    
    std::cout << "热点区域点数: " << hotspot_count << std::endl;
    
    // 验证查询时间符合毫秒级要求
    EXPECT_LT(duration.count(), 50) << "热点区域查询时间超过50ms";
    
    if (duration.count() < 50) {
        std::string result = "热点区域查询: " + std::to_string(hotspot_count) + 
                             " 点, 耗时 " + std::to_string(duration.count()) + "ms";
        std::cout << result << std::endl;
        if (report_file_.is_open()) {
            report_file_ << result << std::endl;
        }
    }
}