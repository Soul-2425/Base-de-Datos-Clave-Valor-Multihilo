#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "KeyValueStore.hpp"
#include "ThreadPool.hpp"
#include "AofManager.hpp"
#include "Config.hpp"

#if defined(_WIN32)
    #include <winsock2.h>
    using socket_fd_t = SOCKET;
#else
    using socket_fd_t = int;
#endif

/**
 * @struct ClientSession
 * @brief Estado individual de cada conexión de cliente.
 */
struct ClientSession {
    socket_fd_t fd;
    std::string buffer;
    bool is_authenticated{false};
};

/**
 * @class TcpServer
 * @brief Servidor TCP reactivo no bloqueante (Reactor Pattern) orientado a resolver C10K.
 * 
 * ARQUITECTURA EVENT-DRIVEN / REACTOR:
 * - Supervisa el socket maestro y todas las conexiones activas mediante sondeo no bloqueante poll().
 * - Las conexiones inactivas (idle) no consumen ningún hilo del ThreadPool.
 * - Los hilos trabajadores del ThreadPool solo se activan cuando un socket tiene bytes disponibles
 *   y un comando listo para ser procesado, maximizando la concurrencia y escalabilidad.
 */
class TcpServer {
public:
    TcpServer(
        const ServerConfig& config,
        KeyValueStore& store,
        ThreadPool& pool,
        AofManager* aof = nullptr
    );

    ~TcpServer();

    // No copiable
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * @brief Inicia el bucle reactivo de eventos.
     */
    void start();

    /**
     * @brief Detiene el servidor y cierra todas las conexiones de forma segura.
     */
    void stop();

    [[nodiscard]] bool is_running() const noexcept {
        return is_running_.load(std::memory_order_acquire);
    }

private:
    bool init_server_socket();
    void set_nonblocking(socket_fd_t fd);
    void close_client(socket_fd_t fd, std::size_t poll_index);
    void process_client_data(socket_fd_t fd, ClientSession& session, std::size_t poll_index);

    ServerConfig config_;
    KeyValueStore& store_;
    ThreadPool& pool_;
    AofManager* aof_{nullptr};

    socket_fd_t server_fd_{-1};
    std::atomic<bool> is_running_{false};

    std::unordered_map<socket_fd_t, ClientSession> sessions_;
};
