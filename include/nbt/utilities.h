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

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nbt/buffer.h"
#include "nbt/nbt.h"

#include "zlib.h"
#include "zstr.hpp"

namespace nbt {

class UtilException : public nbt::BaseException, public std::runtime_error {
public:
  UtilException(const std::string &what) : BaseException(0), std::runtime_error(what) {
  }

  UtilException(const std::string &what, const std::exception &cause) : BaseException(0, std::make_exception_ptr(cause)), std::runtime_error(what) {
  }

  UtilException(const std::string &what, const std::exception_ptr &cause) : BaseException(0, cause), std::runtime_error(what) {
  }
};

class NbtUtilities final {
public:
  enum class Compression : std::uint8_t { None, Gzip, Zlib, Auto };

  [[nodiscard]] static nbt::Tag parseSnbt(std::string_view input) {
    nbt::Options options;
    return parseSnbt(input, options);
  }

  [[nodiscard]] static nbt::Tag parseSnbt(std::string_view input, const nbt::Options &options) {
    if (input.size() > options.maxInputBytes) {
      throw nbt::Exception("SNBT input byte limit exceeded", 0);
    }
    SnbtReader reader(input, options);
    auto value = reader.readValue({});
    reader.skipWhitespace();
    if (!reader.finished()) {
      reader.fail("trailing SNBT data");
    }
    return value;
  }

  [[nodiscard]] static std::string toSnbt(const nbt::Tag &value, bool pretty = false) {
    std::string output;
    writeSnbt(output, value, pretty, 0, false);
    return output;
  }

  template <typename BufferT = nbt::Buffer>
  [[nodiscard]] static nbt::NbtParser<BufferT> parseFile(const std::filesystem::path &path) {
    nbt::Options options;
    return parseFile(path, Compression::Auto, options);
  }

  template <typename BufferT = nbt::Buffer>
  [[nodiscard]] static nbt::NbtParser<BufferT> parseFile(const std::filesystem::path &path, Compression compression) {
    nbt::Options options;
    return parseFile(path, compression, options);
  }

  template <typename BufferT = nbt::Buffer>
  [[nodiscard]] static nbt::NbtParser<BufferT> parseFile(const std::filesystem::path &path, Compression compression, const nbt::Options &options) {
    BufferT buf;

    const auto filename = path.string();

    try {
      std::unique_ptr<std::istream> input;
      if (compression == Compression::None) {
        input = std::make_unique<std::ifstream>(filename, std::ios::in | std::ios::binary);
      } else {
        input = std::make_unique<zstr::ifstream>(filename, std::ios::in | std::ios::binary);
      }

      if (input->fail()) {
        throw nbt::UtilException("cannot open NBT file");
      }

      constexpr std::size_t chunkSize = 1 << 16;
      std::array<char, chunkSize> buffer;

      while (input->read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
        const auto bytesRead = input->gcount();
        if (bytesRead > 0) {
          buf.append(reinterpret_cast<const std::byte *>(buffer.data()), reinterpret_cast<const std::byte *>(buffer.data() + bytesRead));
        }
      }

      if (input->bad()) {
        throw nbt::UtilException("cannot read NBT file");
      }
    } catch (const std::ios_base::failure &e) {
      throw nbt::UtilException(e.what(), e);
    } catch (const strict_fstream::Exception &e) {
      throw nbt::UtilException(e.what(), e);
    }

    nbt::NbtParser<BufferT> document(buf);
    document.setMaxDepth(options.maxDepth);
    document.setMaxContainerElements(options.maxContainerElements);
    document.setMaxTotalNodes(options.maxTotalNodes);
    document.setMaxInputBytes(options.maxInputBytes);
    document.validate(false, options.named);
    return document;
  }

  template <typename BufferT = nbt::Buffer>
  static void saveFile(const std::filesystem::path &path, const nbt::NbtParser<BufferT> &document, Compression compression = Compression::None, bool named = true, int level = Z_DEFAULT_COMPRESSION) {
    if (compression == Compression::Auto) {
      throw std::invalid_argument("Auto compression is invalid for output");
    }
    const auto encoded = document.encode(named);
    const auto filename = path.string();

    try {

      std::unique_ptr<strict_fstream::ofstream> file;
      std::unique_ptr<zstr::ostreambuf> buffer;
      std::unique_ptr<std::ostream> output;

      if (compression == Compression::None) {
        output = std::make_unique<std::ofstream>(filename, std::ios::out | std::ios::binary);
      } else {
        file = std::make_unique<strict_fstream::ofstream>(filename, std::ios::out | std::ios::binary);
        buffer = std::make_unique<zstr::ostreambuf>(file->rdbuf(), zstr::default_buff_size, level, compression == Compression::Gzip ? 31 : 15);
        output = std::make_unique<std::ostream>(buffer.get());
      }

      if (output->fail()) {
        throw nbt::UtilException("cannot open NBT file");
      }

      output->write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
      output->flush();
      if (file != nullptr) {
        file->flush();
        if (file->fail()) {
          throw nbt::UtilException("cannot write NBT file");
        }
      }
      if (output->fail()) {
        throw nbt::UtilException("cannot write NBT file");
      }
    } catch (const std::ios_base::failure &e) {
      throw nbt::UtilException(e.what(), e);
    } catch (const strict_fstream::Exception &e) {
      throw nbt::UtilException(e.what(), e);
    }
  }

private:
  class SnbtReader {
  public:
    SnbtReader(std::string_view input, const nbt::Options &options) : input_(input), options_(options) {
    }

    [[nodiscard]] nbt::Tag readValue(std::string name, std::size_t depth = 0) {
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
        return readCompound(std::move(name), depth);
      }
      if (peek() == '[') {
        return readList(std::move(name), depth);
      }
      if (peek() == '"' || peek() == '\'') {
        return nbt::Tag(std::move(name), readQuoted());
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
      throw nbt::Exception(message, position_);
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

    [[nodiscard]] nbt::Tag readCompound(std::string name, std::size_t depth) {
      expect('{');
      std::vector<nbt::Tag> values;
      skipWhitespace();
      if (consume('}')) {
        return nbt::Tag(std::move(name), std::vector<Tag>{});
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
      return nbt::Tag(std::move(name), std::move(values));
    }

    [[nodiscard]] nbt::Tag readList(std::string name, std::size_t depth) {
      expect('[');
      skipWhitespace();
      if (position_ + 1 < input_.size() && input_[position_ + 1] == ';' && (peek() == 'B' || peek() == 'I' || peek() == 'L')) {
        return readTypedArray(std::move(name));
      }
      std::vector<nbt::Tag> values;
      if (consume(']')) {
        return nbt::Tag(std::move(name), nbt::Type::End, {});
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
      return nbt::Tag(std::move(name), elementType, std::move(values));
    }

    template <class T>
    [[nodiscard]] nbt::Tag readTypedArrayImpl(std::string name) {
      std::vector<T> values;
      readArrayValues(values);
      return nbt::Tag(std::move(name), std::move(values));
    }

    [[nodiscard]] nbt::Tag readTypedArray(std::string name) {
      const auto kind = input_[position_++];
      expect(';');
      if (kind == 'B') {
        return readTypedArrayImpl<std::int8_t>(std::move(name));
      }
      if (kind == 'I') {
        return readTypedArrayImpl<std::int32_t>(std::move(name));
      }
      return readTypedArrayImpl<std::int64_t>(std::move(name));
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

    [[nodiscard]] nbt::Tag readScalar(std::string name, const std::string &token) {
      if (token == "true") {
        return nbt::Tag(std::move(name), 1);
      }
      if (token == "false") {
        return nbt::Tag(std::move(name), 0);
      }
      if (token.empty()) {
        fail("empty SNBT scalar");
      }
      const auto suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(token.back())));
      const auto numeric = suffix == 'b' || suffix == 's' || suffix == 'l' || suffix == 'f' || suffix == 'd' ? std::string_view(token).substr(0, token.size() - 1) : std::string_view(token);
      try {
        if (suffix == 'b') {
          return nbt::Tag(std::move(name), parseNumber<std::int8_t>(numeric));
        }
        if (suffix == 's') {
          return nbt::Tag(std::move(name), parseNumber<std::int16_t>(numeric));
        }
        if (suffix == 'l') {
          return nbt::Tag(std::move(name), parseNumber<std::int64_t>(numeric));
        }
        if (suffix == 'f') {
          return nbt::Tag(std::move(name), parseNumber<float>(numeric));
        }
        if (suffix == 'd') {
          return nbt::Tag(std::move(name), parseNumber<double>(numeric));
        }
        if (token.find_first_of(".eE") != std::string::npos) {
          return nbt::Tag(std::move(name), parseNumber<double>(numeric));
        }
        return nbt::Tag(std::move(name), parseNumber<std::int32_t>(numeric));
      } catch (const std::exception &) {
        return nbt::Tag(std::move(name), token);
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
    const nbt::Options &options_;
    std::size_t position_{};
    std::size_t totalNodes_{};
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

  template <class T>
  static void appendNumber(std::string &output, T value, std::string_view suffix = {}) {
    output += std::to_string(value);
    output.append(suffix);
  }

  static void writeSnbt(std::string &output, const nbt::Tag &value, bool pretty, std::size_t depth, bool includeName) {
    if (includeName && !value.name.empty()) {
      appendQuoted(output, value.name);
      output.push_back(':');
      if (pretty) {
        output.push_back(' ');
      }
    }
    switch (value.type) {
    case nbt::Type::Byte:
      appendNumber(output, std::get<std::int8_t>(value.payload), "b");
      break;
    case nbt::Type::Short:
      appendNumber(output, std::get<std::int16_t>(value.payload), "s");
      break;
    case nbt::Type::Int:
      appendNumber(output, std::get<std::int32_t>(value.payload));
      break;
    case nbt::Type::Long:
      appendNumber(output, std::get<std::int64_t>(value.payload), "L");
      break;
    case nbt::Type::Float:
      appendNumber(output, std::get<float>(value.payload), "f");
      break;
    case nbt::Type::Double:
      appendNumber(output, std::get<double>(value.payload), "d");
      break;
    case nbt::Type::String:
      appendQuoted(output, std::get<std::string>(value.payload));
      break;
    case nbt::Type::ByteArray:
      writeArray(output, "B", std::get<std::vector<std::int8_t>>(value.payload), "b");
      break;
    case nbt::Type::IntArray:
      writeArray(output, "I", std::get<std::vector<std::int32_t>>(value.payload), "");
      break;
    case nbt::Type::LongArray:
      writeArray(output, "L", std::get<std::vector<std::int64_t>>(value.payload), "L");
      break;
    case nbt::Type::List: {
      output.push_back('[');
      const auto &values = std::get<nbt::Tag::Container>(value.payload);
      for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) {
          output += pretty ? ", " : ",";
        }
        writeSnbt(output, values[index], pretty, depth + 1, false);
      }
      output.push_back(']');
      break;
    }
    case nbt::Type::Compound: {
      output.push_back('{');
      const auto &values = std::get<nbt::Tag::Container>(value.payload);
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
    case nbt::Type::End:
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
