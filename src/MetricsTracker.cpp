#include "MetricsTracker.hpp"
#include <sstream>

std::string MetricsTracker::to_info_string(std::uint16_t port, std::size_t thread_count, std::size_t total_keys) const {
    std::ostringstream ss;
    ss << "# Server\r\n"
       << "redis_version:2.0.0-cpp20\r\n"
       << "os:Linux/POSIX\r\n"
       << "arch_bits:64\r\n"
       << "tcp_port:" << port << "\r\n"
       << "uptime_in_seconds:" << uptime_seconds() << "\r\n"
       << "configured_workers:" << thread_count << "\r\n\r\n"
       << "# Clients\r\n"
       << "connected_clients:" << active_connections_.load(std::memory_order_relaxed) << "\r\n"
       << "total_connections_received:" << total_connections_.load(std::memory_order_relaxed) << "\r\n\r\n"
       << "# Stats\r\n"
       << "total_commands_processed:" << total_commands_.load(std::memory_order_relaxed) << "\r\n"
       << "total_reads:" << total_reads_.load(std::memory_order_relaxed) << "\r\n"
       << "total_writes:" << total_writes_.load(std::memory_order_relaxed) << "\r\n"
       << "expired_keys:" << keys_expired_.load(std::memory_order_relaxed) << "\r\n"
       << "evicted_keys:" << keys_evicted_.load(std::memory_order_relaxed) << "\r\n\r\n"
       << "# Keyspace\r\n"
       << "db0:keys=" << total_keys << ",shards=64\r\n";
    return ss.str();
}
