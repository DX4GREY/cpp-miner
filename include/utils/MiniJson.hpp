#pragma once
//
// MiniJson.hpp
// A deliberately small JSON reader/writer sufficient for Stratum mining
// protocol messages (flat objects, string/number/bool/null scalars, and
// arrays of scalars or nested arrays). This is NOT a general-purpose JSON
// library -- for a production pool integration beyond simple Stratum,
// swap this out for nlohmann::json or rapidjson.
//

#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <optional>

namespace cppminer::utils::json {

class Value;
using ValuePtr = std::shared_ptr<Value>;

/// A single JSON value: null, bool, number, string, array, or object.
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value makeString(std::string s);
    static Value makeNumber(double n);
    static Value makeBool(bool b);
    static Value makeArray();
    static Value makeObject();

    Type type() const noexcept { return type_; }

    // Accessors (throw std::bad_variant_access style behaviour avoided:
    // callers should check type() first, or use the optional getters).
    const std::string& asString() const { return stringVal_; }
    double asNumber() const { return numberVal_; }
    bool asBool() const { return boolVal_; }
    const std::vector<Value>& asArray() const { return arrayVal_; }
    std::vector<Value>& asArray() { return arrayVal_; }

    /// Object field access (returns nullopt if absent or not an object).
    std::optional<Value> get(const std::string& key) const;
    void set(const std::string& key, Value v);
    void push(Value v);

    /// Serializes this value to a compact JSON string.
    std::string dump() const;

private:
    Type type_ = Type::Null;
    std::string stringVal_;
    double numberVal_ = 0.0;
    bool boolVal_ = false;
    std::vector<Value> arrayVal_;
    std::vector<std::pair<std::string, Value>> objectVal_;
};

/// Parses a single JSON value from `text`. Returns std::nullopt on
/// malformed input rather than throwing, since Stratum lines come off
/// the network and must never crash the reader thread.
std::optional<Value> parse(const std::string& text);

} // namespace cppminer::utils::json
