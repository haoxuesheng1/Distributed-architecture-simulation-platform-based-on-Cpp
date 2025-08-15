// main.cpp
#include "test_terrain_storage.hpp"
#include <gtest/gtest.h>
#include <fstream>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // 运行所有测试
    int result = RUN_ALL_TESTS();
    
    // 生成性能报告
    std::cout << "\n===== 性能测试报告 =====" << std::endl;
    std::ifstream report("terrain_storage_test_report.txt");
    if (report.is_open()) {
        std::string line;
        while (std::getline(report, line)) {
            if (line.find("ms") != std::string::npos || 
                line.find("points") != std::string::npos) {
                std::cout << line << std::endl;
            }
        }
    } else {
        std::cerr << "无法打开测试报告文件" << std::endl;
    }
    
    return result;
}