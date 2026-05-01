#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return {};
  }
  std::string bytes{
      std::istreambuf_iterator<char>{in},
      std::istreambuf_iterator<char>{},
  };
  std::vector<std::uint8_t> out;
  out.reserve(bytes.size());
  for (char byte : bytes) {
    out.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
  }
  return out;
}

void run_file(const std::filesystem::path& path) {
  auto data = read_file(path);
  (void)LLVMFuzzerTestOneInput(data.data(), data.size());
}

void run_path(const std::filesystem::path& path) {
  std::error_code error;
  if (std::filesystem::is_regular_file(path, error)) {
    run_file(path);
    return;
  }
  if (!std::filesystem::is_directory(path, error)) {
    return;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator{path, error}) {
    if (error) {
      return;
    }
    if (entry.is_regular_file(error)) {
      run_file(entry.path());
    }
  }
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc <= 1) {
    std::string bytes{
        std::istreambuf_iterator<char>{std::cin},
        std::istreambuf_iterator<char>{},
    };
    std::vector<std::uint8_t> data;
    data.reserve(bytes.size());
    for (char byte : bytes) {
      data.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }
    return LLVMFuzzerTestOneInput(data.data(), data.size());
  }

  for (int i = 1; i < argc; ++i) {
    std::string_view argument{argv[i]};
    if (!argument.empty() && argument.front() == '-') {
      continue;
    }
    run_path(argument);
  }
  return 0;
} catch (...) {

  return 1;
}
