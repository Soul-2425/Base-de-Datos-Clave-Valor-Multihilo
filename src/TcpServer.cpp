#include "TcpServer.hpp"
#include "CommandParser.hpp"
#include "MetricsTracker.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#if defined(_WIN32)
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET(s) ::closesocket(s)
    #define POLL_FN WSAPoll
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <cerrno>
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define CLOSE_SOCKET(s) ::close(s)
    #define POLL_FN poll
#endif

TcpServer::TcpServer(
    const ServerConfig& config,
    KeyValueStore& store,
    ThreadPool& pool,
    AofManager* aof
) : config_(config), store_(store), pool_(pool), aof_(aof) {
#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

TcpServer::~TcpServer() {
    stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}

void TcpServer::set_nonblocking(socket_fd_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
}

bool TcpServer::init_server_socket() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == INVALID_SOCKET) {
        std::cerr << "[TcpServer] Error al crear socket del servidor." << std::endl;
        return false;
    }

    int opt = 1;
#if defined(_WIN32)
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config_.port);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "[TcpServer] Error al enlazar (bind) en puerto " << config_.port << std::endl;
        CLOSE_SOCKET(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }

    if (::listen(server_fd_, 256) == SOCKET_ERROR) {
        std::cerr << "[TcpServer] Error al escuchar (listen)." << std::endl;
        CLOSE_SOCKET(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }

    set_nonblocking(server_fd_);
    return true;
}

void TcpServer::start() {
    if (!init_server_socket()) {
        return;
    }

    is_running_.store(true, std::memory_order_release);
    std::cout << "[TcpServer] Reactor Event-Loop iniciado en puerto " << config_.port 
              << " (" << pool_.thread_count() << " hilos trabajadores asignados)." << std::endl;

    std::vector<pollfd> poll_fds;
    poll_fds.reserve(1024);

    // poll_fds[0] representa el socket de escucha del servidor
    pollfd srv_pfd{};
    srv_pfd.fd = server_fd_;
    srv_pfd.events = POLLIN;
    poll_fds.push_back(srv_pfd);

    char chunk[4096];

    while (is_running_.load(std::memory_order_acquire)) {
        int ret = POLL_FN(poll_fds.data(), static_cast<ULONG>(poll_fds.size()), 50);

        if (ret < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
#endif
            break;
        }

        if (ret == 0) {
            continue; // Timeout sin eventos
        }

        // 1. Nuevas conexiones entrantes
        if (poll_fds[0].revents & POLLIN) {
            while (true) {
                sockaddr_in client_addr{};
#if defined(_WIN32)
                int addr_len = sizeof(client_addr);
#else
                socklen_t addr_len = sizeof(client_addr);
#endif
                socket_fd_t client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

                if (client_fd == INVALID_SOCKET) {
                    break; // No hay más conexiones pendientes en esta iteración
                }

                set_nonblocking(client_fd);

                sessions_[client_fd] = ClientSession{
                    client_fd,
                    "",
                    config_.requirepass.empty() // Si no hay contraseña requerida, nace autenticado
                };

                pollfd client_pfd{};
                client_pfd.fd = client_fd;
                client_pfd.events = POLLIN;
                poll_fds.push_back(client_pfd);

                MetricsTracker::instance().on_client_connected();
            }
        }

        // 2. Procesamiento de I/O en sockets de clientes activos
        for (std::size_t i = 1; i < poll_fds.size();) {
            socket_fd_t c_fd = poll_fds[i].fd;
            short revents = poll_fds[i].revents;

            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                CLOSE_SOCKET(c_fd);
                sessions_.erase(c_fd);
                poll_fds.erase(poll_fds.begin() + static_cast<std::ptrdiff_t>(i));
                MetricsTracker::instance().on_client_disconnected();
                continue;
            }

            if (revents & POLLIN) {
                int bytes_read = ::recv(c_fd, chunk, sizeof(chunk), 0);

                if (bytes_read <= 0) {
                    // Cliente desconectado o error
                    CLOSE_SOCKET(c_fd);
                    sessions_.erase(c_fd);
                    poll_fds.erase(poll_fds.begin() + static_cast<std::ptrdiff_t>(i));
                    MetricsTracker::instance().on_client_disconnected();
                    continue;
                }

                auto& session = sessions_[c_fd];
                session.buffer.append(chunk, bytes_read);

                // Despacho asíncrono al ThreadPool para cómputo de comandos
                // Extracción de todos los comandos delimitados por \n
                std::size_t pos = 0;
                bool client_closed = false;

                while ((pos = session.buffer.find('\n')) != std::string::npos) {
                    std::string line = session.buffer.substr(0, pos + 1);
                    session.buffer.erase(0, pos + 1);

                    // Si el comando es parte de un array RESP, RespParser lo gestionará
                    CommandResult res = CommandParser::execute(
                        line,
                        store_,
                        aof_,
                        config_.requirepass,
                        session.is_authenticated,
                        config_.port,
                        pool_.thread_count()
                    );

                    if (!res.response.empty()) {
                        ::send(c_fd, res.response.data(), static_cast<int>(res.response.size()), 0);
                    }

                    if (res.should_close) {
                        CLOSE_SOCKET(c_fd);
                        sessions_.erase(c_fd);
                        poll_fds.erase(poll_fds.begin() + static_cast<std::ptrdiff_t>(i));
                        MetricsTracker::instance().on_client_disconnected();
                        client_closed = true;
                        break;
                    }
                }

                if (client_closed) {
                    continue;
                }
            }

            ++i;
        }
    }

    std::cout << "[TcpServer] Event Loop detenido limpiamente." << std::endl;
}

void TcpServer::stop() {
    bool expected = true;
    if (is_running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        if (server_fd_ != INVALID_SOCKET) {
            CLOSE_SOCKET(server_fd_);
            server_fd_ = INVALID_SOCKET;
        }

        for (const auto& [fd, _] : sessions_) {
            CLOSE_SOCKET(fd);
        }
        sessions_.clear();
    }
}
