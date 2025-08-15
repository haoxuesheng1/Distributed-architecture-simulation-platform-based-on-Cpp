#ifndef TERRAINSTORAGEENGINE_HPP
#define TERRAINSTORAGEENGINE_HPP

#include "levelDBmanager.hpp"
#include <cmath>
#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>
#include <stdexcept>

// 地形数据存储引擎异常类
class TerrainStorageException : public std::runtime_error {
public:
    explicit TerrainStorageException(const std::string& msg)
        : std::runtime_error("TerrainStorageEngine: " + msg) {}
};

// 网格数据缓存项
struct GridCacheItem {
    std::string grid_id;
    std::unordered_map<std::string, std::string> data; // 键值对: <生成键, 地形数据>
};

// LRU缓存实现
class GridLRUCache {
public:
    explicit GridLRUCache(size_t capacity) 
        : capacity_(capacity > 0 ? capacity : 1000) {}

    // 获取缓存项
    std::shared_ptr<GridCacheItem> get(const std::string& grid_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto map_it = cache_map_.find(grid_id);
        if (map_it == cache_map_.end()) {
            return nullptr;
        }
        
        // 移动到访问列表前端
        access_list_.splice(access_list_.begin(), access_list_, map_it->second.second);
        return map_it->second.first;
    }

    // 添加缓存项
    void put(const std::string& grid_id, std::shared_ptr<GridCacheItem> item) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (cache_map_.find(grid_id) != cache_map_.end()) {
            // 已存在则更新
            access_list_.erase(cache_map_[grid_id].second);
        } else if (cache_map_.size() >= capacity_) {
            // 淘汰最久未使用的网格
            auto last = access_list_.back();
            cache_map_.erase(last);
            access_list_.pop_back();
        }
        
        // 添加新项到前端
        access_list_.push_front(grid_id);
        cache_map_[grid_id] = {item, access_list_.begin()};
    }

    // 移除缓存项
    void remove(const std::string& grid_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(grid_id);
        if (it != cache_map_.end()) {
            access_list_.erase(it->second.second);
            cache_map_.erase(it);
        }
    }

    // 清除所有缓存
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_map_.clear();
        access_list_.clear();
    }

    // 获取当前缓存大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_map_.size();
    }

private:
    size_t capacity_;
    std::list<std::string> access_list_;
    std::unordered_map<std::string, 
        std::pair<std::shared_ptr<GridCacheItem>, 
        std::list<std::string>::iterator>> cache_map_;
    mutable std::mutex mutex_;
};

// 地形数据存储引擎
class TerrainStorageEngine {
public:
    /**
     * @brief 构造函数
     * @param db_manager LevelDB管理器引用
     * @param min_lon 最小经度
     * @param min_lat 最小纬度
     * @param max_lon 最大经度
     * @param max_lat 最大纬度
     * @param grid_size 网格大小（单位：度）
     * @param cache_capacity 缓存容量（网格数量）
     */
    TerrainStorageEngine(LevelDBManager& db_manager,
                        double min_lon, double min_lat,
                        double max_lon, double max_lat,
                        double grid_size,
                        size_t cache_capacity = 1000)
        : db_manager_(db_manager),
          min_lon_(min_lon),
          min_lat_(min_lat),
          max_lon_(max_lon),
          max_lat_(max_lat),
          grid_size_(grid_size),
          cache_(cache_capacity) {
        
        if (grid_size_ <= 0.0) {
            throw TerrainStorageException("Grid size must be positive");
        }
        
        // 计算网格行列数
        grid_cols_ = static_cast<int>(std::ceil((max_lon - min_lon) / grid_size_));
        grid_rows_ = static_cast<int>(std::ceil((max_lat - min_lat) / grid_size_));
        
        // 固定网格ID宽度为3位
        id_width_ = 3;
    }
    
    /**
     * @brief 检查坐标是否在有效范围内
     * @param lon 经度
     * @param lat 纬度
     * @return 是否在范围内
     */
    bool isWithinBounds(double lon, double lat) const {
        return (lon >= min_lon_ && lon <= max_lon_ && 
                lat >= min_lat_ && lat <= max_lat_);
    }

    /**
     * @brief 存储地形数据点
     * @param lon 经度
     * @param lat 纬度
     * @param value 地形数据值
     * @param sync 是否同步写入(默认 false)
     */
    void put(double lon, double lat, const std::string& value, bool sync = false) {
        if (!isWithinBounds(lon, lat)) {
            throw TerrainStorageException("坐标超出范围: (" + 
                std::to_string(lon) + ", " + std::to_string(lat) + ")");
        }

        std::string grid_id = computeGridId(lon, lat);
        std::string key = generateKey(lon, lat, grid_id);
        
        // 更新缓存（如果存在）
        auto cache_item = cache_.get(grid_id);
        if (cache_item) {
            cache_item->data[key] = value;
        }
        
        // 写入数据库
        db_manager_.put(key, value, sync);
    }
    
    /**
     * @brief 获取地形数据点
     * @param lon 经度
     * @param lat 纬度
     * @param value [输出] 地形数据值
     * @return 是否成功找到键值
     */
    bool get(double lon, double lat, std::string& value) {
        if (!isWithinBounds(lon, lat)) {
            return false;
        }
        
        std::string grid_id = computeGridId(lon, lat);
        std::string key = generateKey(lon, lat, grid_id);
        
        // 首先尝试从缓存获取
        auto cache_item = cache_.get(grid_id);
        if (cache_item) {
            auto it = cache_item->data.find(key);
            if (it != cache_item->data.end()) {
                value = it->second;
                return true;
            }
        }
        
        // 缓存未命中，从数据库获取
        if (db_manager_.get(key, value)) {
            // 如果网格不在缓存中，则加载整个网格
            if (!cache_item) {
                loadGridToCache(grid_id);
            } else {
                // 如果网格在缓存中，但该点不在，则更新缓存
                cache_item->data[key] = value;
            }
            return true;
        } else {
            // 即使点不存在，也加载整个网格到缓存
            if (!cache_item) {
                loadGridToCache(grid_id);
            }
            return false;
        }
    }
    
    /**
     * @brief 批量存储地形数据点
     * @param data 地形数据向量<经度, 纬度, 值>
     */
    void batchPut(const std::vector<std::tuple<double, double, std::string>>& data) {
        auto batch = db_manager_.createBatch();
        
        for (const auto& item : data) {
            double lon = std::get<0>(item);
            double lat = std::get<1>(item);
            const std::string& value = std::get<2>(item);
            
            if (!isWithinBounds(lon, lat)) {
                throw TerrainStorageException("坐标超出范围: (" + 
                    std::to_string(lon) + ", " + std::to_string(lat) + ")");
            }

            std::string grid_id = computeGridId(lon, lat);
            std::string key = generateKey(lon, lat, grid_id);
            
            // 更新缓存（如果存在）
            auto cache_item = cache_.get(grid_id);
            if (cache_item) {
                cache_item->data[key] = value;
            }
            
            batch.put(key, value);
        }
        
        batch.commit();
    }
    
    /**
     * @brief 范围查询地形数据
     * @param min_lon 最小经度
     * @param min_lat 最小纬度
     * @param max_lon 最大经度
     * @param max_lat 最大纬度
     * @param callback 回调函数 (经度, 纬度, 值)
     */
    void rangeQuery(double min_lon, double min_lat,
                   double max_lon, double max_lat,
                   std::function<void(double, double, const std::string&)> callback) {
        // 计算覆盖的网格范围
        int start_col = std::max(0, lonToGridCol(min_lon));
        int end_col = std::min(grid_cols_ - 1, lonToGridCol(max_lon));
        int start_row = std::max(0, latToGridRow(min_lat));
        int end_row = std::min(grid_rows_ - 1, latToGridRow(max_lat));
        
        // 遍历所有覆盖的网格
        for (int row = start_row; row <= end_row; ++row) {
            for (int col = start_col; col <= end_col; ++col) {
                std::string grid_id = formatGridId(row, col);
                processGrid(grid_id, min_lon, min_lat, max_lon, max_lat, callback);
            }
        }
    }
    
    /**
     * @brief 预加载网格数据到缓存
     * @param grid_id 网格ID
     */
    void preloadGrid(const std::string& grid_id) {
        loadGridToCache(grid_id);
    }
    
    /**
     * @brief 从缓存中移除网格
     * @param grid_id 网格ID
     */
    void evictGridFromCache(const std::string& grid_id) {
        cache_.remove(grid_id);
    }
    
    /**
     * @brief 清除所有缓存
     */
    void clearCache() {
        cache_.clear();
    }
    
    /**
     * @brief 获取当前缓存大小
     * @return 缓存中的网格数量
     */
    size_t getCacheSize() const {
        return cache_.size();
    }
    
    /**
     * @brief 计算网格ID
     * @param lon 经度
     * @param lat 纬度
     * @return 网格ID字符串
     */
    std::string computeGridId(double lon, double lat) const {
        int col = lonToGridCol(lon);
        int row = latToGridRow(lat);
        return formatGridId(row, col);
    }

private:
    // 将经度转换为网格列索引
    int lonToGridCol(double lon) const {
        double normalized = std::max(min_lon_, std::min(max_lon_, lon));
        return static_cast<int>((normalized - min_lon_) / grid_size_);
    }
    
    // 将纬度转换为网格行索引
    int latToGridRow(double lat) const {
        double normalized = std::max(min_lat_, std::min(max_lat_, lat));
        return static_cast<int>((normalized - min_lat_) / grid_size_);
    }
    
    // 格式化网格ID
    std::string formatGridId(int row, int col) const {
        std::ostringstream oss;
        oss << "G_" 
            << std::setw(3) << std::setfill('0') << row << "_"
            << std::setw(3) << std::setfill('0') << col;
        return oss.str();
    }
    
    // 生成数据点键
    std::string generateKey(double lon, double lat, const std::string& grid_id) const {
        std::ostringstream oss;
        oss << grid_id << "|"
            << std::fixed << std::setprecision(7) << lon << "|"
            << std::fixed << std::setprecision(7) << lat;
        return oss.str();
    }
    
    // 处理单个网格内的数据
    void processGrid(const std::string& grid_id,
                    double min_lon, double min_lat,
                    double max_lon, double max_lat,
                    std::function<void(double, double, const std::string&)> callback) {
        // 尝试从缓存获取
        auto cache_item = cache_.get(grid_id);
        
        if (!cache_item) {
            // 缓存未命中，加载网格
            cache_item = loadGridToCache(grid_id);
        }
        
        if (cache_item) {
            // 处理缓存中的数据
            for (const auto& kv : cache_item->data) {
                // 从键中解析经纬度
                double lon, lat;
                if (parseKey(kv.first, lon, lat)) {
                    // 检查点是否在查询范围内
                    if (lon >= min_lon && lon <= max_lon &&
                        lat >= min_lat && lat <= max_lat) {
                        callback(lon, lat, kv.second);
                    }
                }
            }
        } else {
            // 后备方案：直接从数据库查询
            std::string start_key = grid_id + "|";
            std::string end_key = grid_id + "|~";  // ~是ASCII中较大的字符
            
            db_manager_.rangeQuery(start_key, end_key, 
                [&](const std::string& key, const std::string& value) {
                    double lon, lat;
                    if (parseKey(key, lon, lat)) {
                        if (lon >= min_lon && lon <= max_lon &&
                            lat >= min_lat && lat <= max_lat) {
                            callback(lon, lat, value);
                        }
                    }
                });
        }
    }
    
    // 加载网格数据到缓存
    std::shared_ptr<GridCacheItem> loadGridToCache(const std::string& grid_id) {
        auto cache_item = std::make_shared<GridCacheItem>();
        cache_item->grid_id = grid_id;
        
        std::string start_key = grid_id + "|";
        std::string end_key = grid_id + "|~";  // ~是ASCII中较大的字符
        
        // 从数据库加载整个网格的数据
        db_manager_.rangeQuery(start_key, end_key, 
            [&](const std::string& key, const std::string& value) {
                cache_item->data[key] = value;
            });
        
        // 放入缓存
        cache_.put(grid_id, cache_item);
        
        return cache_item;
    }
    
    // 从键中解析经纬度
    bool parseKey(const std::string& key, double& lon, double& lat) const {
        size_t first_delim = key.find('|');
        if (first_delim == std::string::npos) return false;
        
        size_t second_delim = key.find('|', first_delim + 1);
        if (second_delim == std::string::npos) return false;
        
        try {
            lon = std::stod(key.substr(first_delim + 1, second_delim - first_delim - 1));
            lat = std::stod(key.substr(second_delim + 1));
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    LevelDBManager& db_manager_;  // LevelDB管理器引用
    
    // 地理空间参数
    double min_lon_;
    double min_lat_;
    double max_lon_;
    double max_lat_;
    double grid_size_;
    
    // 网格参数
    int grid_rows_;
    int grid_cols_;
    int id_width_;  // 网格ID格式化宽度
    
    GridLRUCache cache_;  // 网格数据缓存
};

#endif // TERRAIN_STORAGE_ENGINE_HPP