#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstdint>
#include <algorithm>

/**
 * @struct ServerConfig
 * @brief Parámetros de configuración del motor de base de datos.
 */
struct ServerConfig {
    std::uint16_t port{8080};
    std::size_t thread_count{std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 4};
    std::string requirepass{""}; // Si está vacío, no se requiere contraseña
    bool aof_enabled{true};
    std::string aof_filename{"database.aof"};
    std::size_t max_keys{500000}; // Límite para política de desalojo LRU

    /**
     * @brief Carga parámetros desde un archivo de configuración estilo redis.conf.
     */
    static ServerConfig load_from_file(const std::string& filepath) {
        ServerConfig config;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return config; // Usar valores por defecto si no existe el archivo
        }

        std::string line;
        while (std::getline(file, line)) {
            // Eliminar espacios al inicio
            line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));

            // Ignorar líneas vacías o comentarios
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream iss(line);
            std::string key, value;
            if (iss >> key >> value) {
                if (key == "port") {
                    config.port = static_cast<std::uint16_t>(std::stoi(value));
                } else if (key == "threads") {
                    config.thread_count = static_cast<std::size_t>(std::stoi(value));
                } else if (key == "requirepass") {
                    config.requirepass = value;
                } else if (key == "aof_enabled") {
                    config.aof_enabled = (value == "yes" || value == "true" || value == "1");
                } else if (key == "aof_filename") {
                    config.aof_filename = value;
                } else if (key == "max_keys") {
                    config.max_keys = static_cast<std::size_t>(std::stoul(value));
                }
            }
        }

        return config;
    }
};
