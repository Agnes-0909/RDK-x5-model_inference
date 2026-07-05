#pragma once

#include <cctype>
#include <exception>
#include <istream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace YAML {

class Exception : public std::runtime_error {
 public:
  explicit Exception(const std::string& message) : std::runtime_error(message) {}
};

class Node {
 public:
  enum class Type { kNull, kScalar, kMap, kSequence };

  Node() = default;
  explicit Node(std::string value) : type_(Type::kScalar), scalar_(std::move(value)) {}

  explicit operator bool() const { return type_ != Type::kNull; }

  const Node& operator[](const std::string& key) const {
    static const Node null_node;
    auto it = map_.find(key);
    return it == map_.end() ? null_node : it->second;
  }

  Node& operator[](const std::string& key) {
    type_ = Type::kMap;
    return map_[key];
  }

  const Node& operator[](size_t index) const {
    static const Node null_node;
    return index < sequence_.size() ? sequence_[index] : null_node;
  }

  template <typename T>
  T as() const {
    std::stringstream ss(scalar_);
    T value{};
    ss >> value;
    if (ss.fail()) {
      throw Exception("failed to convert YAML scalar: " + scalar_);
    }
    return value;
  }

  void setSequence(std::vector<Node> values) {
    type_ = Type::kSequence;
    sequence_ = std::move(values);
  }

 private:
  Type type_ = Type::kNull;
  std::string scalar_;
  std::map<std::string, Node> map_;
  std::vector<Node> sequence_;
};

template <>
inline std::string Node::as<std::string>() const {
  return scalar_;
}

namespace detail {

inline std::string trim(const std::string& input) {
  size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }
  size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(begin, end - begin);
}

inline std::vector<Node> parseSequence(const std::string& text) {
  const auto begin = text.find('[');
  const auto end = text.rfind(']');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    return {};
  }
  std::vector<Node> values;
  std::stringstream ss(text.substr(begin + 1, end - begin - 1));
  std::string item;
  while (std::getline(ss, item, ',')) {
    values.emplace_back(trim(item));
  }
  return values;
}

}  // namespace detail

inline Node Load(std::istream& input) {
  Node root;
  std::string current_map;
  std::string line;
  while (std::getline(input, line)) {
    const auto comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    const auto trimmed = detail::trim(line);
    if (trimmed.empty() || trimmed == "---" || trimmed.rfind("%YAML", 0) == 0) {
      continue;
    }

    const bool nested = !line.empty() && std::isspace(static_cast<unsigned char>(line[0]));
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = detail::trim(line.substr(0, colon));
    std::string value = detail::trim(line.substr(colon + 1));
    const auto tag = value.find("!!");
    if (tag != std::string::npos) {
      value = detail::trim(value.substr(0, tag));
    }

    Node* target = &root;
    if (nested && !current_map.empty()) {
      target = &root[current_map];
    } else if (!nested) {
      current_map = key;
    }

    if (value.empty()) {
      (*target)[key] = Node();
    } else if (value.find('[') != std::string::npos) {
      Node seq;
      seq.setSequence(detail::parseSequence(value));
      (*target)[key] = seq;
    } else {
      (*target)[key] = Node(value);
    }
  }
  return root;
}

}  // namespace YAML
