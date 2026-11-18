#include "sha256.hpp"
#include "render.hpp"
#include <fstream>
#include <stdexcept>

namespace apo::detail {
std::string sha256_bytes(std::string_view bytes) {
  Sha256 hash;
  hash.add(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
  return hash.finish();
}
std::string sha256_file(const std::filesystem::path &path, std::stop_token stop,
                        std::uint64_t maximum_bytes) {
  namespace fs = std::filesystem;
  if (!fs::is_regular_file(path) || fs::is_symlink(path))
    throw std::runtime_error("Hash input must be a regular, non-symlink file");
  const auto size = fs::file_size(path);
  if (size > maximum_bytes)
    throw std::invalid_argument("Hash input exceeds its allowed size");
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read hash input");
  Sha256 hash;
  std::array<unsigned char, 65536> bytes{};
  std::uint64_t read = 0;
  while (in) {
    if (stop.stop_requested())
      throw RenderCancelled();
    in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    const auto count = in.gcount();
    if (count > 0) {
      if (std::uint64_t(count) > size - read)
        throw std::runtime_error("Hash input grew while reading");
      hash.add(bytes.data(), std::size_t(count));
      read += std::uint64_t(count);
    }
  }
  if (!in.eof() || read != size || fs::file_size(path) != size)
    throw std::runtime_error("Hash input changed or failed while reading");
  return hash.finish();
}
} // namespace apo::detail
