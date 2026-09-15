#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <deque>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <chrono>
#include <vector>
#include <memory>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

/**
 * @struct StoreEntry
 * @brief Entrada individual para valores de tipo String con TTL y timestamp LRU.
 */
struct StoreEntry {
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> expires_at;
    std::chrono::steady_clock::time_point last_accessed;
};

/**
 * @struct StoreSnapshot
 * @brief Instantánea estructurada para compactación de persistencia AOF (BGREWRITEAOF).
 */
struct StoreSnapshot {
    std::vector<std::pair<std::string, std::string>> strings;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> hashes;
    std::vector<std::pair<std::string, std::vector<std::string>>> lists;
};

/**
 * @struct StoreShard
 * @brief Partición independiente del almacén clave-valor con alineación de caché.
 */
struct alignas(64) StoreShard {
    mutable std::shared_mutex mutex;

    // 1. Almacenamiento de Strings (clave -> StoreEntry)
    std::unordered_map<std::string, StoreEntry> string_map;

    // 2. Almacenamiento de Hashes (clave -> (campo -> valor))
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_map;

    // 3. Almacenamiento de Listas (clave -> deque de strings)
    std::unordered_map<std::string, std::deque<std::string>> list_map;
};

/**
 * @class KeyValueStore
 * @brief Almacén Sharded de alto rendimiento con soporte para Strings, Hashes y Listas.
 */
class KeyValueStore {
public:
    static constexpr std::size_t NUM_SHARDS = 64;

    KeyValueStore();
    ~KeyValueStore() = default;

    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;

    // ==========================================
    // OPERACIONES SOBRE STRINGS Y CONTADORES
    // ==========================================
    void set(const std::string& key, std::string value, std::optional<std::chrono::seconds> ttl_seconds = std::nullopt);
    [[nodiscard]] std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    [[nodiscard]] bool exists(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl_seconds);
    [[nodiscard]] std::int64_t ttl(const std::string& key);
    [[nodiscard]] std::size_t size() const;
    void clear();

    /**
     * @brief Incrementa o decrementa un contador entero de forma atómica.
     * @param key Identificador de la clave.
     * @param delta Cantidad a sumar (positiva o negativa).
     * @return std::pair<bool, int64_t> {true si fue un entero válido, nuevo valor}.
     */
    std::pair<bool, std::int64_t> incr_by(const std::string& key, std::int64_t delta);

    // ==========================================
    // OPERACIONES SOBRE HASHES (DICCIONARIOS)
    // ==========================================
    bool hset(const std::string& key, const std::string& field, std::string value);
    [[nodiscard]] std::optional<std::string> hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    [[nodiscard]] bool hexists(const std::string& key, const std::string& field);
    [[nodiscard]] std::size_t hlen(const std::string& key);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hgetall(const std::string& key);

    // ==========================================
    // OPERACIONES SOBRE LISTAS (DEQUES)
    // ==========================================
    std::size_t lpush(const std::string& key, std::string value);
    std::size_t rpush(const std::string& key, std::string value);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    [[nodiscard]] std::size_t llen(const std::string& key);
    [[nodiscard]] std::vector<std::string> lrange(const std::string& key, std::int64_t start, std::int64_t stop);

    // ==========================================
    // MANTENIMIENTO, LRU Y SNAPSHOT
    // ==========================================
    std::size_t purge_expired_keys(std::size_t max_keys_per_shard = 20);
    std::size_t evict_lru_keys(std::size_t keys_to_evict);
    [[nodiscard]] StoreSnapshot get_snapshot() const;

private:
    [[nodiscard]] inline std::size_t get_shard_index(std::string_view key) const noexcept {
        return std::hash<std::string_view>{}(key) % NUM_SHARDS;
    }

    [[nodiscard]] inline StoreShard& get_shard(std::string_view key) noexcept {
        return shards_[get_shard_index(key)];
    }

    [[nodiscard]] inline const StoreShard& get_shard(std::string_view key) const noexcept {
        return shards_[get_shard_index(key)];
    }

    std::array<StoreShard, NUM_SHARDS> shards_;
};
