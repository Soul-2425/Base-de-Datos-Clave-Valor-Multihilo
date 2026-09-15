#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstddef>

/**
 * @class MetricsTracker
 * @brief Monitor y rastreador de métricas y telemetría del servidor en tiempo real.
 * 
 * Utiliza variables atómicas (std::atomic) para registrar estadísticas sin bloqueos
 * ni contención entre hilos.
 */
class MetricsTracker {
public:
    static MetricsTracker& instance() {
        static MetricsTracker tracker;
        return tracker;
    }

    void on_client_connected() noexcept {
        total_connections_.fetch_add(1, std::memory_order_relaxed);
        active_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_client_disconnected() noexcept {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }

    void on_command_processed() noexcept {
        total_commands_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_read_op() noexcept {
        total_reads_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_write_op() noexcept {
        total_writes_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_key_expired(std::size_t count = 1) noexcept {
        keys_expired_.fetch_add(count, std::memory_order_relaxed);
    }

    void on_key_evicted(std::size_t count = 1) noexcept {
        keys_evicted_.fetch_add(count, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t uptime_seconds() const noexcept {
        auto now = std::chrono::steady_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count()
        );
    }

    /**
     * @brief Genera la respuesta formateada del comando INFO estilo Redis.
     */
    [[nodiscard]] std::string to_info_string(std::uint16_t port, std::size_t thread_count, std::size_t total_keys) const;

private:
    MetricsTracker() : start_time_(std::chrono::steady_clock::now()) {}

    std::chrono::steady_clock::time_point start_time_;
    std::atomic<std::uint64_t> total_connections_{0};
    std::atomic<std::int64_t> active_connections_{0};
    std::atomic<std::uint64_t> total_commands_{0};
    std::atomic<std::uint64_t> total_reads_{0};
    std::atomic<std::uint64_t> total_writes_{0};
    std::atomic<std::uint64_t> keys_expired_{0};
    std::atomic<std::uint64_t> keys_evicted_{0};
};
