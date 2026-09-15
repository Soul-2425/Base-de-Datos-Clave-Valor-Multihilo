#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "KeyValueStore.hpp"
#include "AofManager.hpp"
#include "MetricsTracker.hpp"
#include "RespParser.hpp"

/**
 * @struct CommandResult
 * @brief Resultado devuelto tras procesar un comando recibido por red.
 */
struct CommandResult {
    std::string response;     // Respuesta serializada en formato RESP2
    bool should_close{false}; // Bandera de terminación de conexión (ej: QUIT)
};

/**
 * @class CommandParser
 * @brief Motor de despacho y ejecución de comandos estilo Redis.
 */
class CommandParser {
public:
    /**
     * @brief Procesa y ejecuta una solicitud entrante.
     * @param raw_command Cadena recibida por red (RESP2 o texto plano).
     * @param store Referencia al KeyValueStore concurrente.
     * @param aof Puntero opcional al gestor de persistencia AOF.
     * @param requirepass Contraseña configurada (si está vacía, no se exige AUTH).
     * @param is_authenticated Estado de autenticación de la sesión del cliente.
     * @param port Puerto del servidor para el reporte INFO.
     * @param thread_count Hilos del servidor para el reporte INFO.
     * @return CommandResult con la respuesta serializada.
     */
    static CommandResult execute(
        std::string_view raw_command,
        KeyValueStore& store,
        AofManager* aof,
        const std::string& requirepass,
        bool& is_authenticated,
        std::uint16_t port = 8080,
        std::size_t thread_count = 4
    );

private:
    static std::string to_upper(std::string_view str);
};
