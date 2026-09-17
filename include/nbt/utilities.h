/**
 * @file utilities.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2026-09-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nbt/nbt.h"

#include "zlib.h"

namespace nbt {

class NbtUtilities final {
public:
  enum class Compression : std::uint8_t { None, Gzip, Zlib, Auto };

  [[nodiscard]] static Nbt::Value parseSnbt(std::string_view input) {
    Nbt::Options options;
    return parseSnbt(input, options);
  }

  [[nodiscard]] static Nbt::Value parseSnbt(std::string_view input, const Nbt::Options &options) {
    SnbtReader reader(input, options);
    auto value = reader.readValue({});
    reader.skipWhitespace();
    if (!reader.finished()) {
      reader.fail("trailing SNBT data");
    }
    return value;
  }

  [[nodiscard]] static std::string toSnbt(const Nbt::Value &value, bool pretty = false) {
    std::string output;
    writeSnbt(output, value, pretty, 0, false);
    return output;
  }

  [[nodiscard]] static Nbt load(const std::filesystem::path &path) {
    Nbt::Options options;
    return load(path, Compression::Auto, options);
  }

  [[nodiscard]] static Nbt load(const std::filesystem::path &path, Compression compression) {
    Nbt::Options options;
    return load(path, compression, options);
  }

  [[nodiscard]] static Nbt load(const std::filesystem::path &path, Compression compression, const Nbt::Options &options) {
    const auto fileBytes = readFile(path);
    const auto resolved = compression == Compression::Auto ? detectCompression(fileBytes) : compression;
    auto bytes = resolved == Compression::None ? fileBytes : inflate(fileBytes, resolved);
    auto owned = std::make_unique<std::byte[]>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), owned.get());
    Nbt document;
    if (document.take(std::move(owned), bytes.size(), options) != Nbt::Status::Complete) {
      throw nbt::Error("truncated NBT file", bytes.size());
    }
    return document;
  }

  static void save(const std::filesystem::path &path, const Nbt &document, Compression compression = Compression::None, Nbt::Format format = Nbt::Format::File, int level = Z_DEFAULT_COMPRESSION) {
    if (compression == Compression::Auto) {
      throw std::invalid_argument("Auto compression is invalid for output");
    }
    const auto encoded = document.encode(format);
    const auto bytes = compression == Compression::None ? encoded : deflate(encoded, compression, level);
    writeFile(path, bytes);
  }

private:
  class SnbtReader {
  public:
    SnbtReader(std::string_view input, const Nbt::Options &options) : input_(input), options_(options) {
    }

    [[nodiscard]] Nbt::Value readValue(std::string name, std::size_t depth = 0) {
      if (depth > options_.maxDepth) {
        fail("SNBT depth limit exceeded");
      }
      skipWhitespace();
      if (finished()) {
        fail("expected SNBT value");
      }
      if (peek() == '{') {
        return readCompound(std::move(name), depth);
      }
      if (peek() == '[') {
        return readList(std::move(name), depth);
      }
      if (peek() == '"' || peek() == '\'') {
        return Nbt::string(std::move(name), readQuoted());
      }
      return readScalar(std::move(name), readBare());
    }

    void skipWhitespace() {
      while (!finished() && std::isspace(static_cast<unsigned char>(peek()))) {
        ++position_;
      }
    }

    [[nodiscard]] bool finished() const noexcept {
      return position_ == input_.size();
    }

    [[noreturn]] void fail(const std::string &message) const {
      throw nbt::Error(message, position_);
    }

  private:
    [[nodiscard]] char peek() const {
      return input_[position_];
    }

    bool consume(char expected) {
      skipWhitespace();
      if (!finished() && peek() == expected) {
        ++position_;
        return true;
      }
      return false;
    }

    void expect(char expected) {
      if (!consume(expected)) {
        fail(std::string("expected '") + expected + "'");
      }
    }

    [[nodiscard]] std::string readQuoted() {
      const auto quote = input_[position_++];
      std::string result;
      while (!finished()) {
        const auto character = input_[position_++];
        if (character == quote) {
          return result;
        }
        if (character == '\\') {
          if (finished()) {
            fail("unfinished SNBT escape");
          }
          const auto escaped = input_[position_++];
          if (escaped != '\\' && escaped != '\'' && escaped != '"') {
            fail("invalid SNBT escape");
          }
          result.push_back(escaped);
        } else {
          result.push_back(character);
        }
      }
      fail("unterminated SNBT string");
    }

    [[nodiscard]] std::string readBare() {
      skipWhitespace();
      const auto begin = position_;
      while (!finished()) {
        const auto character = peek();
        if (std::isspace(static_cast<unsigned char>(character)) || character == ',' || character == ']' || character == '}' || character == ':') {
          break;
        }
        ++position_;
      }
      if (begin == position_) {
        fail("expected SNBT token");
      }
      return std::string(input_.substr(begin, position_ - begin));
    }

    [[nodiscard]] std::string readName() {
      skipWhitespace();
      if (finished()) {
        fail("expected SNBT name");
      }
      return peek() == '"' || peek() == '\'' ? readQuoted() : readBare();
    }

    [[nodiscard]] Nbt::Value readCompound(std::string name, std::size_t depth) {
      expect('{');
      std::vector<Nbt::Value> values;
      skipWhitespace();
      if (consume('}')) {
        return Nbt::compound(std::move(name), {});
      }
      while (true) {
        if (values.size() >= options_.maxContainerElements) {
          fail("SNBT element limit exceeded");
        }
        auto childName = readName();
        expect(':');
        values.push_back(readValue(std::move(childName), depth + 1));
        if (consume('}')) {
          break;
        }
        expect(',');
      }
      return Nbt::compound(std::move(name), std::move(values));
    }

    [[nodiscard]] Nbt::Value readList(std::string name, std::size_t depth) {
      expect('[');
      skipWhitespace();
      if (position_ + 1 < input_.size() && input_[position_ + 1] == ';' && (peek() == 'B' || peek() == 'I' || peek() == 'L')) {
        return readTypedArray(std::move(name));
      }
      std::vector<Nbt::Value> values;
      if (consume(']')) {
        return Nbt::list(std::move(name), Nbt::Type::End, {});
      }
      auto first = readValue({}, depth + 1);
      const auto elementType = first.type;
      values.push_back(std::move(first));
      while (!consume(']')) {
        expect(',');
        auto value = readValue({}, depth + 1);
        if (value.type != elementType) {
          fail("heterogeneous SNBT list");
        }
        values.push_back(std::move(value));
        if (values.size() > options_.maxContainerElements) {
          fail("SNBT element limit exceeded");
        }
      }
      return Nbt::list(std::move(name), elementType, std::move(values));
    }

    [[nodiscard]] Nbt::Value readTypedArray(std::string name) {
      const auto kind = input_[position_++];
      expect(';');
      if (kind == 'B') {
        std::vector<std::int8_t> values;
        readArrayValues(values);
        return Nbt::byteArray(std::move(name), std::move(values));
      }
      if (kind == 'I') {
        std::vector<std::int32_t> values;
        readArrayValues(values);
        return Nbt::intArray(std::move(name), std::move(values));
      }
      std::vector<std::int64_t> values;
      readArrayValues(values);
      return Nbt::longArray(std::move(name), std::move(values));
    }

    template <class T>
    void readArrayValues(std::vector<T> &values) {
      if (consume(']')) {
        return;
      }
      while (true) {
        auto token = readBare();
        if (!token.empty() && std::isalpha(static_cast<unsigned char>(token.back()))) {
          token.pop_back();
        }
        T value{};
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
          fail("invalid SNBT array value");
        }
        values.push_back(value);
        if (values.size() > options_.maxContainerElements) {
          fail("SNBT element limit exceeded");
        }
        if (consume(']')) {
          return;
        }
        expect(',');
      }
    }

    [[nodiscard]] Nbt::Value readScalar(std::string name, const std::string &token) {
      if (token == "true") {
        return Nbt::int8(std::move(name), 1);
      }
      if (token == "false") {
        return Nbt::int8(std::move(name), 0);
      }
      if (token.empty()) {
        fail("empty SNBT scalar");
      }
      const auto suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(token.back())));
      const auto numeric = suffix == 'b' || suffix == 's' || suffix == 'l' || suffix == 'f' || suffix == 'd' ? std::string_view(token).substr(0, token.size() - 1) : std::string_view(token);
      try {
        if (suffix == 'b') {
          return Nbt::int8(std::move(name), parseInteger<std::int8_t>(numeric));
        }
        if (suffix == 's') {
          return Nbt::int16(std::move(name), parseInteger<std::int16_t>(numeric));
        }
        if (suffix == 'l') {
          return Nbt::int64(std::move(name), parseInteger<std::int64_t>(numeric));
        }
        if (suffix == 'f') {
          return Nbt::float32(std::move(name), parseFloat<float>(numeric));
        }
        if (suffix == 'd') {
          return Nbt::float64(std::move(name), parseFloat<double>(numeric));
        }
        if (token.find_first_of(".eE") != std::string::npos) {
          return Nbt::float64(std::move(name), parseFloat<double>(numeric));
        }
        return Nbt::int32(std::move(name), parseInteger<std::int32_t>(numeric));
      } catch (const std::exception &) {
        return Nbt::string(std::move(name), token);
      }
    }

    template <class T>
    [[nodiscard]] static T parseInteger(std::string_view text) {
      T value{};
      const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("not an integer");
      }
      return value;
    }

    template <class T>
    [[nodiscard]] static T parseFloat(std::string_view text) {
      T value{};
      const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("not a number");
      }
      return value;
    }

    std::string_view input_;
    const Nbt::Options &options_;
    std::size_t position_{};
  };

  static void appendIndent(std::string &output, std::size_t depth) {
    output.append(depth * 2, ' ');
  }

  static void appendQuoted(std::string &output, std::string_view value) {
    output.push_back('"');
    for (const auto character : value) {
      if (character == '"' || character == '\\') {
        output.push_back('\\');
      }
      output.push_back(character);
    }
    output.push_back('"');
  }

  static void writeSnbt(std::string &output, const Nbt::Value &value, bool pretty, std::size_t depth, bool includeName) {
    if (includeName && !value.name.empty()) {
      appendQuoted(output, value.name);
      output.push_back(':');
      if (pretty) {
        output.push_back(' ');
      }
    }
    switch (value.type) {
    case Nbt::Type::Byte:
      output += std::to_string(std::get<std::int8_t>(value.payload)) + "b";
      break;
    case Nbt::Type::Short:
      output += std::to_string(std::get<std::int16_t>(value.payload)) + "s";
      break;
    case Nbt::Type::Int:
      output += std::to_string(std::get<std::int32_t>(value.payload));
      break;
    case Nbt::Type::Long:
      output += std::to_string(std::get<std::int64_t>(value.payload)) + "L";
      break;
    case Nbt::Type::Float:
      output += std::to_string(std::get<float>(value.payload)) + "f";
      break;
    case Nbt::Type::Double:
      output += std::to_string(std::get<double>(value.payload)) + "d";
      break;
    case Nbt::Type::String:
      appendQuoted(output, std::get<std::string>(value.payload));
      break;
    case Nbt::Type::ByteArray:
      writeArray(output, "B", std::get<std::vector<std::int8_t>>(value.payload), "b");
      break;
    case Nbt::Type::IntArray:
      writeArray(output, "I", std::get<std::vector<std::int32_t>>(value.payload), "");
      break;
    case Nbt::Type::LongArray:
      writeArray(output, "L", std::get<std::vector<std::int64_t>>(value.payload), "L");
      break;
    case Nbt::Type::List: {
      output.push_back('[');
      const auto &values = std::get<Nbt::List>(value.payload).values;
      for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) {
          output += pretty ? ", " : ",";
        }
        writeSnbt(output, values[index], pretty, depth + 1, false);
      }
      output.push_back(']');
      break;
    }
    case Nbt::Type::Compound: {
      output.push_back('{');
      const auto &values = std::get<Nbt::Compound>(value.payload).values;
      for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) {
          output.push_back(',');
        }
        if (pretty) {
          output.push_back('\n');
          appendIndent(output, depth + 1);
        }
        writeSnbt(output, values[index], pretty, depth + 1, true);
      }
      if (pretty && !values.empty()) {
        output.push_back('\n');
        appendIndent(output, depth);
      }
      output.push_back('}');
      break;
    }
    case Nbt::Type::End:
      output += "null";
      break;
    }
  }

  template <class T>
  static void writeArray(std::string &output, std::string_view kind, const std::vector<T> &values, std::string_view suffix) {
    output.push_back('[');
    output.append(kind);
    output.push_back(';');
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (index) {
        output.push_back(',');
      }
      output += std::to_string(values[index]);
      output.append(suffix);
    }
    output.push_back(']');
  }

  [[nodiscard]] static std::vector<std::byte> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
      throw std::runtime_error("cannot open NBT file");
    }
    const auto end = input.tellg();
    if (end < 0) {
      throw std::runtime_error("cannot determine NBT file size");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
      throw std::runtime_error("cannot read NBT file");
    }
    return bytes;
  }

  static void writeFile(const std::filesystem::path &path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      throw std::runtime_error("cannot write NBT file");
    }
  }

  [[nodiscard]] static Compression detectCompression(std::span<const std::byte> bytes) {
    if (bytes.size() >= 2 && bytes[0] == std::byte{0x1f} && bytes[1] == std::byte{0x8b}) {
      return Compression::Gzip;
    }
    if (bytes.size() >= 2) {
      const auto header = (std::to_integer<unsigned>(bytes[0]) << 8) | std::to_integer<unsigned>(bytes[1]);
      if ((header & 0x0f00U) == 0x0800U && header % 31U == 0) {
        return Compression::Zlib;
      }
    }
    return Compression::None;
  }

  [[nodiscard]] static std::vector<std::byte> inflate(std::span<const std::byte> input, Compression compression) {
    z_stream stream{};
    const auto windowBits = compression == Compression::Gzip ? 15 + 16 : 15;
    if (inflateInit2(&stream, windowBits) != Z_OK) {
      throw std::runtime_error("zlib init failed");
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::vector<std::byte> output;
    std::array<std::byte, 64 * 1024> buffer{};
    int result{};
    do {
      stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
      stream.avail_out = static_cast<uInt>(buffer.size());
      result = ::inflate(&stream, Z_NO_FLUSH);
      if (result != Z_OK && result != Z_STREAM_END) {
        inflateEnd(&stream);
        throw std::runtime_error("invalid compressed NBT data");
      }
      output.insert(output.end(), buffer.begin(), buffer.begin() + (buffer.size() - stream.avail_out));
    } while (result != Z_STREAM_END);
    inflateEnd(&stream);
    return output;
  }

  [[nodiscard]] static std::vector<std::byte> deflate(std::span<const std::byte> input, Compression compression, int level) {
    z_stream stream{};
    const auto windowBits = compression == Compression::Gzip ? 15 + 16 : 15;
    if (deflateInit2(&stream, level, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
      throw std::runtime_error("zlib init failed");
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::vector<std::byte> output;
    std::array<std::byte, 64 * 1024> buffer{};
    int result{};
    do {
      stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
      stream.avail_out = static_cast<uInt>(buffer.size());
      result = ::deflate(&stream, Z_FINISH);
      if (result != Z_OK && result != Z_STREAM_END) {
        deflateEnd(&stream);
        throw std::runtime_error("cannot compress NBT data");
      }
      output.insert(output.end(), buffer.begin(), buffer.begin() + (buffer.size() - stream.avail_out));
    } while (result != Z_STREAM_END);
    deflateEnd(&stream);
    return output;
  }
};

} // namespace nbt
