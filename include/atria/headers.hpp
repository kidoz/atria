#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

class Headers {
 public:
  using Entry = std::pair<std::string, std::string>;

  void set(std::string name, std::string value);
  void append(std::string name, std::string value);

  [[nodiscard]] std::optional<std::string_view> find(std::string_view name) const;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

  static bool valid_name(std::string_view name) noexcept;
  static bool valid_value(std::string_view value) noexcept;

 private:
  std::vector<Entry> entries_;
};

}  // namespace atria
