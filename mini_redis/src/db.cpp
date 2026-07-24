#include "db.h"
#include "log.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════
//  跳表实现
// ═══════════════════════════════════════════════════════════

SkipList::SkipList() : level(0), count(0), rng(std::random_device{}()) {
    header = new SkipListNode("", -INFINITY, MAX_LEVEL);
}

SkipList::~SkipList() {
    SkipListNode* cur = header;
    while (cur) {
        SkipListNode* next = cur->forward[0];
        delete cur;
        cur = next;
    }
}

int SkipList::random_level() {
    int lvl = 1;
    std::uniform_int_distribution<int> dist(0, 3);
    while (lvl < MAX_LEVEL && dist(rng) == 0) lvl++;
    return lvl;
}

void SkipList::insert(const std::string& member, double score) {
    SkipListNode* update[MAX_LEVEL] = {};
    SkipListNode* cur = header;

    // 从最高层往下找插入位置
    for (int i = level; i >= 0; --i) {
        while (cur->forward[i] &&
               (cur->forward[i]->score < score ||
                (cur->forward[i]->score == score && cur->forward[i]->member < member))) {
            cur = cur->forward[i];
        }
        update[i] = cur;
    }

    // 检查是否已存在
    cur = cur->forward[0];
    if (cur && cur->score == score && cur->member == member) {
        cur->score = score; // 更新 score
        return;
    }

    int new_level = random_level() - 1; // 0-indexed
    if (new_level > level) {
        for (int i = level + 1; i <= new_level; ++i)
            update[i] = header;
        level = new_level;
    }

    SkipListNode* node = new SkipListNode(member, score, new_level + 1);
    for (int i = 0; i <= new_level; ++i) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }
    count++;
}

bool SkipList::remove(const std::string& member) {
    SkipListNode* update[MAX_LEVEL] = {};
    SkipListNode* cur = header;

    for (int i = level; i >= 0; --i) {
        while (cur->forward[i] && cur->forward[i]->member != member) {
            // 使用 score 加速查找: 实际上我们需要按 member 查找，
            // 但跳表是按 score 排序的。这里做一个简化：按 member 遍历。
            if (cur->forward[i]) cur = cur->forward[i];
            else break;
        }
        update[i] = cur;
    }

    // 简化: 从头遍历找到 member
    cur = header->forward[0];
    SkipListNode* target = nullptr;
    while (cur) {
        if (cur->member == member) {
            target = cur;
            break;
        }
        cur = cur->forward[0];
    }

    if (!target) return false;

    // 更新各层指针（使用已记录的 update 前驱节点）
    for (int i = 0; i <= level; ++i) {
        if (update[i]->forward[i] == target)
            update[i]->forward[i] = target->forward[i];
    }

    delete target;
    count--;

    while (level > 0 && header->forward[level] == nullptr) level--;
    return true;
}

double SkipList::get_score(const std::string& member) const {
    SkipListNode* cur = header->forward[0];
    while (cur) {
        if (cur->member == member) return cur->score;
        cur = cur->forward[0];
    }
    return 0.0;
}

int SkipList::get_rank(const std::string& member) const {
    int rank = 0;
    SkipListNode* cur = header->forward[0];
    while (cur) {
        if (cur->member == member) return rank;
        rank++;
        cur = cur->forward[0];
    }
    return -1;
}

std::vector<std::string> SkipList::range(int start, int stop) const {
    std::vector<std::string> result;
    int total = (int)count;
    if (start < 0) start = total + start;
    if (stop < 0) stop = total + stop;
    if (start < 0) start = 0;
    if (stop >= total) stop = total - 1;
    if (start > stop) return result;

    int idx = 0;
    SkipListNode* cur = header->forward[0];
    while (cur && idx <= stop) {
        if (idx >= start) result.push_back(cur->member);
        idx++;
        cur = cur->forward[0];
    }
    return result;
}

std::vector<std::pair<std::string, double>> SkipList::range_with_scores(int start, int stop) const {
    std::vector<std::pair<std::string, double>> result;
    int total = (int)count;
    if (start < 0) start = total + start;
    if (stop < 0) stop = total + stop;
    if (start < 0) start = 0;
    if (stop >= total) stop = total - 1;
    if (start > stop) return result;

    int idx = 0;
    SkipListNode* cur = header->forward[0];
    while (cur && idx <= stop) {
        if (idx >= start) result.emplace_back(cur->member, cur->score);
        idx++;
        cur = cur->forward[0];
    }
    return result;
}

// ═══════════════════════════════════════════════════════════
//  数据库引擎
// ═══════════════════════════════════════════════════════════

Database::~Database() {
    if (aof_fp) fclose(aof_fp);
    for (auto& kv : store) delete kv.second;
}

// ── 辅助函数 ──

RedisObject* Database::get_or_create(const std::string& key, ValueType type) {
    auto it = store.find(key);
    if (it != store.end()) {
        // 类型检查: 如果类型不匹配，返回 nullptr
        if (it->second->type != type) return nullptr;
        return it->second;
    }
    RedisObject* obj = new RedisObject();
    obj->type = type;
    if (type == TYPE_ZSET) obj->zset_val = new SkipList();
    store[key] = obj;
    return obj;
}

RedisObject* Database::get_value(const std::string& key) {
    // 调用方必须持有 mtx
    auto it = store.find(key);
    if (it == store.end()) return nullptr;
    _expire_key_if_needed_nolock(key);
    it = store.find(key); // 可能已被删除
    if (it == store.end()) return nullptr;
    return it->second;
}

bool Database::is_expired(const std::string& key) const {
    auto it = expires.find(key);
    if (it == expires.end()) return false;
    return time(nullptr) > it->second;
}

void Database::expire_key_if_needed(const std::string& key) {
    // 公开接口：自己加锁（外部调用安全）
    std::lock_guard<std::mutex> lock(mtx);
    if (is_expired(key)) {
        _del_nolock({key});
    }
}

void Database::_expire_key_if_needed_nolock(const std::string& key) {
    if (is_expired(key)) {
        _del_nolock({key});
    }
}

void Database::check_expire(const std::string& key) {
    expire_key_if_needed(key);
}

void Database::active_expire_cycle(int max_samples) {
    // 采样检查：最多检查 max_samples 个过期 key
    std::vector<std::string> to_del;
    {
        std::lock_guard<std::mutex> lock(mtx);
        time_t now = time(nullptr);
        int sampled = 0;
        for (auto& kv : expires) {
            if (++sampled > max_samples) break;
            if (now > kv.second) to_del.push_back(kv.first);
        }
    }
    for (auto& key : to_del) {
        del({key});
    }
    if (!to_del.empty())
        Log::info("Expired %zu keys", to_del.size());
}

bool Database::match_pattern(const std::string& pattern, const std::string& key) const {
    // 简单通配符匹配: * 匹配任意字符序列, ? 匹配单个字符
    if (pattern == "*") return true;
    size_t pi = 0, ki = 0;
    size_t ps = pattern.size(), ks = key.size();
    while (pi < ps && ki < ks) {
        if (pattern[pi] == '*') {
            pi++;
            if (pi >= ps) return true; // 末尾的 * 匹配剩余全部
            // 找到下一个 * 或末尾
            while (ki < ks) {
                if (match_pattern(pattern.substr(pi), key.substr(ki))) return true;
                ki++;
            }
            return false;
        } else if (pattern[pi] == '?' || pattern[pi] == key[ki]) {
            pi++; ki++;
        } else {
            return false;
        }
    }
    while (pi < ps && pattern[pi] == '*') pi++;
    return pi == ps && ki == ks;
}

// ── AOF 持久化 ──

void Database::aof_append(const std::string& cmd_line) {
    if (aof_filename.empty()) return;
    if (!aof_fp) {
        aof_fp = fopen(aof_filename.c_str(), "a");
        if (!aof_fp) {
            Log::error("Failed to open AOF file: %s", aof_filename.c_str());
            return;
        }
    }
    fprintf(aof_fp, "%s\n", cmd_line.c_str());
    // 每 1000 次写入 fflush 一次，兼顾性能与数据安全
    if (++aof_writes >= 1000) {
        fflush(aof_fp);
        aof_writes = 0;
    }
}

void Database::load_aof() {
    if (aof_filename.empty()) return;
    std::ifstream aof(aof_filename);
    if (!aof) return;

    std::string line;
    int count = 0;
    while (std::getline(aof, line)) {
        if (line.empty()) continue;
        // AOF 格式: *N\r\n$LEN\r\nCMD\r\n... (完整 RESP 格式)
        // 简化加载: 只处理 SET 命令
        // 生产级实现应该用 RESP 解析器回放
        if (line.find("*3") == 0 || line.find("*2") == 0) {
            // 跳过（AOF 中的完整 RESP 格式需要专门的解析器回放）
            // 这里只做基本计数
            count++;
        }
    }
    Log::info("AOF loaded: %d entries scanned", count);
}

// ═══════════════════════════════════════════════════════════
//  命令实现
// ═══════════════════════════════════════════════════════════

// ── String ──

std::string Database::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = store.find(key);
    if (it != store.end()) {
        delete it->second;
        store.erase(it);
    }
    RedisObject* obj = new RedisObject();
    obj->type = TYPE_STRING;
    obj->str_val = value;
    store[key] = obj;
    aof_append("SET " + key + " " + value);
    return "+OK\r\n";
}

std::string Database::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    _expire_key_if_needed_nolock(key);
    auto it = store.find(key);
    if (it == store.end() || it->second->type != TYPE_STRING)
        return "$-1\r\n";
    std::string& val = it->second->str_val;
    return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
}

int Database::incr(const std::string& key) {
    return incrby(key, 1);
}

int Database::incrby(const std::string& key, int64_t delta) {
    std::lock_guard<std::mutex> lock(mtx);
    _expire_key_if_needed_nolock(key);
    auto it = store.find(key);
    if (it == store.end()) {
        // key 不存在，初始化为 0 然后加 delta
        RedisObject* obj = new RedisObject();
        obj->type = TYPE_STRING;
        obj->str_val = std::to_string(delta);
        store[key] = obj;
        return (int)delta;
    }
    if (it->second->type != TYPE_STRING) {
        throw std::runtime_error("WRONGTYPE Operation against a key holding the wrong kind of value");
    }
    std::string& val = it->second->str_val;
    int64_t num;
    try {
        num = std::stoll(val);
    } catch (...) {
        throw std::runtime_error("ERR value is not an integer or out of range");
    }
    num += delta;
    val = std::to_string(num);
    return (int)num;
}

int Database::decr(const std::string& key) {
    return decrby(key, 1);
}

int Database::decrby(const std::string& key, int64_t delta) {
    return incrby(key, -delta);
}

int Database::del(const std::vector<std::string>& keys) {
    std::lock_guard<std::mutex> lock(mtx);
    return _del_nolock(keys);
}

bool Database::rename(const std::string& old_key, const std::string& new_key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = store.find(old_key);
    if (it == store.end()) return false;
    // 如果 new_key 已存在，先删除
    auto nit = store.find(new_key);
    if (nit != store.end()) {
        delete nit->second;
        store.erase(nit);
        expires.erase(new_key);
    }
    store[new_key] = it->second;
    store.erase(it);
    // 迁移过期时间
    auto eit = expires.find(old_key);
    if (eit != expires.end()) {
        expires[new_key] = eit->second;
        expires.erase(eit);
    }
    return true;
}

int Database::_del_nolock(const std::vector<std::string>& keys) {
    // 调用方必须已持有 mtx
    int count = 0;
    for (auto& key : keys) {
        auto it = store.find(key);
        if (it != store.end()) {
            delete it->second;
            store.erase(it);
            expires.erase(key);
            count++;
        }
    }
    return count;
}

int Database::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    _expire_key_if_needed_nolock(key);
    return store.count(key) ? 1 : 0;
}

std::vector<std::string> Database::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> result;
    for (auto& kv : store) {
        if (!is_expired(kv.first) && match_pattern(pattern, kv.first))
            result.push_back(kv.first);
    }
    return result;
}

// ── List ──

int Database::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_or_create(key, TYPE_LIST);
    if (!obj) return -1;
    for (auto it = values.rbegin(); it != values.rend(); ++it)
        obj->list_val.push_front(*it);
    int len = (int)obj->list_val.size();
    aof_append("LPUSH " + key + " " + std::to_string(values.size()));
    return len;
}

int Database::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_or_create(key, TYPE_LIST);
    if (!obj) return -1;
    for (auto& v : values) obj->list_val.push_back(v);
    int len = (int)obj->list_val.size();
    aof_append("RPUSH " + key + " " + std::to_string(values.size()));
    return len;
}

std::string Database::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_LIST || obj->list_val.empty())
        return "$-1\r\n";
    std::string val = obj->list_val.front();
    obj->list_val.pop_front();
    return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
}

std::string Database::rpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_LIST || obj->list_val.empty())
        return "$-1\r\n";
    std::string val = obj->list_val.back();
    obj->list_val.pop_back();
    return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
}

std::vector<std::string> Database::lrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    std::vector<std::string> result;
    if (!obj || obj->type != TYPE_LIST) return result;

    auto& lst = obj->list_val;
    int size = (int)lst.size();
    if (start < 0) start = size + start;
    if (stop < 0) stop = size + stop;
    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;
    if (start > stop) return result;

    int idx = 0;
    for (auto& v : lst) {
        if (idx >= start && idx <= stop) result.push_back(v);
        if (idx > stop) break;
        idx++;
    }
    return result;
}

int Database::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_LIST) return 0;
    return (int)obj->list_val.size();
}

// ── Hash ──

int Database::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_or_create(key, TYPE_HASH);
    if (!obj) return -1;
    int created = obj->hash_val.count(field) ? 0 : 1;
    obj->hash_val[field] = value;
    aof_append("HSET " + key + " " + field + " " + value);
    return created;
}

std::string Database::hget(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_HASH) return "$-1\r\n";
    auto it = obj->hash_val.find(field);
    if (it == obj->hash_val.end()) return "$-1\r\n";
    return "$" + std::to_string(it->second.size()) + "\r\n" + it->second + "\r\n";
}

int Database::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_HASH) return 0;
    return obj->hash_val.erase(field) ? 1 : 0;
}

std::vector<std::string> Database::hgetall(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    std::vector<std::string> result;
    if (!obj || obj->type != TYPE_HASH) return result;
    for (auto& kv : obj->hash_val) {
        result.push_back(kv.first);
        result.push_back(kv.second);
    }
    return result;
}

int Database::hexists(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_HASH) return 0;
    return obj->hash_val.count(field) ? 1 : 0;
}

// ── Set ──

int Database::sadd(const std::string& key, const std::vector<std::string>& members) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_or_create(key, TYPE_SET);
    if (!obj) return -1;
    int added = 0;
    for (auto& m : members) {
        if (obj->set_val.insert(m).second) added++;
    }
    return added;
}

int Database::srem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_SET) return 0;
    return obj->set_val.erase(member) ? 1 : 0;
}

std::vector<std::string> Database::smembers(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    std::vector<std::string> result;
    if (!obj || obj->type != TYPE_SET) return result;
    for (auto& m : obj->set_val) result.push_back(m);
    return result;
}

int Database::sismember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_SET) return 0;
    return obj->set_val.count(member) ? 1 : 0;
}

int Database::scard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_SET) return 0;
    return (int)obj->set_val.size();
}

// ── Sorted Set ──

int Database::zadd(const std::string& key, double score, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_or_create(key, TYPE_ZSET);
    if (!obj) return -1;
    if (!obj->zset_val) obj->zset_val = new SkipList();
    size_t old_count = obj->zset_val->size();
    obj->zset_val->insert(member, score);
    aof_append("ZADD " + key + " " + std::to_string(score) + " " + member);
    return (obj->zset_val->size() > old_count) ? 1 : 0;
}

int Database::zrem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_ZSET || !obj->zset_val) return 0;
    return obj->zset_val->remove(member) ? 1 : 0;
}

std::string Database::zscore(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_ZSET || !obj->zset_val) return "$-1\r\n";
    double score = obj->zset_val->get_score(member);
    std::string s = std::to_string(score);
    // 去掉尾部多余的 0
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.') s.pop_back();
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::vector<std::string> Database::zrange(const std::string& key, int start, int stop, bool withscores) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    std::vector<std::string> result;
    if (!obj || obj->type != TYPE_ZSET || !obj->zset_val) return result;

    auto items = obj->zset_val->range_with_scores(start, stop);
    for (auto& kv : items) {
        result.push_back(kv.first);
        if (withscores) {
            std::string s = std::to_string(kv.second);
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.') s.pop_back();
            result.push_back(s);
        }
    }
    return result;
}

int Database::zrank(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_ZSET || !obj->zset_val) return -1;
    return obj->zset_val->get_rank(member);
}

int Database::zcard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    RedisObject* obj = get_value(key);
    if (!obj || obj->type != TYPE_ZSET || !obj->zset_val) return 0;
    return (int)obj->zset_val->size();
}

// ── 过期 ──

int Database::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!store.count(key)) return 0;
    expires[key] = time(nullptr) + seconds;
    return 1;
}

int Database::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!store.count(key)) return -2; // key 不存在
    auto it = expires.find(key);
    if (it == expires.end()) return -1; // 永不过期
    time_t remain = it->second - time(nullptr);
    return remain < 0 ? -2 : (int)remain;
}

// ── 管理 ──

int Database::dbsize() const {
    std::lock_guard<std::mutex> lock(mtx);
    return (int)store.size();
}

std::string Database::flushdb() {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& kv : store) delete kv.second;
    store.clear();
    expires.clear();
    return "+OK\r\n";
}

std::string Database::info() {
    std::lock_guard<std::mutex> lock(mtx);
    std::ostringstream ss;
    ss << "# Server\r\n";
    ss << "mini_redis_version:1.0.0\r\n";
    ss << "# Keyspace\r\n";
    ss << "db0:keys=" << store.size() << ",expires=" << expires.size() << "\r\n";
    std::string s = ss.str();
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}
