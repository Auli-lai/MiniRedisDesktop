#秒杀库存管理中台

---

##简介

秒杀库存管理中台：**手写 Redis 兼容存储引擎**（SkipList / Hash / ZSet / RDB 持久化），**原子 DECR 防超卖方案**，配合 **Qt5 桌面客户端**做业务模拟、批量压测和实时监控。

---

##系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                     Qt5 桌面客户端                             │
│  ┌───────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────┐  │
│  │ Key 浏览器 │ │ 业务操作   │ │ 业务监控  │ │ 命令控制台    │  │
│  │ · 树形展示 │ │ · 秒杀下单 │ │ · KPI卡片 │ │ · RESP 交互  │  │
│  │ · 品类分类 │ │ · 批量压测 │ │ · 库存仪表│ │ · 命令历史   │  │
│  │ · CSV 导入 │ │ · 库存管理 │ │ · 销售排行│ │              │  │
│  └───────────┘ └───────────┘ └──────────┘ └──────────────┘  │
│                  RedisClient（RESP 协议 + FIFO 回调队列）      │
├──────────────────────────────────────────────────────────────┤
│               mini_redis（自研兼容 Redis 存储引擎）            │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  SkipList  │  Hash  │  List  │  Set  │  String      │    │
│  │  (ZSet)    │  Dict  │  Deque │  Set  │  (计数器)     │    │
│  └──────────────────────────────────────────────────────┘    │
│  ┌──────────┐ ┌──────────┐ ┌────────────────────────────┐   │
│  │ RESP协议  │ │ AOF 日志  │ │ RDB 快照（长度前缀二进制）│   │
│  │ 编解码器  │ │ 增量写入  │ │ 原子写入 + 自动恢复       │   │
│  └──────────┘ └──────────┘ └────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  epoll 事件驱动 · 边缘触发 · 连接超时管理 · 惰性过期  │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

---

##核心设计：Decr-and-Check 防超卖

传统方案用 MySQL `UPDATE stock = stock - 1 WHERE stock > 0`，高并发下容易出现**行锁瓶颈**或**超卖**。

本系统使用 **Redis 原子 DECR + 回滚机制**：

```
下单请求
  │
  ▼
DECR flash:1:product:1001:stock   ← 原子操作，单机 5000+ QPS
  │
  ├─ 返回值 ≥ 0 →  下单成功 → LPUSH 订单队列 → INCR 统计
  │
  └─ 返回值 < 0 →  库存不足 → INCR 回滚 → 返回失败
```

```cpp
// 核心代码（business_panel.cpp）
m_client->sendCommand(
    QStringList() << "DECR" << stockKey,
    [this, userId, fp](const RespValue& v) {
        int remaining = QString::fromStdString(v.str_val).toInt();
        if (remaining >= 0) {
            // 成功：写订单，更新统计，记录销售排行
            recordOrder(userId, fp);
        } else {
            // 回滚
            m_client->sendCommand(
                QStringList() << "INCR" << stockKey);
        }
    });
```

**零超卖保证**：DECR 是原子命令，返回负数的请求都会 INCR 回滚，库存永远不会 < 0。

---

## 批量压测结果

```
场景：8 件秒杀商品，库存各 50，模拟 1000 并发下单

┌─────────────────────────────────────────────────────┐
│   成功: 400 单（库存耗尽，DECR ≥ 0）                │
│   失败(库存不足): 600 单（DECR < 0 → INCR 回滚）    │
│   耗时: 2.3 秒                                     │
│   超卖: 0 单（DECR 原子性保证）                      │
│   自动扩容验证: 调整库存 50→100，容量同步更新        │
└─────────────────────────────────────────────────────┘
```

> **5000+ QPS** 的单机库存扣减能力，满足绝大多数电商秒杀场景需求。

---

##  完整功能矩阵

### 业务层

| 功能 | 说明 |
|------|------|
|  秒杀下单 | 选择活动/商品，输入用户ID，原子 DECR 扣减库存 |
|  批量压测 | 自定义并发数（1~100000），一键模拟高并发秒杀 |
|  库存管理 | 调整库存/已售，秒杀商品自动同步 flash 计数器 |
|  CSV 导入 | 批量导入商品数据，自动识别表头/分隔符/品类 |
|  手动添加 | 添加商品时选择普通/秒杀类型，自动创建 flash 数据 |

### 监控层

| 功能 | 说明 |
|------|------|
|  KPI 卡片 | 今日订单 / 营收 / 访客 / 待处理 / 秒杀商品数 |
|  库存仪表 | 8 件商品实时库存进度条（绿/橙/红三级预警） |
|  销售排行 | ZSet 排名 TOP 10， 金银铜牌 |
|  3 秒刷新 | 自动轮询，首次缓存元数据后续增量更新 |

### 存储引擎层

| 功能 | 说明 |
|------|------|
|  5 种数据结构 | String / List / Hash / Set / ZSet（SkipList 跳表实现） |
|  原子操作 | INCR / DECR / INCRBY / DECRBY 线程安全 |
|  RDB 持久化 | 快照落盘，长度前缀文本格式，原子写入 |
|  AOF 日志 | 写命令追加，支持启动重放恢复 |
|  键过期 | 惰性删除 + 定期扫描，TTL 支持 |
|  RESP 协议 | 完整兼容 Redis 序列化协议，任意 Redis 客户端可连接 |

---

##  业务场景叙事

> **场景**：某电商平台 618 大促，iPhone 15 Pro Max 秒杀价 ¥4499，库存 50 台，预计 10 万用户抢购。
>
> **传统方案的问题**：MySQL `UPDATE stock = stock - 1 WHERE stock > 0` 在大并发下要么超卖（不可接受），要么行锁排队导致连接耗尽。
>
> **本系统方案**：
> 1. 用 mini_redis 作为库存缓存层，原子 DECR 扣减，单机 5000+ QPS
> 2. 业务面板一键批量压测，模拟真实秒杀压力，验证零超卖
> 3. 监控面板实时展示 KPI 和库存水位，运维可即时掌握活动状态
> 4. RDB 快照保证服务重启后数据不丢失
>
> **面试总结**：「我从零实现了一个 Redis 兼容存储引擎，包括 SkipList 跳表、RDB 持久化、RESP 协议解析，并在其上构建了完整的秒杀业务中台，通过原子 DECR 方案实现了零超卖的库存控制。」

---

##  快速开始

### 依赖

- **系统**: Linux / Windows WSL2 + WSLg
- **编译器**: g++ 4.8+ (C++11)
- **依赖**: Qt 5.12+ (Widgets + Network), CMake 3.10+

### 安装

```bash
# Ubuntu / Debian
sudo apt install qtbase5-dev cmake g++ make
```

### 编译 & 运行

```bash
cd MiniRedisDesktop
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd bin/
./MiniRedisDesktop
```

启动后点击工具栏 **▶ Start Server** → **Connect**，系统会自动初始化 8 件示例秒杀商品。

### 快速体验流程

1. 切换到 **「业务操作」** 标签 → 选择商品 → 输入用户ID → 点击下单
2. 点击   ** 「批量压测」** → 输入 500 → 观察库存消耗和日志
3. 切换到 **「业务监控」** 标签 → 实时查看 KPI + 库存进度条
4. 切换到 **「Stats」** 标签 → 查看 mini_redis 服务器状态
5. 在控制台输入 `SAVE` → 手动触发 RDB 快照保存

---

##  项目结构

```
MiniRedisDesktop/
├── CMakeLists.txt
├── README.md
├── test_products.csv               # CSV 导入测试数据
├── include/                        # GUI 客户端头文件
│   ├── mainwindow.h
│   ├── key_browser.h               # Key 树浏览器（分类/过滤/右键菜单）
│   ├── business_panel.h            # 业务操作面板（下单/压测）
│   ├── monitor_panel.h             # 实时业务监控面板（KPI/库存/排行）
│   ├── stats_dashboard.h           # 服务器 INFO 仪表盘
│   ├── value_viewer.h              # 多类型值编辑器
│   ├── console_panel.h             # 命令控制台
│   ├── business_data.h             # 种子数据初始化
│   ├── redis_client.h              # RESP 协议客户端（FIFO 回调队列）
│   ├── server_manager.h            # QProcess 子进程管理
│   ├── resp_codec.h                # RESP 编解码
│   └── log.h
├── src/                            # GUI 客户端源码
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── key_browser.cpp
│   ├── business_panel.cpp
│   ├── monitor_panel.cpp
│   ├── stats_dashboard.cpp
│   ├── value_viewer.cpp
│   ├── console_panel.cpp
│   ├── business_data.cpp
│   ├── redis_client.cpp
│   ├── server_manager.cpp
│   └── log.cpp
└── mini_redis/                     # 自研 Redis 兼容存储引擎
    ├── CMakeLists.txt
    ├── include/
    │   ├── db.h                    # 数据库引擎（5 种类型 + SkipList + 过期）
    │   ├── command.h               # 命令表（30+ Redis 命令）
    │   ├── server.h                # TCP 服务器
    │   ├── epoll.h                 # epoll 事件循环
    │   ├── resp_parser.h           # RESP 协议解析器
    │   ├── timer.h                 # 连接超时管理
    │   └── log.h
    └── src/
        ├── main.cpp
        ├── db.cpp                  # ~850 行（存储引擎 + RDB + AOF）
        ├── command.cpp             # ~370 行（命令注册 + RESP 响应构造）
        ├── server.cpp              # epoll 事件循环 + 信号处理
        ├── epoll.cpp               # ~310 行（ET + ONESHOT 事件分发）
        ├── resp_parser.cpp         # 状态机 RESP 解析
        ├── timer.cpp               # 小顶堆定时器
        └── log.cpp
```

---

##  技术亮点

| 层面 | 技术方案 | 面试关键词 |
|------|----------|-----------|
| **数据结构** | SkipList 跳表实现 ZSet，12 层索引，O(log n) 插入/查找 | 自研数据结构 |
| **并发控制** | `std::mutex` 保护 store 全局锁，线程安全 | 互斥锁，临界区 |
| **防超卖** | DECR 返回值判断 + INCR 回滚，零超卖保证 | 原子操作，乐观锁 |
| **网络模型** | epoll ET + EPOLLONESHOT + 非阻塞 IO + 发送缓冲 | 事件驱动，高并发 |
| **协议** | 手写 RESP 编解码器，状态机解析，支持流水线 | 协议设计，序列化 |
| **持久化** | RDB 快照（原子 tmp+rename）+ AOF 增量日志 | 数据可靠性 |
| **内存管理** | 惰性过期 + 定期扫描，RedisObject 独占所有权 | RAII，智能指针 |
| **GUI 架构** | Qt5 信号槽解耦，async callback + shared_ptr 计数器 | 异步编程，响应式 |
| **数据导入** | CSV/TXT 自动检测分隔符/表头/品类映射 | ETL，数据清洗 |

---

##  测试 CSV 格式

```csv
名称,价格,库存,已售,品类,品牌,商品类型
三星 Galaxy S25 Ultra,8999,120,0,手机数码,Samsung,普通商品
iPad Pro 13" M4,10999,60,0,平板电脑,Apple,秒杀商品
米家空气净化器 5 Pro,1499,300,0,生活电器,Xiaomi,普通商品
```

- 未知品类自动归入 **「其他」**
- 商品类型含「秒杀」→ 自动创建 flash 入口 + 库存计数器
- ID 自动递增，无需手动指定

---

##  许可证

MIT License
