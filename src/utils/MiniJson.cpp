#include "utils/MiniJson.hpp"

#include <sstream>
#include <cctype>
#include <cmath>

namespace cppminer::utils::json {

Value Value::makeString(std::string s) {
    Value v; v.type_ = Type::String; v.stringVal_ = std::move(s); return v;
}
Value Value::makeNumber(double n) {
    Value v; v.type_ = Type::Number; v.numberVal_ = n; return v;
}
Value Value::makeBool(bool b) {
    Value v; v.type_ = Type::Bool; v.boolVal_ = b; return v;
}
Value Value::makeArray() {
    Value v; v.type_ = Type::Array; return v;
}
Value Value::makeObject() {
    Value v; v.type_ = Type::Object; return v;
}

std::optional<Value> Value::get(const std::string& key) const {
    if (type_ != Type::Object) return std::nullopt;
    for (const auto& [k, v] : objectVal_) {
        if (k == key) return v;
    }
    return std::nullopt;
}

void Value::set(const std::string& key, Value v) {
    for (auto& [k, existing] : objectVal_) {
        if (k == key) { existing = std::move(v); return; }
    }
    objectVal_.emplace_back(key, std::move(v));
}

void Value::push(Value v) {
    arrayVal_.push_back(std::move(v));
}

namespace {

/// Escapes a string for safe inclusion inside a JSON string literal.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

/// Recursive-descent parser over a fixed input buffer.
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    std::optional<Value> parseValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) return std::nullopt;
        switch (text_[pos_]) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return parseString();
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool consume(char expected) {
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == expected) { ++pos_; return true; }
        return false;
    }

    std::optional<Value> parseObject() {
        if (!consume('{')) return std::nullopt;
        Value obj = Value::makeObject();
        skipWhitespace();
        if (consume('}')) return obj;
        while (true) {
            skipWhitespace();
            auto key = parseRawString();
            if (!key) return std::nullopt;
            if (!consume(':')) return std::nullopt;
            auto val = parseValue();
            if (!val) return std::nullopt;
            obj.set(*key, *val);
            skipWhitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            return std::nullopt;
        }
        return obj;
    }

    std::optional<Value> parseArray() {
        if (!consume('[')) return std::nullopt;
        Value arr = Value::makeArray();
        skipWhitespace();
        if (consume(']')) return arr;
        while (true) {
            auto val = parseValue();
            if (!val) return std::nullopt;
            arr.push(*val);
            skipWhitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            return std::nullopt;
        }
        return arr;
    }

    std::optional<std::string> parseRawString() {
        skipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '"') return std::nullopt;
        ++pos_;
        std::string out;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            char c = text_[pos_];
            if (c == '\\' && pos_ + 1 < text_.size()) {
                char next = text_[pos_ + 1];
                switch (next) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': out += '"';  break;
                    case '\\': out += '\\'; break;
                    default: out += next; break;
                }
                pos_ += 2;
            } else {
                out += c;
                ++pos_;
            }
        }
        if (pos_ >= text_.size()) return std::nullopt; // unterminated string
        ++pos_; // closing quote
        return out;
    }

    std::optional<Value> parseString() {
        auto s = parseRawString();
        if (!s) return std::nullopt;
        return Value::makeString(*s);
    }

    std::optional<Value> parseBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return Value::makeBool(true); }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Value::makeBool(false); }
        return std::nullopt;
    }

    std::optional<Value> parseNull() {
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Value(); }
        return std::nullopt;
    }

    std::optional<Value> parseNumber() {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '-' || text_[pos_] == '+')) {
            ++pos_;
        }
        if (pos_ == start) return std::nullopt;
        try {
            return Value::makeNumber(std::stod(text_.substr(start, pos_ - start)));
        } catch (...) {
            return std::nullopt;
        }
    }
};

} // namespace

std::string Value::dump() const {
    switch (type_) {
        case Type::Null:   return "null";
        case Type::Bool:   return boolVal_ ? "true" : "false";
        case Type::Number: {
            // Print integers without a trailing ".0" (stratum IDs, nonces).
            if (numberVal_ == std::floor(numberVal_)) {
                return std::to_string(static_cast<long long>(numberVal_));
            }
            std::ostringstream oss;
            oss << numberVal_;
            return oss.str();
        }
        case Type::String:
            return "\"" + escape(stringVal_) + "\"";
        case Type::Array: {
            std::string out = "[";
            for (std::size_t i = 0; i < arrayVal_.size(); ++i) {
                if (i) out += ",";
                out += arrayVal_[i].dump();
            }
            return out + "]";
        }
        case Type::Object: {
            std::string out = "{";
            for (std::size_t i = 0; i < objectVal_.size(); ++i) {
                if (i) out += ",";
                out += "\"" + escape(objectVal_[i].first) + "\":" + objectVal_[i].second.dump();
            }
            return out + "}";
        }
    }
    return "null";
}

std::optional<Value> parse(const std::string& text) {
    Parser parser(text);
    return parser.parseValue();
}

} // namespace cppminer::utils::json
