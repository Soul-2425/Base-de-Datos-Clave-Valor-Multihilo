#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

/**
 * @class RespParser
 * @brief Serializador y deserializador del protocolo RESP2 (REdis Serialization Protocol)
 *        con fallback transparente para texto plano (telnet/netcat).
 */
class RespParser {
public:
    /**
     * @brief Deserializa una solicitud entrante (RESP o texto plano) en tokens de comando.
     * @param input Búfer de texto entrante.
     * @return Vector de argumentos como strings.
     */
    static std::vector<std::string> parse_request(std::string_view input);

    // ==========================================
    // MÉTODOS DE SERIALIZACIÓN ESTÁNDAR RESP2
    // ==========================================

    [[nodiscard]] static std::string serialize_simple_string(std::string_view str);
    [[nodiscard]] static std::string serialize_error(std::string_view err_msg);
    [[nodiscard]] static std::string serialize_integer(std::int64_t num);
    [[nodiscard]] static std::string serialize_bulk_string(std::string_view str);
    [[nodiscard]] static std::string serialize_null();
    [[nodiscard]] static std::string serialize_array(const std::vector<std::string>& items);

private:
    static std::vector<std::string> parse_resp_array(std::string_view input);
    static std::vector<std::string> parse_plain_text(std::string_view input);
};
