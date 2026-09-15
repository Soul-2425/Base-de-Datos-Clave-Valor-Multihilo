#include "KeyValueStore.hpp"
#include <algorithm>
#include <stdexcept>

KeyValueStore::KeyValueStore() = default;

void KeyValueStore::set(const std::string& key, std::string value, std::optional<std::chrono::seconds> ttl_seconds) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    std::optional<std::chrono::steady_clock::time_point> expires_at = std::nullopt;
    if (ttl_seconds.has_value() && ttl_seconds->count() > 0) {
        expires_at = now + *ttl_seconds;
    }

    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    shard.string_map[key] = StoreEntry{ std::move(value), expires_at, now };
}

std::optional<std::string> KeyValueStore::get(const std::string& key) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    {
        std::shared_lock<std::shared_mutex> s_lock(shard.mutex);
        auto it = shard.string_map.find(key);
        if (it == shard.string_map.end()) {
            return std::nullopt;
        }

        if (!it->second.expires_at.has_value() || it->second.expires_at.value() > now) {
            std::string val = it->second.value;
            s_lock.unlock();

            std::unique_lock<std::shared_mutex> u_lock(shard.mutex);
            auto it_up = shard.string_map.find(key);
            if (it_up != shard.string_map.end()) {
                it_up->second.last_accessed = now;
            }
            return val;
        }
    }

    // Limpieza pasiva de clave expirada
    std::unique_lock<std::shared_mutex> u_lock(shard.mutex);
    auto it = shard.string_map.find(key);
    if (it != shard.string_map.end()) {
        if (it->second.expires_at.has_value() && it->second.expires_at.value() <= now) {
            shard.string_map.erase(it);
            return std::nullopt;
        }
        return it->second.value;
    }

    return std::nullopt;
}

bool KeyValueStore::del(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    bool removed = (shard.string_map.erase(key) > 0);
    removed = (shard.hash_map.erase(key) > 0) || removed;
    removed = (shard.list_map.erase(key) > 0) || removed;
    return removed;
}

bool KeyValueStore::exists(const std::string& key) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    std::shared_lock<std::shared_mutex> lock(shard.mutex);
    auto it_str = shard.string_map.find(key);
    if (it_str != shard.string_map.end()) {
        if (!it_str->second.expires_at.has_value() || it_str->second.expires_at.value() > now) {
            return true;
        }
    }

    if (shard.hash_map.find(key) != shard.hash_map.end()) return true;
    if (shard.list_map.find(key) != shard.list_map.end()) return true;

    return false;
}

bool KeyValueStore::expire(const std::string& key, std::chrono::seconds ttl_seconds) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.string_map.find(key);
    if (it == shard.string_map.end()) {
        return false;
    }

    if (it->second.expires_at.has_value() && it->second.expires_at.value() <= now) {
        shard.string_map.erase(it);
        return false;
    }

    if (ttl_seconds.count() > 0) {
        it->second.expires_at = now + ttl_seconds;
    } else {
        shard.string_map.erase(it);
    }
    return true;
}

std::int64_t KeyValueStore::ttl(const std::string& key) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    std::shared_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.string_map.find(key);
    if (it == shard.string_map.end()) {
        if (shard.hash_map.find(key) != shard.hash_map.end() || 
            shard.list_map.find(key) != shard.list_map.end()) {
            return -1; // Existe pero no tiene TTL
        }
        return -2; // No existe
    }

    if (!it->second.expires_at.has_value()) {
        return -1;
    }

    if (it->second.expires_at.value() <= now) {
        lock.unlock();
        std::unique_lock<std::shared_mutex> u_lock(shard.mutex);
        shard.string_map.erase(key);
        return -2;
    }

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second.expires_at.value() - now).count();
    return remaining >= 0 ? remaining : -2;
}

std::size_t KeyValueStore::size() const {
    std::size_t total = 0;
    auto now = std::chrono::steady_clock::now();

    for (const StoreShard& shard : shards_) {
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        for (const auto& [_, entry] : shard.string_map) {
            if (!entry.expires_at.has_value() || entry.expires_at.value() > now) {
                ++total;
            }
        }
        total += shard.hash_map.size();
        total += shard.list_map.size();
    }
    return total;
}

void KeyValueStore::clear() {
    for (StoreShard& shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        shard.string_map.clear();
        shard.hash_map.clear();
        shard.list_map.clear();
    }
}

std::pair<bool, std::int64_t> KeyValueStore::incr_by(const std::string& key, std::int64_t delta) {
    StoreShard& shard = get_shard(key);
    auto now = std::chrono::steady_clock::now();

    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.string_map.find(key);

    if (it == shard.string_map.end() || 
        (it->second.expires_at.has_value() && it->second.expires_at.value() <= now)) {
        // Clave no existe o ha expirado: se inicializa en delta
        shard.string_map[key] = StoreEntry{ std::to_string(delta), std::nullopt, now };
        return { true, delta };
    }

    try {
        std::int64_t current_val = std::stoll(it->second.value);
        current_val += delta;
        it->second.value = std::to_string(current_val);
        it->second.last_accessed = now;
        return { true, current_val };
    } catch (...) {
        return { false, 0 }; // No es un entero válido
    }
}

// ==========================================
// MÉTODOS PARA HASHES
// ==========================================
bool KeyValueStore::hset(const std::string& key, const std::string& field, std::string value) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto& fields = shard.hash_map[key];
    bool is_new = (fields.find(field) == fields.end());
    fields[field] = std::move(value);
    return is_new;
}

std::optional<std::string> KeyValueStore::hget(const std::string& key, const std::string& field) {
    StoreShard& shard = get_shard(key);
    std::shared_lock<std::shared_mutex> lock(shard.mutex);

    auto it_key = shard.hash_map.find(key);
    if (it_key == shard.hash_map.end()) return std::nullopt;

    auto it_field = it_key->second.find(field);
    if (it_field == it_key->second.end()) return std::nullopt;

    return it_field->second;
}

bool KeyValueStore::hdel(const std::string& key, const std::string& field) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto it_key = shard.hash_map.find(key);
    if (it_key == shard.hash_map.end()) return false;

    bool erased = (it_key->second.erase(field) > 0);
    if (it_key->second.empty()) {
        shard.hash_map.erase(it_key);
    }
    return erased;
}

bool KeyValueStore::hexists(const std::string& key, const std::string& field) {
    return hget(key, field).has_value();
}

std::size_t KeyValueStore::hlen(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::shared_lock<std::shared_mutex> lock(shard.mutex);

    auto it_key = shard.hash_map.find(key);
    if (it_key == shard.hash_map.end()) return 0;
    return it_key->second.size();
}

std::vector<std::pair<std::string, std::string>> KeyValueStore::hgetall(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::shared_lock<std::shared_mutex> lock(shard.mutex);

    std::vector<std::pair<std::string, std::string>> result;
    auto it_key = shard.hash_map.find(key);
    if (it_key != shard.hash_map.end()) {
        result.reserve(it_key->second.size());
        for (const auto& [f, v] : it_key->second) {
            result.emplace_back(f, v);
        }
    }
    return result;
}

// ==========================================
// MÉTODOS PARA LISTAS
// ==========================================
std::size_t KeyValueStore::lpush(const std::string& key, std::string value) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto& list = shard.list_map[key];
    list.push_front(std::move(value));
    return list.size();
}

std::size_t KeyValueStore::rpush(const std::string& key, std::string value) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto& list = shard.list_map[key];
    list.push_back(std::move(value));
    return list.size();
}

std::optional<std::string> KeyValueStore::lpop(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto it = shard.list_map.find(key);
    if (it == shard.list_map.end() || it->second.empty()) return std::nullopt;

    std::string val = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) shard.list_map.erase(it);
    return val;
}

std::optional<std::string> KeyValueStore::rpop(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto it = shard.list_map.find(key);
    if (it == shard.list_map.end() || it->second.empty()) return std::nullopt;

    std::string val = std::move(it->second.back());
    it->second.pop_back();
    if (it->second.empty()) shard.list_map.erase(it);
    return val;
}

std::size_t KeyValueStore::llen(const std::string& key) {
    StoreShard& shard = get_shard(key);
    std::shared_lock<std::shared_mutex> lock(shard.mutex);

    auto it = shard.list_map.find(key);
    if (it == shard.list_map.end()) return 0;
    return it->second.size();
}

std::vector<std::string> KeyValueStore::lrange(const std::string& key, std::int64_t start, std::int64_t stop) {
    StoreShard& shard = get_shard(key);
    std::shared_lock<std::shared_mutex> lock(shard.mutex);

    auto it = shard.list_map.find(key);
    if (it == shard.list_map.end() || it->second.empty()) return {};

    const auto& list = it->second;
    std::int64_t len = static_cast<std::int64_t>(list.size());

    // Normalización de índices negativos estilo Redis (-1 es último elemento)
    if (start < 0) start = std::max<std::int64_t>(0, len + start);
    if (stop < 0) stop = len + stop;

    if (start >= len || start > stop) return {};
    stop = std::min<std::int64_t>(stop, len - 1);

    std::vector<std::string> res;
    res.reserve(static_cast<std::size_t>(stop - start + 1));
    for (std::int64_t i = start; i <= stop; ++i) {
        res.push_back(list[static_cast<std::size_t>(i)]);
    }
    return res;
}

// ==========================================
// MANTENIMIENTO, LRU Y SNAPSHOT
// ==========================================
std::size_t KeyValueStore::purge_expired_keys(std::size_t max_keys_per_shard) {
    std::size_t purged = 0;
    auto now = std::chrono::steady_clock::now();

    for (StoreShard& shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        std::size_t checked = 0;

        for (auto it = shard.string_map.begin(); it != shard.string_map.end() && checked < max_keys_per_shard;) {
            ++checked;
            if (it->second.expires_at.has_value() && it->second.expires_at.value() <= now) {
                it = shard.string_map.erase(it);
                ++purged;
            } else {
                ++it;
            }
        }
    }
    return purged;
}

std::size_t KeyValueStore::evict_lru_keys(std::size_t keys_to_evict) {
    if (keys_to_evict == 0) return 0;

    struct KeyTimestamp {
        std::string key;
        std::chrono::steady_clock::time_point last_accessed;
        std::size_t shard_idx;
    };

    std::vector<KeyTimestamp> candidates;

    for (std::size_t i = 0; i < NUM_SHARDS; ++i) {
        std::shared_lock<std::shared_mutex> lock(shards_[i].mutex);
        std::size_t sample = 0;
        for (const auto& [k, entry] : shards_[i].string_map) {
            candidates.push_back({ k, entry.last_accessed, i });
            if (++sample >= 10) break;
        }
    }

    if (candidates.empty()) return 0;

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.last_accessed < b.last_accessed;
    });

    std::size_t evicted = 0;
    std::size_t target = std::min(keys_to_evict, candidates.size());

    for (std::size_t i = 0; i < target; ++i) {
        const auto& cand = candidates[i];
        std::unique_lock<std::shared_mutex> lock(shards_[cand.shard_idx].mutex);
        if (shards_[cand.shard_idx].string_map.erase(cand.key) > 0) {
            ++evicted;
        }
    }

    return evicted;
}

StoreSnapshot KeyValueStore::get_snapshot() const {
    StoreSnapshot snapshot;
    auto now = std::chrono::steady_clock::now();

    for (const StoreShard& shard : shards_) {
        std::shared_lock<std::shared_mutex> lock(shard.mutex);

        // Extraer strings válidos
        for (const auto& [k, entry] : shard.string_map) {
            if (!entry.expires_at.has_value() || entry.expires_at.value() > now) {
                snapshot.strings.emplace_back(k, entry.value);
            }
        }

        // Extraer hashes
        for (const auto& [k, fields] : shard.hash_map) {
            std::vector<std::pair<std::string, std::string>> field_pairs;
            field_pairs.reserve(fields.size());
            for (const auto& [f, v] : fields) {
                field_pairs.emplace_back(f, v);
            }
            snapshot.hashes.emplace_back(k, std::move(field_pairs));
        }

        // Extraer listas
        for (const auto& [k, deque] : shard.list_map) {
            snapshot.lists.emplace_back(k, std::vector<std::string>(deque.begin(), deque.end()));
        }
    }

    return snapshot;
}
