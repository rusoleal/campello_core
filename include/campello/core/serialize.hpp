#pragma once

#include "reflect.hpp"
#include <charconv>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace campello::core {

// ------------------------------------------------------------------
// JsonWriter: minimal JSON writer for flat objects
// ------------------------------------------------------------------
class JsonWriter {
public:
    void begin_object() { append('{'); first_ = true; }
    void end_object() { append('}'); first_ = false; }

    void key(std::string_view k) {
        comma();
        append('"');
        append_escaped(k);
        append('"');
        append(':');
        first_ = true; // value is next, so no comma before it
    }

    void null_value() { append("null"); first_ = false; }
    void value(bool v) { append(v ? "true" : "false"); first_ = false; }
    void value(int v) { append(std::to_string(v)); first_ = false; }
    void value(long long v) { append(std::to_string(v)); first_ = false; }
    void value(unsigned v) { append(std::to_string(v)); first_ = false; }
    void value(unsigned long long v) { append(std::to_string(v)); first_ = false; }
    void value(float v) { append(std::to_string(v)); first_ = false; }
    void value(double v) { append(std::to_string(v)); first_ = false; }
    void value(std::string_view v) {
        append('"');
        append_escaped(v);
        append('"');
        first_ = false;
    }

    std::string str() const { return out_; }
    void clear() { out_.clear(); first_ = false; }

private:
    void comma() {
        if (!first_) append(',');
        first_ = false;
    }

    void append(char c) { out_.push_back(c); }
    void append(std::string_view s) { out_.append(s); }

    void append_escaped(std::string_view s) {
        for (char c : s) {
            switch (c) {
                case '"': out_.append("\\\""); break;
                case '\\': out_.append("\\\\"); break;
                case '\b': out_.append("\\b"); break;
                case '\f': out_.append("\\f"); break;
                case '\n': out_.append("\\n"); break;
                case '\r': out_.append("\\r"); break;
                case '\t': out_.append("\\t"); break;
                default: out_.push_back(c); break;
            }
        }
    }

    std::string out_;
    bool first_ = false;
};

// ------------------------------------------------------------------
// TypeSerializerRegistry: maps TypeId -> serializer / deserializer
// ------------------------------------------------------------------
class TypeSerializerRegistry {
public:
    using Serializer = std::function<void(JsonWriter&, const void*)>;
    using Deserializer = std::function<bool(std::string_view, void*)>;

    void register_type(detail::TypeId id, Serializer ser, Deserializer des) {
        serializers_[id] = std::move(ser);
        deserializers_[id] = std::move(des);
    }

    bool has_serializer(detail::TypeId id) const {
        return serializers_.contains(id);
    }

    bool has_deserializer(detail::TypeId id) const {
        return deserializers_.contains(id);
    }

    void serialize(JsonWriter& w, detail::TypeId id, const void* ptr) const {
        auto it = serializers_.find(id);
        if (it != serializers_.end()) {
            it->second(w, ptr);
        } else {
            w.null_value();
        }
    }

    bool deserialize(std::string_view json, detail::TypeId id, void* ptr) const {
        auto it = deserializers_.find(id);
        if (it != deserializers_.end()) {
            return it->second(json, ptr);
        }
        return false;
    }

private:
    std::unordered_map<detail::TypeId, Serializer> serializers_;
    std::unordered_map<detail::TypeId, Deserializer> deserializers_;
};

// Register primitive type serializers
inline void register_primitive_serializers(TypeSerializerRegistry& reg) {
    reg.register_type(
        detail::type_id<bool>(),
        [](JsonWriter& w, const void* p) { w.value(*static_cast<const bool*>(p)); },
        [](std::string_view json, void* p) {
            if (json == "true") { *static_cast<bool*>(p) = true; return true; }
            if (json == "false") { *static_cast<bool*>(p) = false; return true; }
            return false;
        }
    );
    reg.register_type(
        detail::type_id<int>(),
        [](JsonWriter& w, const void* p) { w.value(*static_cast<const int*>(p)); },
        [](std::string_view json, void* p) {
            int v = 0;
            auto [ptr, ec] = std::from_chars(json.data(), json.data() + json.size(), v);
            if (ec == std::errc()) { *static_cast<int*>(p) = v; return true; }
            return false;
        }
    );
    reg.register_type(
        detail::type_id<float>(),
        [](JsonWriter& w, const void* p) { w.value(*static_cast<const float*>(p)); },
        [](std::string_view json, void* p) {
            try { *static_cast<float*>(p) = std::stof(std::string(json)); return true; }
            catch (...) { return false; }
        }
    );
    reg.register_type(
        detail::type_id<double>(),
        [](JsonWriter& w, const void* p) { w.value(*static_cast<const double*>(p)); },
        [](std::string_view json, void* p) {
            try { *static_cast<double*>(p) = std::stod(std::string(json)); return true; }
            catch (...) { return false; }
        }
    );
    reg.register_type(
        detail::type_id<std::string>(),
        [](JsonWriter& w, const void* p) { w.value(*static_cast<const std::string*>(p)); },
        [](std::string_view json, void* p) {
            std::string s(json);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                s = s.substr(1, s.size() - 2);
            }
            *static_cast<std::string*>(p) = std::move(s);
            return true;
        }
    );
}

// ------------------------------------------------------------------
// Component serialization / deserialization
// ------------------------------------------------------------------
inline std::string serialize_component(const ComponentInfo& info, const void* component,
                                       const TypeSerializerRegistry& registry) {
    JsonWriter w;
    w.begin_object();
    for (const auto& prop : info.properties) {
        w.key(prop.name);
        registry.serialize(w, prop.type_id, prop.value_ptr(component));
    }
    w.end_object();
    return w.str();
}

// Minimal flat JSON object parser for deserialization
// Returns a map of key -> raw value string
inline std::unordered_map<std::string, std::string> parse_flat_json(std::string_view json) {
    std::unordered_map<std::string, std::string> result;
    std::size_t i = 0;
    const std::size_t n = json.size();

    auto skip_ws = [&]() {
        while (i < n && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
    };

    auto parse_string = [&]() -> std::string {
        std::string s;
        ++i; // skip opening quote
        while (i < n && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < n) {
                ++i;
                switch (json[i]) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    default: s.push_back(json[i]); break;
                }
            } else {
                s.push_back(json[i]);
            }
            ++i;
        }
        if (i < n && json[i] == '"') ++i; // skip closing quote
        return s;
    };

    auto parse_value = [&]() -> std::string {
        skip_ws();
        std::size_t start = i;
        if (i < n && json[i] == '"') {
            // String value: include quotes for the deserializer to strip
            ++i;
            while (i < n && json[i] != '"') {
                if (json[i] == '\\' && i + 1 < n) i += 2;
                else ++i;
            }
            if (i < n && json[i] == '"') ++i;
            return std::string(json.substr(start, i - start));
        }
        // Primitive: read until comma, }, or whitespace
        while (i < n && json[i] != ',' && json[i] != '}') {
            if (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r') break;
            ++i;
        }
        std::string val(json.substr(start, i - start));
        // Trim trailing whitespace
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\n' || val.back() == '\r'))
            val.pop_back();
        return val;
    };

    skip_ws();
    if (i < n && json[i] == '{') ++i;
    while (i < n) {
        skip_ws();
        if (i < n && json[i] == '}') break;
        if (i < n && json[i] == '"') {
            std::string key = parse_string();
            skip_ws();
            if (i < n && json[i] == ':') ++i;
            std::string val = parse_value();
            result[std::move(key)] = std::move(val);
        }
        skip_ws();
        if (i < n && json[i] == ',') ++i;
    }
    return result;
}

inline bool deserialize_component(const ComponentInfo& info, void* component,
                                  std::string_view json,
                                  const TypeSerializerRegistry& registry) {
    auto values = parse_flat_json(json);
    for (const auto& prop : info.properties) {
        auto it = values.find(prop.name);
        if (it != values.end()) {
            if (!registry.deserialize(it->second, prop.type_id, prop.value_ptr(component))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace campello::core
