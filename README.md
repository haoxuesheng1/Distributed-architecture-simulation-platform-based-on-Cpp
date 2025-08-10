# Distributed-architecture-simulation-platform-based-on-Cpp
这是一个基于C++的分布式仿真平台项目，包括以下几个部分：基于C++20设计并实现分布式通信中间件，同时基于ZeroMQ+Protobuf构建节点通信框架； 设计并实现资源池（线程池（可选动态扩缩容策略）和数据库连接池）； 构建基于Raft共识算法的分布式任务调度引擎，实现节点状态同步和故障自动恢复；设计LevelDB存储引擎优化方案：通过键值结构优化和布隆过滤器集成，实现亿级地形数据毫秒级查询；采用无锁队列（moodycamel::ConcurrentQueue）减少线程切换开销，结合内存池（tcmalloc）降低内存碎片。由于某些原因，目前仅上传部分组件。
