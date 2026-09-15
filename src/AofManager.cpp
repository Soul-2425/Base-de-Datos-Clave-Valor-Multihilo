#include "AofManager.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>

AofManager::AofManager(std::string filename, bool enabled)
    : filename_(std::move(filename)), enabled_(enabled) {
    if (enabled_) {
        file_.open(filename_, std::ios::out | std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            std::cerr << "[AOF] Advertencia: No se pudo abrir o crear archivo AOF: " << filename_ << std::endl;
        }
    }
}

AofManager::~AofManager() {
    flush_and_close();
}

void AofManager::append(const std::vector<std::string>& tokens) {
    if (!enabled_ || tokens.empty() || !file_.is_open()) {
        return;
    }

    std::lock_guard<std::mutex> lock(file_mutex_);
    file_ << "*" << tokens.size() << "\r\n";
    for (const auto& token : tokens) {
        file_ << "$" << token.size() << "\r\n" << token << "\r\n";
    }
    file_.flush();
}

std::size_t AofManager::load_into_store(KeyValueStore& store) {
    if (!enabled_) {
        return 0;
    }

    std::ifstream infile(filename_, std::ios::in | std::ios::binary);
    if (!infile.is_open()) {
        return 0;
    }

    std::size_t replayed = 0;
    std::string line;

    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        if (line[0] == '*') {
            try {
                std::size_t token_count = std::stoul(line.substr(1));
                std::vector<std::string> tokens;
                tokens.reserve(token_count);

                for (std::size_t i = 0; i < token_count; ++i) {
                    std::string len_line;
                    if (!std::getline(infile, len_line)) break;
                    if (!len_line.empty() && len_line.back() == '\r') len_line.pop_back();

                    if (len_line.empty() || len_line[0] != '$') break;
                    std::size_t token_len = std::stoul(len_line.substr(1));

                    std::string token_val(token_len, '\0');
                    infile.read(&token_val[0], static_cast<std::streamsize>(token_len));

                    char crlf[2];
                    infile.read(crlf, 2);

                    tokens.push_back(std::move(token_val));
                }

                if (tokens.size() == token_count) {
                    const std::string& cmd = tokens[0];
                    if (cmd == "SET" && tokens.size() >= 3) {
                        store.set(tokens[1], tokens[2]);
                    } else if (cmd == "DEL" && tokens.size() >= 2) {
                        for (std::size_t i = 1; i < tokens.size(); ++i) {
                            store.del(tokens[i]);
                        }
                    } else if (cmd == "EXPIRE" && tokens.size() >= 3) {
                        store.expire(tokens[1], std::chrono::seconds(std::stoll(tokens[2])));
                    } else if (cmd == "HSET" && tokens.size() >= 4) {
                        store.hset(tokens[1], tokens[2], tokens[3]);
                    } else if (cmd == "HDEL" && tokens.size() >= 3) {
                        store.hdel(tokens[1], tokens[2]);
                    } else if (cmd == "LPUSH" && tokens.size() >= 3) {
                        for (std::size_t i = 2; i < tokens.size(); ++i) {
                            store.lpush(tokens[1], tokens[i]);
                        }
                    } else if (cmd == "RPUSH" && tokens.size() >= 3) {
                        for (std::size_t i = 2; i < tokens.size(); ++i) {
                            store.rpush(tokens[1], tokens[i]);
                        }
                    } else if (cmd == "FLUSHDB") {
                        store.clear();
                    }
                    ++replayed;
                }
            } catch (...) {
                // Continuar leyendo
            }
        }
    }

    return replayed;
}

bool AofManager::rewrite(const KeyValueStore& store) {
    if (!enabled_) return false;

    std::string tmp_filename = filename_ + ".tmp";
    std::ofstream tmp_file(tmp_filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!tmp_file.is_open()) {
        return false;
    }

    // Obtener instantánea compacta de memoria
    StoreSnapshot snap = store.get_snapshot();

    // 1. Escribir strings
    for (const auto& [k, v] : snap.strings) {
        tmp_file << "*3\r\n$3\r\nSET\r\n$" << k.size() << "\r\n" << k 
                 << "\r\n$" << v.size() << "\r\n" << v << "\r\n";
    }

    // 2. Escribir hashes
    for (const auto& [k, fields] : snap.hashes) {
        for (const auto& [f, v] : fields) {
            tmp_file << "*4\r\n$4\r\nHSET\r\n$" << k.size() << "\r\n" << k 
                     << "\r\n$" << f.size() << "\r\n" << f 
                     << "\r\n$" << v.size() << "\r\n" << v << "\r\n";
        }
    }

    // 3. Escribir listas
    for (const auto& [k, items] : snap.lists) {
        if (!items.empty()) {
            tmp_file << "*" << (2 + items.size()) << "\r\n$5\r\nRPUSH\r\n$" << k.size() << "\r\n" << k << "\r\n";
            for (const auto& item : items) {
                tmp_file << "$" << item.size() << "\r\n" << item << "\r\n";
            }
        }
    }

    tmp_file.flush();
    tmp_file.close();

    // Reemplazo atómico del archivo AOF
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_.close();
    }

    std::error_code ec;
    std::filesystem::rename(tmp_filename, filename_, ec);
    if (ec) {
        // En algunos sistemas Windows el rename falla si el archivo destino existe
        std::filesystem::remove(filename_, ec);
        std::filesystem::rename(tmp_filename, filename_, ec);
    }

    file_.open(filename_, std::ios::out | std::ios::app | std::ios::binary);
    return !ec;
}

void AofManager::flush_and_close() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}
