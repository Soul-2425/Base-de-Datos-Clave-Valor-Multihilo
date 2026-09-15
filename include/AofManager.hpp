#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <cstddef>
#include "KeyValueStore.hpp"

/**
 * @class AofManager
 * @brief Gestor de persistencia en disco mediante Append-Only File (AOF) con compactación atómica.
 */
class AofManager {
public:
    explicit AofManager(std::string filename, bool enabled = true);
    ~AofManager();

    AofManager(const AofManager&) = delete;
    AofManager& operator=(const AofManager&) = delete;

    void append(const std::vector<std::string>& tokens);
    std::size_t load_into_store(KeyValueStore& store);
    void flush_and_close();

    /**
     * @brief Compacta el archivo AOF (BGREWRITEAOF) escribiendo una instantánea mínima
     *        del estado actual en memoria y reemplazando atómicamente el log histórico.
     * @param store Referencia al KeyValueStore.
     * @return true si la compactación fue exitosa, false en caso contrario.
     */
    bool rewrite(const KeyValueStore& store);

    [[nodiscard]] bool is_enabled() const noexcept {
        return enabled_;
    }

private:
    std::string filename_;
    bool enabled_{true};
    std::ofstream file_;
    std::mutex file_mutex_;
};
