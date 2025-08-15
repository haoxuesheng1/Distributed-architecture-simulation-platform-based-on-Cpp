aux_source_directory(./ SRC_LIST)

# 添加源文件
add_executable(TerrainStorageEngineTest ${SRC_LIST})

# 链接库（GTest::gtest 是核心库，GTest::gtest_main 包含默认main函数）
target_link_libraries(TerrainStorageEngineTest
    PRIVATE 
        GTest::gtest 
        GTest::gtest_main  # 如果你没自己写main函数，保留这个
        leveldb 
        Threads::Threads  # 更规范的线程库链接方式
)