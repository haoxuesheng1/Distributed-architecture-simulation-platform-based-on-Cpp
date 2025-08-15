// test_basic.cpp
#include "test_terrain_storage.hpp"

// 测试基本存储和检索
TEST_F(TerrainStorageTest, BasicPutAndGet) {
    // 存储一个点
    terrain_store_->put(116.405285, 39.904989, "43.5");
    
    // 检索同一个点
    std::string value;
    bool found = terrain_store_->get(116.405285, 39.904989, value);
    
    ASSERT_TRUE(found);
    EXPECT_EQ(value, "43.5");
    
    // 检索不存在的点
    found = terrain_store_->get(116.5, 40.0, value);
    EXPECT_FALSE(found);
}

// 测试边界点
TEST_F(TerrainStorageTest, BoundaryPoints) {
    // 存储边界点
    terrain_store_->put(116.0, 39.0, "boundary1");
    terrain_store_->put(117.5, 41.0, "boundary2");
    
    // 检索边界点
    std::string value;
    ASSERT_TRUE(terrain_store_->get(116.0, 39.0, value));
    EXPECT_EQ(value, "boundary1");
    
    ASSERT_TRUE(terrain_store_->get(117.5, 41.0, value));
    EXPECT_EQ(value, "boundary2");
    
    // 测试超出边界的点
    EXPECT_THROW(terrain_store_->put(115.9, 38.9, "out1"), TerrainStorageException);
    EXPECT_THROW(terrain_store_->put(117.6, 41.1, "out2"), TerrainStorageException);
}

// 测试网格ID计算
TEST_F(TerrainStorageTest, GridIdComputation) {
    // 已知点测试
    EXPECT_EQ(terrain_store_->computeGridId(116.405, 39.905), "G_090_040");
    
    // 边界点测试
    EXPECT_EQ(terrain_store_->computeGridId(116.0, 39.0), "G_000_000");
    EXPECT_EQ(terrain_store_->computeGridId(117.499, 40.999), "G_199_149");
}

// 测试批量操作
TEST_F(TerrainStorageTest, BatchOperations) {
    // 生成1000个随机点
    auto batch_data = generateBatchData(1000);
    
    // 批量存储
    auto duration = measureTime("批量存储1000点", [&]{
        terrain_store_->batchPut(batch_data);
    });
    
    // 随机抽样验证
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, batch_data.size() - 1);
    
    for (int i = 0; i < 10; i++) {
        size_t idx = dist(gen);
        const auto& point = batch_data[idx];
        double lon = std::get<0>(point);
        double lat = std::get<1>(point);
        const std::string& expected = std::get<2>(point);
        
        std::string value;
        bool found = terrain_store_->get(lon, lat, value);
        
        ASSERT_TRUE(found) << "点(" << lon << ", " << lat << ") 未找到";
        EXPECT_EQ(value, expected);
    }
}