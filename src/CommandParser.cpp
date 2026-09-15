#include "CommandParser.hpp"
#include <algorithm>
#include <cctype>

std::string CommandParser::to_upper(std::string_view str) {
    std::string res;
    res.reserve(str.size());
    for (char ch : str) {
        res.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return res;
}

CommandResult CommandParser::execute(
    std::string_view raw_command,
    KeyValueStore& store,
    AofManager* aof,
    const std::string& requirepass,
    bool& is_authenticated,
    std::uint16_t port,
    std::size_t thread_count
) {
    std::vector<std::string> tokens = RespParser::parse_request(raw_command);

    if (tokens.empty()) {
        return { "", false };
    }

    std::string cmd = to_upper(tokens[0]);
    MetricsTracker::instance().on_command_processed();

    // 1. Control de Autenticación
    if (!requirepass.empty() && !is_authenticated) {
        if (cmd == "AUTH") {
            if (tokens.size() == 2 && tokens[1] == requirepass) {
                is_authenticated = true;
                return { RespParser::serialize_simple_string("OK"), false };
            }
            return { RespParser::serialize_error("invalid password"), false };
        }
        if (cmd == "QUIT") {
            return { RespParser::serialize_simple_string("OK"), true };
        }
        return { "-NOAUTH Authentication required.\r\n", false };
    }

    // 2. AUTH
    if (cmd == "AUTH") {
        if (tokens.size() == 2) {
            if (requirepass.empty() || tokens[1] == requirepass) {
                is_authenticated = true;
                return { RespParser::serialize_simple_string("OK"), false };
            }
            return { RespParser::serialize_error("invalid password"), false };
        }
        return { RespParser::serialize_error("wrong number of arguments for 'auth' command"), false };
    }

    // 3. PING
    if (cmd == "PING") {
        if (tokens.size() == 1) {
            return { RespParser::serialize_simple_string("PONG"), false };
        } else {
            return { RespParser::serialize_bulk_string(tokens[1]), false };
        }
    }

    // 4. SET <key> <value> [EX seconds]
    if (cmd == "SET") {
        if (tokens.size() < 3) {
            return { RespParser::serialize_error("wrong number of arguments for 'set' command"), false };
        }

        std::optional<std::chrono::seconds> ttl = std::nullopt;
        if (tokens.size() >= 5 && to_upper(tokens[3]) == "EX") {
            try {
                ttl = std::chrono::seconds(std::stoll(tokens[4]));
            } catch (...) {
                return { RespParser::serialize_error("value is not an integer or out of range"), false };
            }
        }

        store.set(tokens[1], tokens[2], ttl);
        MetricsTracker::instance().on_write_op();

        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_simple_string("OK"), false };
    }

    // 5. SETEX <key> <seconds> <value>
    if (cmd == "SETEX") {
        if (tokens.size() != 4) {
            return { RespParser::serialize_error("wrong number of arguments for 'setex' command"), false };
        }

        try {
            auto sec = std::chrono::seconds(std::stoll(tokens[2]));
            store.set(tokens[1], tokens[3], sec);
            MetricsTracker::instance().on_write_op();

            if (aof && aof->is_enabled()) {
                aof->append({ "SET", tokens[1], tokens[3], "EX", tokens[2] });
            }
            return { RespParser::serialize_simple_string("OK"), false };
        } catch (...) {
            return { RespParser::serialize_error("value is not an integer or out of range"), false };
        }
    }

    // 6. GET <key>
    if (cmd == "GET") {
        if (tokens.size() != 2) {
            return { RespParser::serialize_error("wrong number of arguments for 'get' command"), false };
        }

        MetricsTracker::instance().on_read_op();
        auto value = store.get(tokens[1]);
        if (value.has_value()) {
            return { RespParser::serialize_bulk_string(*value), false };
        }
        return { RespParser::serialize_null(), false };
    }

    // 7. MGET <k1> <k2> ...
    if (cmd == "MGET") {
        if (tokens.size() < 2) {
            return { RespParser::serialize_error("wrong number of arguments for 'mget' command"), false };
        }

        std::string res = "*" + std::to_string(tokens.size() - 1) + "\r\n";
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            MetricsTracker::instance().on_read_op();
            auto val = store.get(tokens[i]);
            if (val.has_value()) {
                res += RespParser::serialize_bulk_string(*val);
            } else {
                res += RespParser::serialize_null();
            }
        }
        return { res, false };
    }

    // 8. MSET <k1> <v1> <k2> <v2> ...
    if (cmd == "MSET") {
        if (tokens.size() < 3 || (tokens.size() - 1) % 2 != 0) {
            return { RespParser::serialize_error("wrong number of arguments for 'mset' command"), false };
        }

        for (std::size_t i = 1; i < tokens.size(); i += 2) {
            store.set(tokens[i], tokens[i + 1]);
            MetricsTracker::instance().on_write_op();
        }

        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_simple_string("OK"), false };
    }

    // 9. CONTADORES: INCR / DECR / INCRBY
    if (cmd == "INCR") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'incr' command"), false };
        auto [ok, val] = store.incr_by(tokens[1], 1);
        if (!ok) return { RespParser::serialize_error("value is not an integer or out of range"), false };
        MetricsTracker::instance().on_write_op();
        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(val), false };
    }

    if (cmd == "DECR") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'decr' command"), false };
        auto [ok, val] = store.incr_by(tokens[1], -1);
        if (!ok) return { RespParser::serialize_error("value is not an integer or out of range"), false };
        MetricsTracker::instance().on_write_op();
        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(val), false };
    }

    if (cmd == "INCRBY") {
        if (tokens.size() != 3) return { RespParser::serialize_error("wrong number of arguments for 'incrby' command"), false };
        try {
            std::int64_t delta = std::stoll(tokens[2]);
            auto [ok, val] = store.incr_by(tokens[1], delta);
            if (!ok) return { RespParser::serialize_error("value is not an integer or out of range"), false };
            MetricsTracker::instance().on_write_op();
            if (aof && aof->is_enabled()) aof->append(tokens);
            return { RespParser::serialize_integer(val), false };
        } catch (...) {
            return { RespParser::serialize_error("value is not an integer or out of range"), false };
        }
    }

    // 10. HASHES: HSET, HGET, HDEL, HEXISTS, HLEN, HGETALL
    if (cmd == "HSET") {
        if (tokens.size() != 4) return { RespParser::serialize_error("wrong number of arguments for 'hset' command"), false };
        bool is_new = store.hset(tokens[1], tokens[2], tokens[3]);
        MetricsTracker::instance().on_write_op();
        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(is_new ? 1 : 0), false };
    }

    if (cmd == "HGET") {
        if (tokens.size() != 3) return { RespParser::serialize_error("wrong number of arguments for 'hget' command"), false };
        MetricsTracker::instance().on_read_op();
        auto val = store.hget(tokens[1], tokens[2]);
        if (val.has_value()) return { RespParser::serialize_bulk_string(*val), false };
        return { RespParser::serialize_null(), false };
    }

    if (cmd == "HDEL") {
        if (tokens.size() != 3) return { RespParser::serialize_error("wrong number of arguments for 'hdel' command"), false };
        bool removed = store.hdel(tokens[1], tokens[2]);
        MetricsTracker::instance().on_write_op();
        if (removed && aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(removed ? 1 : 0), false };
    }

    if (cmd == "HEXISTS") {
        if (tokens.size() != 3) return { RespParser::serialize_error("wrong number of arguments for 'hexists' command"), false };
        MetricsTracker::instance().on_read_op();
        return { RespParser::serialize_integer(store.hexists(tokens[1], tokens[2]) ? 1 : 0), false };
    }

    if (cmd == "HLEN") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'hlen' command"), false };
        MetricsTracker::instance().on_read_op();
        return { RespParser::serialize_integer(static_cast<std::int64_t>(store.hlen(tokens[1]))), false };
    }

    if (cmd == "HGETALL") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'hgetall' command"), false };
        MetricsTracker::instance().on_read_op();
        auto pairs = store.hgetall(tokens[1]);
        std::vector<std::string> flat_items;
        flat_items.reserve(pairs.size() * 2);
        for (auto& [f, v] : pairs) {
            flat_items.push_back(std::move(f));
            flat_items.push_back(std::move(v));
        }
        return { RespParser::serialize_array(flat_items), false };
    }

    // 11. LISTAS: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE
    if (cmd == "LPUSH") {
        if (tokens.size() < 3) return { RespParser::serialize_error("wrong number of arguments for 'lpush' command"), false };
        std::size_t new_len = 0;
        for (std::size_t i = 2; i < tokens.size(); ++i) {
            new_len = store.lpush(tokens[1], tokens[i]);
            MetricsTracker::instance().on_write_op();
        }
        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(static_cast<std::int64_t>(new_len)), false };
    }

    if (cmd == "RPUSH") {
        if (tokens.size() < 3) return { RespParser::serialize_error("wrong number of arguments for 'rpush' command"), false };
        std::size_t new_len = 0;
        for (std::size_t i = 2; i < tokens.size(); ++i) {
            new_len = store.rpush(tokens[1], tokens[i]);
            MetricsTracker::instance().on_write_op();
        }
        if (aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(static_cast<std::int64_t>(new_len)), false };
    }

    if (cmd == "LPOP") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'lpop' command"), false };
        auto val = store.lpop(tokens[1]);
        MetricsTracker::instance().on_write_op();
        if (val.has_value()) {
            if (aof && aof->is_enabled()) aof->append(tokens);
            return { RespParser::serialize_bulk_string(*val), false };
        }
        return { RespParser::serialize_null(), false };
    }

    if (cmd == "RPOP") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'rpop' command"), false };
        auto val = store.rpop(tokens[1]);
        MetricsTracker::instance().on_write_op();
        if (val.has_value()) {
            if (aof && aof->is_enabled()) aof->append(tokens);
            return { RespParser::serialize_bulk_string(*val), false };
        }
        return { RespParser::serialize_null(), false };
    }

    if (cmd == "LLEN") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'llen' command"), false };
        MetricsTracker::instance().on_read_op();
        return { RespParser::serialize_integer(static_cast<std::int64_t>(store.llen(tokens[1]))), false };
    }

    if (cmd == "LRANGE") {
        if (tokens.size() != 4) return { RespParser::serialize_error("wrong number of arguments for 'lrange' command"), false };
        try {
            std::int64_t start = std::stoll(tokens[2]);
            std::int64_t stop = std::stoll(tokens[3]);
            MetricsTracker::instance().on_read_op();
            auto items = store.lrange(tokens[1], start, stop);
            return { RespParser::serialize_array(items), false };
        } catch (...) {
            return { RespParser::serialize_error("value is not an integer or out of range"), false };
        }
    }

    // 12. DEL <key> [key ...]
    if (cmd == "DEL") {
        if (tokens.size() < 2) return { RespParser::serialize_error("wrong number of arguments for 'del' command"), false };
        std::int64_t deleted = 0;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            if (store.del(tokens[i])) ++deleted;
        }
        MetricsTracker::instance().on_write_op();
        if (deleted > 0 && aof && aof->is_enabled()) aof->append(tokens);
        return { RespParser::serialize_integer(deleted), false };
    }

    // 13. EXISTS <key>
    if (cmd == "EXISTS") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'exists' command"), false };
        MetricsTracker::instance().on_read_op();
        return { RespParser::serialize_integer(store.exists(tokens[1]) ? 1 : 0), false };
    }

    // 14. EXPIRE & TTL
    if (cmd == "EXPIRE") {
        if (tokens.size() != 3) return { RespParser::serialize_error("wrong number of arguments for 'expire' command"), false };
        try {
            auto sec = std::chrono::seconds(std::stoll(tokens[2]));
            bool ok = store.expire(tokens[1], sec);
            MetricsTracker::instance().on_write_op();
            if (ok && aof && aof->is_enabled()) aof->append(tokens);
            return { RespParser::serialize_integer(ok ? 1 : 0), false };
        } catch (...) {
            return { RespParser::serialize_error("value is not an integer or out of range"), false };
        }
    }

    if (cmd == "TTL") {
        if (tokens.size() != 2) return { RespParser::serialize_error("wrong number of arguments for 'ttl' command"), false };
        MetricsTracker::instance().on_read_op();
        return { RespParser::serialize_integer(store.ttl(tokens[1])), false };
    }

    // 15. DBSIZE & FLUSHDB
    if (cmd == "DBSIZE") {
        return { RespParser::serialize_integer(static_cast<std::int64_t>(store.size())), false };
    }

    if (cmd == "FLUSHDB") {
        store.clear();
        MetricsTracker::instance().on_write_op();
        if (aof && aof->is_enabled()) aof->append({ "FLUSHDB" });
        return { RespParser::serialize_simple_string("OK"), false };
    }

    // 16. BGREWRITEAOF
    if (cmd == "BGREWRITEAOF") {
        if (aof && aof->is_enabled()) {
            bool success = aof->rewrite(store);
            if (success) {
                return { RespParser::serialize_simple_string("Background append only file rewriting finished successfully"), false };
            }
            return { RespParser::serialize_error("rewriting failed"), false };
        }
        return { RespParser::serialize_error("AOF is not enabled"), false };
    }

    // 17. INFO
    if (cmd == "INFO") {
        std::string info = MetricsTracker::instance().to_info_string(port, thread_count, store.size());
        return { RespParser::serialize_bulk_string(info), false };
    }

    // 18. QUIT
    if (cmd == "QUIT") {
        return { RespParser::serialize_simple_string("OK"), true };
    }

    return { RespParser::serialize_error("unknown command '" + tokens[0] + "'"), false };
}
