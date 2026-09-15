#include <iostream>
#include <csignal>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

#include "KeyValueStore.hpp"
#include "ThreadPool.hpp"
#include "TcpServer.hpp"
#include "AofManager.hpp"
#include "Config.hpp"
#include "MetricsTracker.hpp"

static TcpServer* g_server_ptr = nullptr;
static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[Main] Señal recibida (" << signal << "). Iniciando parada controlada (Graceful Shutdown)..." << std::endl;
        g_running.store(false, std::memory_order_release);
        if (g_server_ptr) {
            g_server_ptr->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "============================================================" << std::endl;
    std::cout << "       REDIS-STYLE IN-MEMORY KEY-VALUE DATABASE (C++20)     " << std::endl;
    std::cout << "       [Sharded 64x | Event-Driven Reactor | AOF | RESP2]   " << std::endl;
    std::cout << "============================================================" << std::endl;

    // 1. Carga de archivo de configuración
    std::string config_path = "server.conf";
    if (argc > 1 && std::string(argv[1]).find(".conf") != std::string::npos) {
        config_path = argv[1];
    }
    ServerConfig config = ServerConfig::load_from_file(config_path);

    // Sobreescritura opcional por argumentos CLI: ./kv_server [port] [threads]
    if (argc > 1 && std::string(argv[1]).find(".conf") == std::string::npos) {
        try { config.port = static_cast<std::uint16_t>(std::stoi(argv[1])); } catch (...) {}
    }
    if (argc > 2) {
        try { config.thread_count = static_cast<std::size_t>(std::stoi(argv[2])); } catch (...) {}
    }

    // Registrar manejadores de señales del sistema
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 2. Inicialización del Almacén Sharded
    KeyValueStore store;
    std::cout << "[Init] KeyValueStore inicializado con " << KeyValueStore::NUM_SHARDS 
              << " Shards y soporte Reader-Writer Lock." << std::endl;

    // 3. Inicialización y Reproducción de Persistencia AOF
    AofManager aof(config.aof_filename, config.aof_enabled);
    if (config.aof_enabled) {
        std::size_t replayed = aof.load_into_store(store);
        std::cout << "[Init] Persistencia AOF habilitada (" << config.aof_filename 
                  << "): " << replayed << " comandos reproducidos al arranque." << std::endl;
    } else {
        std::cout << "[Init] Persistencia AOF deshabilitada (modo volátil en memoria)." << std::endl;
    }

    // 4. Inicialización del ThreadPool
    ThreadPool pool(config.thread_count);
    std::cout << "[Init] ThreadPool activo con " << pool.thread_count() << " hilos trabajadores." << std::endl;

    // 5. Hilo en segundo plano: Janitor (Expiración activa TTL y desalojo LRU)
    std::thread janitor_thread([&store, &config]() {
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // A. Purga periódica de claves con TTL caducado
            std::size_t purged = store.purge_expired_keys(20);
            if (purged > 0) {
                MetricsTracker::instance().on_key_expired(purged);
            }

            // B. Desalojo LRU si el total de claves excede max_keys
            std::size_t current_size = store.size();
            if (current_size > config.max_keys) {
                std::size_t to_evict = current_size - config.max_keys;
                std::size_t evicted = store.evict_lru_keys(to_evict);
                if (evicted > 0) {
                    MetricsTracker::instance().on_key_evicted(evicted);
                }
            }
        }
    });

    // 6. Configurar e Iniciar Servidor TCP Reactivo
    TcpServer server(config, store, pool, config.aof_enabled ? &aof : nullptr);
    g_server_ptr = &server;

    std::cout << "[Init] Protocolo RESP2 y Texto Plano activos en puerto " << config.port << std::endl;
    if (!config.requirepass.empty()) {
        std::cout << "[Init] Seguridad: Autenticación requerida (requirepass activo)." << std::endl;
    }
    std::cout << "[Init] Conéctate con: redis-cli -p " << config.port 
              << "  o  nc localhost " << config.port << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    // Iniciar bucle de eventos (bloqueante hasta señal de parada)
    server.start();

    // 7. Limpieza final de recursos
    g_running.store(false, std::memory_order_release);
    if (janitor_thread.joinable()) {
        janitor_thread.join();
    }
    aof.flush_and_close();

    std::cout << "[Main] Todos los subsistemas detenidos con éxito. Servidor finalizado." << std::endl;
    return 0;
}
