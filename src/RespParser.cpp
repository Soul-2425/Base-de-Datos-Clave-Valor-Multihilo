#include "RespParser.hpp"
#include <cctype>
#include <sstream>

std::vector<std::string> RespParser::parse_request(std::string_view input) {
    // Eliminar espacios o saltos de línea al inicio
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }

    if (input.empty()) {
        return {};
    }

    // Si comienza con '*', es un array formateado según el protocolo RESP2
    if (input.front() == '*') {
        auto resp_tokens = parse_resp_array(input);
        if (!resp_tokens.empty()) {
            return resp_tokens;
        }
    }

    // Fallback: Si no es RESP válido o es texto plano simple
    return parse_plain_text(input);
}

std::vector<std::string> RespParser::parse_resp_array(std::string_view input) {
    std::vector<std::string> tokens;
    std::size_t pos = 0;

    if (input[pos] != '*') return {};
    ++pos;

    // Leer número de elementos
    std::size_t crlf = input.find("\r\n", pos);
    if (crlf == std::string_view::npos) return {};

    int count = 0;
    try {
        count = std::stoi(std::string(input.substr(pos, crlf - pos)));
    } catch (...) {
        return {};
    }

    pos = crlf + 2;

    for (int i = 0; i < count; ++i) {
        if (pos >= input.size() || input[pos] != '$') {
            return {};
        }
        ++pos;

        std::size_t len_crlf = input.find("\r\n", pos);
        if (len_crlf == std::string_view::npos) return {};

        int len = 0;
        try {
            len = std::stoi(std::string(input.substr(pos, len_crlf - pos)));
        } catch (...) {
            return {};
        }

        pos = len_crlf + 2;
        if (pos + static_cast<std::size_t>(len) > input.size()) {
            return {};
        }

        tokens.emplace_back(input.substr(pos, len));
        pos += len;

        // Saltar \r\n posterior al valor
        if (pos + 1 < input.size() && input[pos] == '\r' && input[pos + 1] == '\n') {
            pos += 2;
        }
    }

    return tokens;
}

std::vector<std::string> RespParser::parse_plain_text(std::string_view input) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';

    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '\r' || c == '\n') {
            continue;
        }

        if ((c == '"' || c == '\'') && (!in_quotes || c == quote_char)) {
            in_quotes = !in_quotes;
            quote_char = in_quotes ? c : '\0';
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }

    return tokens;
}

std::string RespParser::serialize_simple_string(std::string_view str) {
    std::string res;
    res.reserve(str.size() + 3);
    res += "+";
    res += str;
    res += "\r\n";
    return res;
}

std::string RespParser::serialize_error(std::string_view err_msg) {
    std::string res;
    res.reserve(err_msg.size() + 7);
    res += "-ERR ";
    res += err_msg;
    res += "\r\n";
    return res;
}

std::string RespParser::serialize_integer(std::int64_t num) {
    return ":" + std::to_string(num) + "\r\n";
}

std::string RespParser::serialize_bulk_string(std::string_view str) {
    std::string res;
    std::string len_str = std::to_string(str.size());
    res.reserve(1 + len_str.size() + 2 + str.size() + 2);
    res += "$";
    res += len_str;
    res += "\r\n";
    res += str;
    res += "\r\n";
    return res;
}

std::string RespParser::serialize_null() {
    return "$-1\r\n";
}

std::string RespParser::serialize_array(const std::vector<std::string>& items) {
    std::string res = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        res += serialize_bulk_string(item);
    }
    return res;
}
