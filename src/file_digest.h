#pragma once
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

// Called only after the SDK's terminal Result_OK, before publishing the staged file.
inline std::string file_sha256(const std::string& path, unsigned long long size,
    const std::function<bool()>& canceled, const std::function<void(int)>& progress) {
  std::ifstream input(path, std::ios::binary);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!input || !context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("Could not start SHA256 verification");
  std::array<char, 1024 * 1024> buffer;
  unsigned long long read = 0; int previous = -1;
  progress(0);
  while (input) {
    if (canceled()) throw std::runtime_error("SHA256 verification canceled");
    input.read(buffer.data(), buffer.size()); const auto count = input.gcount();
    if (count > 0 && EVP_DigestUpdate(context.get(), buffer.data(), size_t(count)) != 1)
      throw std::runtime_error("Could not compute SHA256");
    read += count;
    const int percent = size ? int(100.0L * read / size) : 0;
    if (percent >= previous + 5) { previous = percent; progress(std::min(100, percent)); }
  }
  if (!input.eof() || read != size) throw std::runtime_error("File changed during SHA256 verification");
  if (canceled()) throw std::runtime_error("SHA256 verification canceled");
  unsigned char digest[EVP_MAX_MD_SIZE]; unsigned length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1 || length != 32)
    throw std::runtime_error("Could not finish SHA256 verification");
  const char* hex = "0123456789abcdef"; std::string result;
  for (unsigned i = 0; i < length; ++i) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
  return result;
}
