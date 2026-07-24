#ifndef DB_H
#define DB_H

#include <string>
#include <unordered_map>
#include <list>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <ctime>
#include <random>
#include <cstdio>

// ═══════════════════════════════════════════════════════════
//  跳表节点 (用于 Sorted Set)
// ═══════════════════════════════════════════════════════════
struct SkipListNode {
    std::string member;
    double score;
    std::vector<SkipListNode*> forward; // 每层的前向指针

    SkipListNode(const std::string& m, double s, int level)
        : member(m), score(s), forward(level, nullptr) {}
};

// ═══════════════════════════════════════════════════════════
//  跳表 (Sorted Set 底层实现)
// ═══════════════════════════════════════════════════════════
class SkipList {
public:
    static const int MAX_LEVEL = 12;

    SkipList();
    ~SkipList();

    void insert(const std::string& member, double score);
    bool remove(const std::string& member);
    double get_score(const std::string& member) const;
    int get_rank(const std::string& member) const;
    std::vector<std::string> range(int start, int stop) const; // 按 score 升序
    size_t size() const { return count; }
    std::vector<std::pair<std::string, double>> range_with_scores(int start, int stop) const;

private:
    SkipListNode* header;
    int level;
    size_t count;
    mutable std::mt19937 rng;

    int random_level();
};

// ═══════════════════════════════════════════════════════════
//  Redis 值类型
// ═══════════════════════════════════════════════════════════
enum ValueType { TYPE_STRING, TYPE_LIST, TYPE_HASH, TYPE_SET, TYPE_ZSET };

struct RedisObject {
    ValueType type;
    // 各类型的实际数据
    std::string str_val;                                      // String
    std::list<std::string> list_val;                          // List
    std::unordered_map<std::string, std::string> hash_val;    // Hash
    std::unordered_set<std::string> set_val;                  // Set
    SkipList* zset_val;                                       // Sorted Set (owned pointer)

    RedisObject() : type(TYPE_STRING), zset_val(nullptr) {}
    ~RedisObject() { delete zset_val; }
    RedisObject(const RedisObject&) = delete;
    RedisObject& operator=(const RedisObject&) = delete;
};

// ═══════════════════════════════════════════════════════════
//  数据库引擎
// ═══════════════════════════════════════════════════════════
class Database {
public:
    Database() : aof_filename("appendonly.aof"), aof_fp(nullptr), aof_writes(0) {}
    ~Database();

    // ── 基础命令 ──
    std::string set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    int del(const std::vector<std::string>& keys);
    int incr(const std::string& key);        // INCR
    int incrby(const std::string& key, int64_t delta);  // INCRBY
    int decr(const std::string& key);        // DECR
    int decrby(const std::string& key, int64_t delta);  // DECRBY
    int exists(const std::string& key);
    bool rename(const std::string& old_key, const std::string& new_key);
    std::vector<std::string> keys(const std::string& pattern);

    // ── List 命令 ──
    int lpush(const std::string& key, const std::vector<std::string>& values);
    int rpush(const std::string& key, const std::vector<std::string>& values);
    std::string lpop(const std::string& key);
    std::string rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    int llen(const std::string& key);

    // ── Hash 命令 ──
    int hset(const std::string& key, const std::string& field, const std::string& value);
    std::string hget(const std::string& key, const std::string& field);
    int hdel(const std::string& key, const std::string& field);
    std::vector<std::string> hgetall(const std::string& key);
    int hexists(const std::string& key, const std::string& field);

    // ── Set 命令 ──
    int sadd(const std::string& key, const std::vector<std::string>& members);
    int srem(const std::string& key, const std::string& member);
    std::vector<std::string> smembers(const std::string& key);
    int sismember(const std::string& key, const std::string& member);
    int scard(const std::string& key);

    // ── Sorted Set 命令 ──
    int zadd(const std::string& key, double score, const std::string& member);
    int zrem(const std::string& key, const std::string& member);
    std::string zscore(const std::string& key, const std::string& member);
    std::vector<std::string> zrange(const std::string& key, int start, int stop, bool withscores);
    int zrank(const std::string& key, const std::string& member);
    int zcard(const std::string& key);

    // ── 过期管理 ──
    int expire(const std::string& key, int seconds);
    int ttl(const std::string& key);
    void check_expire(const std::string& key);
    void active_expire_cycle(int max_samples = 20);

    // ── 持久化 ──
    int dbsize() const;
    std::string flushdb();
    std::string info();

    // ── 定时器回调：过期的 key 被标记删除 ──
    void expire_key_if_needed(const std::string& key);

private:
    // 主存储: key → RedisObject*
    std::unordered_map<std::string, RedisObject*> store;

    // 过期时间: key → 过期时间戳
    std::unordered_map<std::string, time_t> expires;

    mutable std::mutex mtx;

    // 确保 key 的类型正确，如果 key 不存在则创建
    RedisObject* get_or_create(const std::string& key, ValueType type);
    RedisObject* get_value(const std::string& key);
    bool is_expired(const std::string& key) const;
    bool match_pattern(const std::string& pattern, const std::string& key) const;

    // 内部无锁版本（调用方必须已持有 mtx）
    int _del_nolock(const std::vector<std::string>& keys);
    void _expire_key_if_needed_nolock(const std::string& key);

    // AOF 日志（使用持久文件句柄，避免每次 open/close）
    std::string aof_filename;
    FILE* aof_fp;
    int aof_writes;          // 写入计数，用于周期性 fflush
    void aof_append(const std::string& cmd_line);

public:
    void set_aof_file(const std::string& filename) { aof_filename = filename; }
    void load_aof();
};

#endif // DB_H
