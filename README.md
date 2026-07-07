# MiniRedis Desktop Manager

基于 **Qt5 + C++11** 的 Redis 兼容数据管理桌面客户端。一键启动内嵌 mini_redis 服务器，树形浏览 Key，支持 String/List/Hash/Set/ZSet 五种数据类型的可视化编辑。

---

## 预览

```
┌───────────────────────────────────────────────────────────┐
│  File  View  Help                                         │
│  [▶ Start] [■ Stop] [🔗 Connect] [⏻ Disconnect] [🔄 Refresh]│
├──────────────┬────────────────────────────────────────────┤
│  Key 浏览器   │  [Value] [Stats]                            │
│              │                                             │
│  🔍 user:*   │  Key: user:1001   Type: Hash   TTL: 86400  │
│              │  ─────────────────────────────────────────  │
│  📁 DB0      │  Field          Value                       │
│   ├─ user:1  │  name           Alice                       │
│   ├─ user:2  │  age            25                          │
│   ├─ cache:a │  active         true                        │
│   ├─ sess:x  │                                             │
│   └─ cfg:y   │  [💾 Save] [🗑 Delete] [🔄 Refresh]         │
├──────────────┴────────────────────────────────────────────┤
│  > SET mykey hello                                         │
│  +OK                                                       │
│  > GET mykey                                               │
│  $5                                                        │
│  hello                                                     │
├────────────────────────────────────────────────────────────┤
│  ● 已连接 127.0.0.1:54321                      DB0         │
└───────────────────────────────────────────────────────────┘
```

---

## 特性

| 功能 | 说明 |
|------|------|
| 🔌 一键启动 | 内嵌 mini_redis 服务器，点击即用 |
| 🌲 Key 浏览器 | 树形展示所有 Key，按 DB 分组，支持通配符搜索 |
| 📝 值编辑器 | 5 种数据类型各有专用编辑器，所见即所得 |
| 💻 命令控制台 | 类 redis-cli 交互，支持命令历史与自动补全 |
| 📊 性能仪表盘 | INFO 命令可视化，定时自动刷新 |
| 🎨 深色主题 | 控制台暗色风格，贴近专业开发工具 |

---

## 快速开始

### 编译环境

- **系统:** Linux (Ubuntu 20.04+) / Windows 11 WSL2 (WSLg)
- **编译器:** g++ 4.8+ / clang 3.3+ (C++11)
- **依赖:** Qt 5.12+ (Widgets + Network), CMake 3.10+

### 安装依赖

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install qtbase5-dev cmake g++ make

# 验证 Qt 安装
qmake --version
```

### 编译

```bash
# 1. 克隆项目
git clone https://github.com/Auli-lai/Mini_Redis.git
cd Mini_Redis

# 2. 编译 GUI 客户端
cd ../MiniRedisDesktop
mkdir -p build && cd build
cmake .. && make -j$(nproc)

```

### 运行

```bash
#GUI 自动启动内嵌 server
cd bin/
./MiniRedisDesktop
```

### WSLg (Windows 11)

Windows 11 WSL2 内置 WSLg，在 WSL 终端中编译运行即可，GUI 窗口自动出现在 Windows 桌面：

```bash
# 在 WSL Ubuntu 中
cd /mnt/e/Vscode\ projects/MiniRedisDesktop
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd bin/
./MiniRedisDesktop
```

---

## 项目结构

```
MiniRedisDesktop/
├── CMakeLists.txt
├── README.md
├── include
│   ├── console_panel.h
│   ├── key_browser.h
│   ├── log.h
│   ├── mainwindow.h
│   ├── redis_client.h
│   ├── resp_codec.h
│   ├── server_manager.h
│   ├── stats_dashboard.h
│   └── value_viewer.h
├── mini_redis
│   ├── CMakeLists.txt
│   ├── include
│   │   ├── command.h
│   │   ├── db.h
│   │   ├── epoll.h
│   │   ├── log.h
│   │   ├── queue.h
│   │   ├── resp_parser.h
│   │   ├── server.h
│   │   └── timer.h
│   └── src
│       ├── command.cpp
│       ├── db.cpp
│       ├── epoll.cpp
│       ├── log.cpp
│       ├── main.cpp
│       ├── resp_parser.cpp
│       ├── server.cpp
│       └── timer.cpp
├── resources
└── src
    ├── console_panel.cpp
    ├── key_browser.cpp
    ├── log.cpp
    ├── main.cpp
    ├── mainwindow.cpp
    ├── redis_client.cpp
    ├── server_manager.cpp
    ├── stats_dashboard.cpp
    └── value_viewer.cpp
```

---

## 技术亮点

| 层面 | 实现 |
|------|------|
| 网络模型 | Qt 异步 QTcpSocket，事件驱动，不阻塞 UI |
| 协议解析 | 手写 RESP 编解码器，借鉴 mini_redis 解析器设计 |
| 进程管理 | QProcess 管理 server 子进程，自动端口检测 |
| UI 架构 | 信号槽解耦，各组件独立可测试 |
| 数据编辑 | QStackedWidget 按类型切换编辑器，命令构造自动 RESP 编码 |

---

## 许可证

MIT License
