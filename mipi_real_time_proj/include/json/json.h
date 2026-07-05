#pragma once

#include <cctype>
#include <istream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace Json {

class Value {
 public:
  enum class Type { kNull, kInt, kString, kObject, kArray };

  Value() = default;
  explicit Value(int value) : type_(Type::kInt), int_(value) {}
  explicit Value(std::string value) : type_(Type::kString), string_(std::move(value)) {}

  const Value& operator[](const std::string& key) const {
    static const Value null_value;
    auto it = object_.find(key);
    return it == object_.end() ? null_value : it->second;
  }

  Value& operator[](const std::string& key) {
    type_ = Type::kObject;
    return object_[key];
  }

  const Value& operator[](int index) const {
    static const Value null_value;
    return index >= 0 && static_cast<size_t>(index) < array_.size()
               ? array_[static_cast<size_t>(index)]
               : null_value;
  }

  bool isMember(const std::string& key) const {
    return object_.find(key) != object_.end();
  }

  int asInt() const { return int_; }
  std::string asString() const { return string_; }

  void append(Value value) {
    type_ = Type::kArray;
    array_.push_back(std::move(value));
  }

 private:
  Type type_ = Type::kNull;
  int int_ = 0;
  std::string string_;
  std::map<std::string, Value> object_;
  std::vector<Value> array_;

  friend class Parser;
};

class Parser {
 public:
  explicit Parser(std::string text) : text_(std::move(text)) {}

  Value parse() {
    skipWs();
    return parseValue();
  }

 private:
  Value parseValue() {
    skipWs();
    if (peek() == '{') return parseObject();
    if (peek() == '[') return parseArray();
    if (peek() == '"') return Value(parseString());
    return Value(parseInt());
  }

  Value parseObject() {
    consume('{');
    Value object;
    object.type_ = Value::Type::kObject;
    skipWs();
    while (peek() != '}') {
      std::string key = parseString();
      consume(':');
      object[key] = parseValue();
      skipWs();
      if (peek() == ',') {
        consume(',');
      }
      skipWs();
    }
    consume('}');
    return object;
  }

  Value parseArray() {
    consume('[');
    Value array;
    skipWs();
    while (peek() != ']') {
      array.append(parseValue());
      skipWs();
      if (peek() == ',') {
        consume(',');
      }
      skipWs();
    }
    consume(']');
    return array;
  }

  std::string parseString() {
    consume('"');
    std::string out;
    while (pos_ < text_.size() && text_[pos_] != '"') {
      out.push_back(text_[pos_++]);
    }
    consume('"');
    return out;
  }

  int parseInt() {
    skipWs();
    int sign = 1;
    if (peek() == '-') {
      sign = -1;
      ++pos_;
    }
    int value = 0;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      value = value * 10 + (text_[pos_++] - '0');
    }
    return sign * value;
  }

  void consume(char expected) {
    skipWs();
    if (peek() != expected) {
      throw std::runtime_error("invalid JSON");
    }
    ++pos_;
  }

  char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

  void skipWs() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  std::string text_;
  size_t pos_ = 0;
};

inline std::istream& operator>>(std::istream& input, Value& value) {
  std::string text((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
  value = Parser(text).parse();
  return input;
}

}  // namespace Json
