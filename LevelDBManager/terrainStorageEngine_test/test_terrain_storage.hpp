#ifndef TEST_TERRAIN_STORAGE_H
#define TEST_TERRAIN_STORAGE_H

#include "terrainStorageEngine.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <random>
#include <chrono>
#include <fstream>
#include <type_traits>

namespace fs = std::filesystem;

// 测试固件类
class TerrainStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时数据库目录
        db_path_ = fs::temp_directory_path() / "terrain_db_test";
        fs::create_directory(db_path_);
        
        // 初始化LevelDB管理器
        LevelDBManager& db_manager = LevelDBManager::getInstance();
        db_manager.initialize(db_path_.string());
        
        // 创建地形存储引擎 (北京地区)
        terrain_store_ = std::make_unique<TerrainStorageEngine>(
            db_manager,
            116.0, 39.0,   // 最小经度, 最小纬度
            117.5, 41.0,   // 最大经度, 最大纬度
            0.01,          // 网格大小(约1km)
            500            // 缓存500个网格
        );
        
        // 打开测试报告文件
        report_file_.open("terrain_storage_test_report.txt", std::ios::app);
    }
    
    void TearDown() override {
        // 关闭报告文件
        if (report_file_.is_open()) {
            report_file_.close();
        }
        
        // 关闭数据库
        LevelDBManager::getInstance().shutdown();
        
        // 删除临时目录
        fs::remove_all(db_path_);
        
        // 释放存储引擎
        terrain_store_.reset();
    }
    
    // 生成随机地形点
    std::tuple<double, double, std::string> generateRandomPoint() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<double> lon_dist(116.0, 117.5);
        std::uniform_real_distribution<double> lat_dist(39.0, 41.0);
        
        double lon = lon_dist(gen);
        double lat = lat_dist(gen);
        double elevation = std::uniform_real_distribution<double>(0.0, 2000.0)(gen);
        
        return std::make_tuple(lon, lat, std::to_string(elevation));
    }
    
    // 生成批量数据
    std::vector<std::tuple<double, double, std::string>> generateBatchData(size_t count) {
        std::vector<std::tuple<double, double, std::string>> data;
        data.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            data.push_back(generateRandomPoint());
        }
        
        return data;
    }
    
    // 测量函数执行时间（处理返回void的函数）
    template<typename Func, typename... Args>
    auto measureTime(const std::string& label, Func&& func, Args&&... args) 
        -> std::chrono::milliseconds {
        
        auto start = std::chrono::high_resolution_clock::now();
        std::forward<Func>(func)(std::forward<Args>(args)...);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // 将结果写入报告文件和控制台
        std::string output = label + " 耗时: " + std::to_string(duration.count()) + " ms";
        std::cout << output << std::endl;
        
        if (report_file_.is_open()) {
            report_file_ << output << std::endl;
        }
        
        return duration;
    }
    
    // 成员变量
    fs::path db_path_;
    std::unique_ptr<TerrainStorageEngine> terrain_store_;
    std::ofstream report_file_;
};

#endif // TEST_TERRAIN_STORAGE_H