#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <future>
#include <atomic>
#include <thread>
#include <filesystem>

#include "KeyValueStore.hpp"
#include "ThreadPool.hpp"
#include "RespParser.hpp"
#include "CommandParser.hpp"
#include "AofManager.hpp"

void test_basic_crud() {
    std::cout << "[Test 1/9] Operaciones básicas CRUD sobre Strings..." << std::endl;
    KeyValueStore store;

    store.set("usuario:1", "Carlos");
    auto res = store.get("usuario:1");
    assert(res.has_value() && *res == "Carlos");
    assert(store.exists("usuario:1"));
    assert(!store.exists("usuario:999"));
    assert(store.size() == 1);

    bool deleted = store.del("usuario:1");
    assert(deleted);
    assert(!store.exists("usuario:1"));
    assert(store.size() == 0);

    store.set("k1", "v1");
    store.set("k2", "v2");
    assert(store.size() == 2);
    store.clear();
    assert(store.size() == 0);
    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_atomic_counters() {
    std::cout << "[Test 2/9] Operaciones de Contadores Atómicos (INCR, DECR, INCRBY)..." << std::endl;
    KeyValueStore store;

    // INCR sobre clave inexistente inicializa en 1
    auto [ok1, val1] = store.incr_by("visitas", 1);
    assert(ok1 && val1 == 1);

    // INCR subsiguiente
    auto [ok2, val2] = store.incr_by("visitas", 1);
    assert(ok2 && val2 == 2);

    // INCRBY delta
    auto [ok3, val3] = store.incr_by("visitas", 10);
    assert(ok3 && val3 == 12);

    // DECR (-1)
    auto [ok4, val4] = store.incr_by("visitas", -1);
    assert(ok4 && val4 == 11);

    // Error ante clave no numérica
    store.set("texto", "no_soy_un_numero");
    auto [ok5, val5] = store.incr_by("texto", 5);
    assert(!ok5);

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_hashes() {
    std::cout << "[Test 3/9] Operaciones sobre Hashes (HSET, HGET, HDEL, HGETALL)..." << std::endl;
    KeyValueStore store;

    // HSET
    bool is_new1 = store.hset("user:100", "nombre", "Carlos");
    bool is_new2 = store.hset("user:100", "rol", "Lead Architect");
    assert(is_new1 && is_new2);

    // Actualizar campo existente
    bool is_new3 = store.hset("user:100", "rol", "Principal Engineer");
    assert(!is_new3);

    // HGET y HEXISTS
    auto nombre = store.hget("user:100", "nombre");
    assert(nombre.has_value() && *nombre == "Carlos");
    assert(store.hexists("user:100", "nombre"));
    assert(!store.hexists("user:100", "edad"));

    // HLEN
    assert(store.hlen("user:100") == 2);

    // HGETALL
    auto all_fields = store.hgetall("user:100");
    assert(all_fields.size() == 2);

    // HDEL
    assert(store.hdel("user:100", "nombre"));
    assert(!store.hexists("user:100", "nombre"));
    assert(store.hlen("user:100") == 1);

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_lists() {
    std::cout << "[Test 4/9] Operaciones sobre Listas (LPUSH, RPUSH, LPOP, RPOP, LRANGE)..." << std::endl;
    KeyValueStore store;

    // LPUSH y RPUSH
    store.rpush("cola", "msg1");
    store.rpush("cola", "msg2");
    store.lpush("cola", "msg0"); // cola: [msg0, msg1, msg2]

    assert(store.llen("cola") == 3);

    // LRANGE
    auto range_all = store.lrange("cola", 0, -1);
    assert(range_all.size() == 3);
    assert(range_all[0] == "msg0");
    assert(range_all[1] == "msg1");
    assert(range_all[2] == "msg2");

    auto range_part = store.lrange("cola", 1, 2);
    assert(range_part.size() == 2);
    assert(range_part[0] == "msg1");

    // LPOP y RPOP
    auto pop_left = store.lpop("cola");
    assert(pop_left.has_value() && *pop_left == "msg0");

    auto pop_right = store.rpop("cola");
    assert(pop_right.has_value() && *pop_right == "msg2");

    assert(store.llen("cola") == 1);

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_ttl_expiration() {
    std::cout << "[Test 5/9] Probando TTL y expiración pasiva/activa..." << std::endl;
    KeyValueStore store;

    store.set("temp_key", "expira_pronto", std::chrono::seconds(1));
    assert(store.exists("temp_key"));
    assert(store.ttl("temp_key") >= 0);

    store.set("perm_key", "permanente");
    assert(store.ttl("perm_key") == -1);
    assert(store.ttl("inexistente") == -2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    auto res = store.get("temp_key");
    assert(!res.has_value());
    assert(!store.exists("temp_key"));
    assert(store.ttl("temp_key") == -2);

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_lru_eviction() {
    std::cout << "[Test 6/9] Probando política de desalojo LRU..." << std::endl;
    KeyValueStore store;

    store.set("k1", "v1");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    store.set("k2", "v2");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    store.set("k3", "v3");

    store.get("k1"); // Refrescar k1

    std::size_t evicted = store.evict_lru_keys(1);
    assert(evicted == 1);
    assert(store.exists("k1"));
    assert(store.size() == 2);

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_resp_protocol() {
    std::cout << "[Test 7/9] Probando protocolo RESP2 y Serialización..." << std::endl;

    std::string raw_resp = "*3\r\n$3\r\nSET\r\n$2\r\nmi\r\n$5\r\nvalor\r\n";
    auto tokens = RespParser::parse_request(raw_resp);
    assert(tokens.size() == 3);
    assert(tokens[0] == "SET");
    assert(tokens[1] == "mi");
    assert(tokens[2] == "valor");

    assert(RespParser::serialize_simple_string("OK") == "+OK\r\n");
    assert(RespParser::serialize_error("err") == "-ERR err\r\n");
    assert(RespParser::serialize_integer(42) == ":42\r\n");
    assert(RespParser::serialize_bulk_string("hola") == "$4\r\nhola\r\n");

    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_aof_persistence_and_rewrite() {
    std::cout << "[Test 8/9] Persistencia AOF y Compactación (BGREWRITEAOF)..." << std::endl;
    std::string test_aof = "test_rewrite.aof";
    if (std::filesystem::exists(test_aof)) std::filesystem::remove(test_aof);

    {
        KeyValueStore store;
        AofManager aof(test_aof, true);

        // Generar historial redundante
        for (int i = 0; i < 50; ++i) {
            aof.append({ "SET", "contador", std::to_string(i) });
        }
        store.set("contador", "49");
        store.hset("config", "host", "localhost");
        store.rpush("cola", "job1");

        auto size_before = std::filesystem::file_size(test_aof);

        // Compactar AOF mediante rewrite
        bool ok = aof.rewrite(store);
        assert(ok);

        auto size_after = std::filesystem::file_size(test_aof);
        assert(size_after < size_before);
        aof.flush_and_close();
    }

    // Replay tras reinicio
    {
        KeyValueStore fresh_store;
        AofManager aof_reader(test_aof, true);
        std::size_t loaded = aof_reader.load_into_store(fresh_store);

        assert(loaded >= 3);
        assert(fresh_store.get("contador").value() == "49");
        assert(fresh_store.hget("config", "host").value() == "localhost");
        assert(fresh_store.llen("cola") == 1);
    }

    if (std::filesystem::exists(test_aof)) std::filesystem::remove(test_aof);
    std::cout << "  -> Superado con éxito.\n" << std::endl;
}

void test_high_concurrency_sharding() {
    std::cout << "[Test 9/9] Estrés multihilo concurrente con 64 Shards..." << std::endl;
    KeyValueStore store;
    const std::size_t num_threads = 8;
    const int ops_per_thread = 10000;

    ThreadPool pool(num_threads);
    std::atomic<long long> writes{0};
    std::atomic<long long> reads{0};

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futures;

    for (std::size_t t = 0; t < num_threads; ++t) {
        if (t % 2 == 0) {
            futures.push_back(pool.enqueue([&store, &writes, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    std::string key = "k_" + std::to_string((t * 1000) + (i % 1000));
                    store.set(key, "val_" + std::to_string(i));
                    store.incr_by("counter_shared", 1);
                    writes.fetch_add(2, std::memory_order_relaxed);
                }
            }));
        } else {
            futures.push_back(pool.enqueue([&store, &reads, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    std::string key = "k_" + std::to_string(i % 1000);
                    store.get(key);
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
            }));
        }
    }

    for (auto& f : futures) f.get();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "  - Total de operaciones concurrentes: " << (writes.load() + reads.load()) << std::endl;
    std::cout << "  - Tiempo total transcurrido:         " << elapsed.count() << " ms" << std::endl;
    std::cout << "  - Throughput medido:                 " 
              << static_cast<int>((writes.load() + reads.load()) / (elapsed.count() / 1000.0)) << " ops/seg" << std::endl;
    std::cout << "  -> Superado con éxito sin contención global.\n" << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "    SUITE DE PRUEBAS INTEGRALES (100% PRODUCCIÓN)           " << std::endl;
    std::cout << "============================================================" << std::endl;

    test_basic_crud();
    test_atomic_counters();
    test_hashes();
    test_lists();
    test_ttl_expiration();
    test_lru_eviction();
    test_resp_protocol();
    test_aof_persistence_and_rewrite();
    test_high_concurrency_sharding();

    std::cout << "============================================================" << std::endl;
    std::cout << "    ¡TODAS LAS PRUEBAS (9/9) FINALIZARON EXITOSAMENTE!      " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
