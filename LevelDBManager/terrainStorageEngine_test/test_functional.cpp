// test_functional.cpp
#include "test_terrain_storage.hpp"

// 测试范围查询
TEST_F(TerrainStorageTest, RangeQuery) {
    // 创建测试数据
    std::vector<std::tuple<double, double, std::string>> test_data = {
        {116.402, 39.901, "point1"},
        {116.403, 39.902, "point2"},
        {116.404, 39.903, "point3"},
        {116.405, 39.904, "point4"},
        {116.500, 40.000, "point5"}  // 范围外点
    };
    
    terrain_store_->batchPut(test_data);
    
    // 执行范围查询
    std::vector<std::tuple<double, double, std::string>> results;
    auto callback = [&](double lon, double lat, const std::string& value) {
        results.emplace_back(lon, lat, value);
    };
    
    terrain_store_->rangeQuery(116.401, 39.900, 116.406, 39.905, callback);
    
    // 验证结果
    ASSERT_EQ(results.size(), 4);
    
    // 结果排序以便验证
    std::sort(results.begin(), results.end(), 
        [](const auto& a, const auto& b) {
            return std::get<2>(a) < std::get<2>(b);
        });
    
    EXPECT_EQ(std::get<2>(results[0]), "point1");
    EXPECT_EQ(std::get<2>(results[1]), "point2");
    EXPECT_EQ(std::get<2>(results[2]), "point3");
    EXPECT_EQ(std::get<2>(results[3]), "point4");
}

// 测试缓存功能
TEST_F(TerrainStorageTest, CacheFunctionality) {
    // 先存储一个点确保数据库不为空
    terrain_store_->put(116.405, 39.905, "test_value");
    
    // 获取网格ID
    std::string grid_id = terrain_store_->computeGridId(116.405, 39.905);
    
    // 确保缓存为空
    terrain_store_->clearCache();
    ASSERT_EQ(terrain_store_->getCacheSize(), 0);
    
    // 查询点 - 应触发缓存加载
    std::string value;
    terrain_store_->get(116.405, 39.905, value);
    
    // 验证缓存中有一个网格
    ASSERT_EQ(terrain_store_->getCacheSize(), 1);
    
    // 添加更多点到同一网格
    terrain_store_->put(116.4051, 39.9051, "cache1");
    terrain_store_->put(116.4052, 39.9052, "cache2");
    
    // 直接通过缓存获取
    bool found = terrain_store_->get(116.4051, 39.9051, value);
    ASSERT_TRUE(found);
    EXPECT_EQ(value, "cache1");
    
    // 预加载另一个网格
    std::string another_grid = "G_050_030";
    terrain_store_->preloadGrid(another_grid);
    ASSERT_EQ(terrain_store_->getCacheSize(), 2);
    
    // 从缓存中移除网格
    terrain_store_->evictGridFromCache(grid_id);
    ASSERT_EQ(terrain_store_->getCacheSize(), 1);
    
    // 清除所有缓存
    terrain_store_->clearCache();
    ASSERT_EQ(terrain_store_->getCacheSize(), 0);
}

// 测试网格边界查询
TEST_F(TerrainStorageTest, GridBoundaryQuery) {
    // 在两个网格边界附近创建点
    terrain_store_->put(116.40499, 39.90499, "grid1");  // 网格G_090_040
    terrain_store_->put(116.40501, 39.90501, "grid2");  // 网格G_091_041
    
    // 执行范围查询跨越两个网格
    std::vector<std::string> values;
    auto callback = [&](double lon, double lat, const std::string& value) {
        values.push_back(value);
    };
    
    terrain_store_->rangeQuery(116.40498, 39.90498, 116.40502, 39.90502, callback);
    
    // 验证找到两个点
    ASSERT_EQ(values.size(), 2);
    EXPECT_TRUE(std::find(values.begin(), values.end(), "grid1") != values.end());
    EXPECT_TRUE(std::find(values.begin(), values.end(), "grid2") != values.end());
}