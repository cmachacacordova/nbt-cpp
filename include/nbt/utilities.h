/**
 * @file utilities.h
 * @author Carlos Machaca (carloscordova96@hotmail.com)
 * @brief Optional SNBT, filesystem and compression helpers (requires ZLIB).
 * @version 0.1
 * @date 2026-09-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "nbt/buffer.h"
#include "nbt/nbt.h"

#include "zlib.h"
#include "zstr.hpp"

namespace nbt {

/** @brief Exception thrown by NbtUtilities for I/O or SNBT errors. */
class UtilException : public NBT_NS BaseException, public std::runtime_error {
public:
  UtilException(const std::string &what) : NBT_NS BaseException(0), std::runtime_error(what) {
  }

  UtilException(const std::string &what, const std::exception &cause) : NBT_NS BaseException(0, std::make_exception_ptr(cause)), std::runtime_error(what) {
  }

  UtilException(const std::string &what, const std::exception_ptr &cause) : NBT_NS BaseException(0, cause), std::runtime_error(what) {
  }
};

/** @brief SNBT parsing/serialization, file I/O and compression helpers. */
class NbtUtilities final {
public:
  /**
   * @brief File compression mode. Auto detects gzip/zlib on input and is invalid for output.
   */
  enum class Compression : std::uint8_t { None, Gzip, Zlib, Auto };

  /**
   * @brief Parse SNBT text into an owning NBT_NS Tag.
   * @param input SNBT text.
   * @return The decoded root tag.
   * @throws NBT_NS Exception on malformed input.
   */
  [[nodiscard]] static NBT_NS Tag parseSnbt(std::string_view input) {
    NBT_NS Options options;
    return parseSnbt(input, options);
  }

  /**
   * @brief Parse SNBT text into an owning NBT_NS Tag with resource limits.
   * @param input SNBT text.
   * @param options Resource limits applied during parsing.
   * @return The decoded root tag.
   * @throws NBT_NS Exception on malformed input or violated limits.
   */
  [[nodiscard]] static NBT_NS Tag parseSnbt(std::string_view input, const NBT_NS Options &options) {
    if (input.size() > options.maxInputBytes) {
      throw NBT_NS Exception("SNBT input byte limit exceeded", 0);
    }
    NBT_NS NbtUtilities::SnbtReader reader(input, options);
    auto value = reader.readValue();
    reader.skipWhitespace();
    if (!reader.finished()) {
      reader.fail("trailing SNBT data");
    }
    return value;
  }

  /**
   * @brief Serialize an owning tag to SNBT text.
   * @param value Tag to serialize.
   * @param pretty Emit newlines and indentation when true.
   * @return The SNBT representation.
   */
  [[nodiscard]] static std::string toSnbt(const NBT_NS Tag &value, bool pretty = false) {
    std::string output;
    writeSnbt(output, {}, value, pretty, 0);
    return output;
  }

  /**
   * @brief Read an NBT file with automatic compression detection.
   * @param path File to read.
   * @return A validated document owning the file bytes.
   * @throws NBT_NS UtilException on I/O failure; NBT_NS Exception on malformed input.
   */

  [[nodiscard]] static NBT_NS NbtParser parseFile(const std::filesystem::path &path) {
    NBT_NS Options options;
    return parseFile(path, NBT_NS NbtUtilities::Compression::Auto, options);
  }

  /**
   * @brief Read an NBT file with an explicit compression mode.
   * @param path File to read.
   * @param compression Compression applied to the file contents.
   * @return A validated document owning the file bytes.
   * @throws NBT_NS UtilException on I/O failure; NBT_NS Exception on malformed input.
   */

  [[nodiscard]] static NBT_NS NbtParser parseFile(const std::filesystem::path &path, NBT_NS NbtUtilities::Compression compression) {
    NBT_NS Options options;
    return parseFile(path, compression, options);
  }

  /**
   * @brief Read an NBT file with explicit compression and resource limits.
   * @param path File to read.
   * @param compression Compression applied to the file contents.
   * @param options Resource limits applied during validation; `named` selects whether
   *                the root tag carries a name.
   * @return A validated document owning the file bytes.
   * @throws NBT_NS UtilException on I/O failure; NBT_NS Exception on malformed input.
   */

  [[nodiscard]] static NBT_NS NbtParser parseFile(const std::filesystem::path &path, NBT_NS NbtUtilities::Compression compression, const NBT_NS Options &options) {
    NBT_NS Buffer buf;

    const auto filename = path.string();

    try {
      std::unique_ptr<std::istream> input;
      if (compression == NBT_NS NbtUtilities::Compression::None) {
        input = std::make_unique<std::ifstream>(filename, std::ios::in | std::ios::binary);
      } else {
        input = std::make_unique<zstr::ifstream>(filename, std::ios::in | std::ios::binary);
      }

      if (input->fail()) {
        throw NBT_NS UtilException("cannot open NBT file");
      }

      constexpr std::size_t chunkSize = 1 << 16;
      std::array<char, chunkSize> buffer;

      while (input->read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input->gcount() > 0) {
        const auto bytesRead = input->gcount();
        buf.append(reinterpret_cast<const std::byte *>(buffer.data()), reinterpret_cast<const std::byte *>(buffer.data() + bytesRead));
        if (buf.size() > options.maxInputBytes) {
          throw NBT_NS Exception("NBT input byte limit exceeded", 0);
        }
      }

      if (input->bad()) {
        throw NBT_NS UtilException("cannot read NBT file");
      }
    } catch (const std::ios_base::failure &e) {
      throw NBT_NS UtilException(e.what(), e);
    } catch (const strict_fstream::Exception &e) {
      throw NBT_NS UtilException(e.what(), e);
    }

    auto document = NBT_NS NbtParser::parse(buf, options);
    return document;
  }

  /**
   * @brief Encode a document and write it to a file, optionally compressed.
   * @param path Destination file.
   * @param document Document to encode.
   * @param compression Output compression; NBT_NS NbtUtilities::Compression::Auto is invalid here.
   * @param named Whether the encoded root tag carries its name.
   * @param level ZLIB compression level.
   * @throws std::invalid_argument on NBT_NS NbtUtilities::Compression::Auto; NBT_NS UtilException on I/O failure.
   */

  static void saveFile(const std::filesystem::path &path, const NBT_NS NbtParser &document, NBT_NS NbtUtilities::Compression compression = NBT_NS NbtUtilities::Compression::None, bool named = true, int level = Z_DEFAULT_COMPRESSION) {
    if (compression == NBT_NS NbtUtilities::Compression::Auto) {
      throw std::invalid_argument("Auto compression is invalid for output");
    }
    std::optional<std::string_view> storedName;
    if (named) {
      storedName = document.nodes_.empty() ? std::string_view{} : document.readName(document.nodes_[0]);
    }
    const auto encoded = document.encode(storedName);
    const auto filename = path.string();

    try {

      std::unique_ptr<strict_fstream::ofstream> file;
      std::unique_ptr<zstr::ostreambuf> buffer;
      std::unique_ptr<std::ostream> output;

      if (compression == NBT_NS NbtUtilities::Compression::None) {
        output = std::make_unique<std::ofstream>(filename, std::ios::out | std::ios::binary);
      } else {
        file = std::make_unique<strict_fstream::ofstream>(filename, std::ios::out | std::ios::binary);
        buffer = std::make_unique<zstr::ostreambuf>(file->rdbuf(), zstr::default_buff_size, level, compression == NBT_NS NbtUtilities::Compression::Gzip ? 31 : 15);
        output = std::make_unique<std::ostream>(buffer.get());
      }

      if (output->fail()) {
        throw NBT_NS UtilException("cannot open NBT file");
      }

      output->write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
      output->flush();
      if (file != nullptr) {
        file->flush();
        if (file->fail()) {
          throw NBT_NS UtilException("cannot write NBT file");
        }
      }
      if (output->fail()) {
        throw NBT_NS UtilException("cannot write NBT file");
      }
    } catch (const std::ios_base::failure &e) {
      throw NBT_NS UtilException(e.what(), e);
    } catch (const strict_fstream::Exception &e) {
      throw NBT_NS UtilException(e.what(), e);
    }
  }

private:
  class SnbtReader {
  public:
    SnbtReader(std::string_view input, const NBT_NS Options &options) : input_(input), options_(options) {
    }

    [[nodiscard]] NBT_NS Tag readValue(std::size_t depth = 0) {
      if (depth > options_.maxDepth) {
        fail("SNBT depth limit exceeded");
      }
      if (totalNodes_ >= options_.maxTotalNodes) {
        fail("SNBT node limit exceeded");
      }
      ++totalNodes_;
      skipWhitespace();
      if (finished()) {
        fail("expected SNBT value");
      }
      if (peek() == '{') {
        return readCompound(depth);
      }
      if (peek() == '[') {
        return readList(depth);
      }
      if (peek() == '"' || peek() == '\'') {
        return NBT_NS Tag(readQuoted());
      }
      return readScalar(readBare());
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
      throw NBT_NS Exception(message, position_);
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
          switch (escaped) {
          case '\\':
          case '\'':
          case '"':
            result.push_back(escaped);
            break;
          case 'n':
            result.push_back('\n');
            break;
          case 't':
            result.push_back('\t');
            break;
          case 'r':
            result.push_back('\r');
            break;
          case 'b':
            result.push_back('\b');
            break;
          case 'f':
            result.push_back('\f');
            break;
          default:
            fail("invalid SNBT escape");
          }
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
        if (std::isspace(static_cast<unsigned char>(character)) != 0 || character == ',' || character == ']' || character == '}' || character == ':') {
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

    [[nodiscard]] NBT_NS Tag readCompound(std::size_t depth) {
      expect('{');
      NBT_NS Tag::Compound values;
      skipWhitespace();
      if (consume('}')) {
        return NBT_NS Tag(std::move(values));
      }
      while (true) {
        if (values.size() >= options_.maxContainerElements) {
          fail("SNBT element limit exceeded");
        }
        auto childName = readName();
        expect(':');
        values.insert_or_assign(std::move(childName), readValue(depth + 1));
        if (consume('}')) {
          break;
        }
        expect(',');
      }
      return NBT_NS Tag(std::move(values));
    }

    [[nodiscard]] NBT_NS Tag readList(std::size_t depth) {
      expect('[');
      skipWhitespace();
      if (position_ + 1 < input_.size() && input_[position_ + 1] == ';' && (peek() == 'B' || peek() == 'I' || peek() == 'L')) {
        return readTypedArray();
      }
      std::vector<NBT_NS Tag> values;
      if (consume(']')) {
        return NBT_NS Tag(std::move(values));
      }
      auto first = readValue(depth + 1);
      const auto elementType = first.type();
      values.push_back(std::move(first));
      while (!consume(']')) {
        expect(',');
        auto value = readValue(depth + 1);
        if (value.type() != elementType) {
          fail("heterogeneous SNBT list");
        }
        values.push_back(std::move(value));
        if (values.size() > options_.maxContainerElements) {
          fail("SNBT element limit exceeded");
        }
      }
      return makeListTag(std::move(values));
    }

    template <class T>
    [[nodiscard]] NBT_NS Tag readTypedArrayImpl() {
      std::vector<T> values;
      readArrayValues(values);
      return NBT_NS Tag(std::move(values));
    }

    [[nodiscard]] NBT_NS Tag readTypedArray() {
      const auto kind = input_[position_++];
      expect(';');
      if (kind == 'B') {
        return readTypedArrayImpl<std::int8_t>();
      }
      if (kind == 'I') {
        return readTypedArrayImpl<std::int32_t>();
      }
      return readTypedArrayImpl<std::int64_t>();
    }

    template <class T>
    void readArrayValues(std::vector<T> &values) {
      if (consume(']')) {
        return;
      }
      while (true) {
        auto token = readBare();
        if (!token.empty() && std::isalpha(static_cast<unsigned char>(token.back()))) {
          const auto suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(token.back())));
          const bool valid = (std::is_same_v<T, std::int8_t> && suffix == 'b') || (std::is_same_v<T, std::int64_t> && suffix == 'l');
          if (!valid) {
            fail("invalid SNBT array element suffix");
          }
          token.pop_back();
        }
        T value{};
        try {
          value = parseNumber<T>(token);
        } catch (const std::exception &) {
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

    [[nodiscard]] NBT_NS Tag readScalar(const std::string &token) const {
      if (token == "true") {
        return NBT_NS Tag(NBT_NS Tag::Byte{1});
      }
      if (token == "false") {
        return NBT_NS Tag(NBT_NS Tag::Byte{0});
      }
      if (token.empty()) {
        fail("empty SNBT scalar");
      }
      const auto suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(token.back())));
      const auto numeric = suffix == 'b' || suffix == 's' || suffix == 'l' || suffix == 'f' || suffix == 'd' ? std::string_view(token).substr(0, token.size() - 1) : std::string_view(token);
      try {
        if (suffix == 'b') {
          return NBT_NS Tag(parseNumber<std::int8_t>(numeric));
        }
        if (suffix == 's') {
          return NBT_NS Tag(parseNumber<std::int16_t>(numeric));
        }
        if (suffix == 'l') {
          return NBT_NS Tag(parseNumber<std::int64_t>(numeric));
        }
        if (suffix == 'f') {
          return NBT_NS Tag(parseNumber<float>(numeric));
        }
        if (suffix == 'd') {
          return NBT_NS Tag(parseNumber<double>(numeric));
        }
        if (token.find_first_of(".eE") != std::string::npos) {
          return NBT_NS Tag(parseNumber<double>(numeric));
        }
        return NBT_NS Tag(parseNumber<std::int32_t>(numeric));
      } catch (const std::exception &) {
        return NBT_NS Tag(std::string_view(token));
      }
    }

    template <class T>
    [[nodiscard]] static T parseNumber(std::string_view text) {
      T value{};
      const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("not a number");
      }
      return value;
    }

    std::string_view input_;
    const NBT_NS Options &options_;
    std::size_t position_{};
    std::size_t totalNodes_{};
  };

  static void appendIndent(std::string &output, std::size_t depth) {
    output.append(depth * 2, ' ');
  }

  static void appendQuoted(std::string &output, std::string_view value) {
    output.push_back('"');
    for (const auto character : value) {
      switch (character) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\t':
        output += "\\t";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      default:
        output.push_back(character);
      }
    }
    output.push_back('"');
  }

  template <class T>
  static void appendNumber(std::string &output, T value, std::string_view suffix = {}) {
    if constexpr (std::is_floating_point_v<T>) {
      std::array<char, 32> buf;
      auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
      if (ec == std::errc{}) {
        output.append(buf.data(), ptr);
      } else {
        output += std::to_string(value);
      }
    } else {
      output += std::to_string(value);
    }
    output.append(suffix);
  }

  [[nodiscard]] static NBT_NS Tag makeListTag(std::vector<NBT_NS Tag> &&values) {
    if (values.empty()) {
      return NBT_NS Tag(std::vector<NBT_NS Tag>{});
    }
    switch (values.front().type()) {
    case NBT_NS Type::Byte: {
      std::vector<NBT_NS Tag::Byte> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Byte>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::Short: {
      std::vector<NBT_NS Tag::Short> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Short>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::Int: {
      std::vector<NBT_NS Tag::Int> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Int>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::Long: {
      std::vector<NBT_NS Tag::Long> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Long>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::Float: {
      std::vector<NBT_NS Tag::Float> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Float>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::Double: {
      std::vector<NBT_NS Tag::Double> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::Double>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    case NBT_NS Type::String: {
      std::vector<NBT_NS Tag::String> typed;
      typed.reserve(values.size());
      for (const auto &tag : values) {
        typed.push_back(std::get<NBT_NS Tag::String>(tag.payload()));
      }
      return NBT_NS Tag(std::move(typed));
    }
    default:
      return NBT_NS Tag(std::move(values));
    }
  }

  static void writeSnbt(std::string &output, std::optional<std::string_view> name, const NBT_NS Tag &value, bool pretty, std::size_t depth) {
    if (name.has_value()) {
      appendQuoted(output, *name);
      output.push_back(':');
      if (pretty) {
        output.push_back(' ');
      }
    }
    switch (value.type()) {
    case NBT_NS Type::Byte:
      appendNumber(output, std::get<std::int8_t>(value.payload()), "b");
      break;
    case NBT_NS Type::Short:
      appendNumber(output, std::get<std::int16_t>(value.payload()), "s");
      break;
    case NBT_NS Type::Int:
      appendNumber(output, std::get<std::int32_t>(value.payload()));
      break;
    case NBT_NS Type::Long:
      appendNumber(output, std::get<std::int64_t>(value.payload()), "L");
      break;
    case NBT_NS Type::Float:
      appendNumber(output, std::get<float>(value.payload()), "f");
      break;
    case NBT_NS Type::Double:
      appendNumber(output, std::get<double>(value.payload()), "d");
      break;
    case NBT_NS Type::String:
      appendQuoted(output, std::get<std::string>(value.payload()));
      break;
    case NBT_NS Type::ByteArray:
      writeArray(output, "B", std::get<std::vector<std::int8_t>>(value.payload()), "b");
      break;
    case NBT_NS Type::IntArray:
      writeArray(output, "I", std::get<std::vector<std::int32_t>>(value.payload()), "");
      break;
    case NBT_NS Type::LongArray:
      writeArray(output, "L", std::get<std::vector<std::int64_t>>(value.payload()), "L");
      break;
    case NBT_NS Type::List: {
      output.push_back('[');
      bool first = true;
      std::visit(
          [&](const auto &values) {
            using V = std::decay_t<decltype(values)>;
            if constexpr (!std::is_same_v<V, NBT_NS Tag::End>) {
              for (const auto &item : values) {
                if (!first) {
                  output += pretty ? ", " : ",";
                }
                first = false;
                if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag>>) {
                  writeSnbt(output, {}, item, pretty, depth + 1);
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Byte>>) {
                  appendNumber(output, item, "b");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Short>>) {
                  appendNumber(output, item, "s");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Int>>) {
                  appendNumber(output, item, "");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Long>>) {
                  appendNumber(output, item, "L");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Float>>) {
                  appendNumber(output, item, "f");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::Double>>) {
                  appendNumber(output, item, "d");
                } else if constexpr (std::is_same_v<V, NBT_NS Tag::Array<NBT_NS Tag::String>>) {
                  appendQuoted(output, item);
                }
              }
            }
          },
          std::get<NBT_NS Tag::List>(value.payload()));
      output.push_back(']');
      break;
    }
    case NBT_NS Type::Compound: {
      output.push_back('{');
      const auto &values = std::get<NBT_NS Tag::Compound>(value.payload());
      std::size_t index = 0;
      for (const auto &[childName, child] : values) {
        if (index++) {
          output.push_back(',');
        }
        if (pretty) {
          output.push_back('\n');
          appendIndent(output, depth + 1);
        }
        writeSnbt(output, std::string_view(childName), child, pretty, depth + 1);
      }
      if (pretty && !values.empty()) {
        output.push_back('\n');
        appendIndent(output, depth);
      }
      output.push_back('}');
      break;
    }
    case NBT_NS Type::End:
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
      appendNumber(output, values[index], suffix);
    }
    output.push_back(']');
  }
};

} // namespace nbt
