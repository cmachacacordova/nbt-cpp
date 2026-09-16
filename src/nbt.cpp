#include "nbt/nbt.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <type_traits>

#include "zstr.hpp"

namespace nbt {
namespace {

constexpr int zlibWindowBits = 15;
constexpr int gzipWindowBits = zlibWindowBits + 16;

class Reader {
public:
  Reader(std::istream &stream, const ParseOptions &options, std::vector<Token> *tokens) : stream_(stream), options_(options), tokens_(tokens) {
  }

  Tag root() {
    Tag result;
    if (options_.format == BinaryFormat::File) {
      result = this->named(std::nullopt, 0);
    } else {
      const auto begin = this->position();
      const Type rootType = this->type();
      if (rootType != Type::Compound) {
        fail("Network NBT root must be TAG_Compound");
      }
      const auto token = this->push(rootType, begin, std::nullopt);
      result = payload(rootType, {}, token, 0);
      finish(token, position());
    }
    if (options_.requireCompleteInput && stream_.peek() != std::char_traits<char>::eof()) {
      fail("trailing data");
    }
    return result;
  }

private:
  [[nodiscard]] std::size_t position() const noexcept {
    return position_;
  }

  void read(std::byte *destination, std::size_t size) {
    if (size == 0) {
      return;
    }
    stream_.read(reinterpret_cast<char *>(destination), static_cast<std::streamsize>(size));
    const auto readCount = static_cast<std::size_t>(stream_.gcount());
    position_ += readCount;
    if (readCount != size) {
      throw IncompleteDataError("truncated NBT data", position_);
    }
  }

  template <class T>
  T number() {
    static_assert(std::is_arithmetic_v<T>);

    using U = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    U bits{};

    std::array<std::byte, sizeof(T)> bytes;
    this->read(bytes.data(), sizeof(T));

    for (std::size_t i = 0; i < sizeof(T); ++i) {
      bits = static_cast<U>((bits << 8) | std::to_integer<std::uint8_t>(bytes[i]));
    }

    if constexpr (std::is_floating_point_v<T>) {
      return std::bit_cast<T>(bits);
    } else {
      return static_cast<T>(bits);
    }
  }

  std::string string() {
    const auto length = number<std::uint16_t>();
    std::string result(length, '\0');
    if (length > 0) {
      read(reinterpret_cast<std::byte *>(result.data()), length);
    }
    return result;
  }

  std::size_t count() {
    const auto value = number<std::int32_t>();
    if (value < 0) {
      fail("negative array or list length");
    }
    if (static_cast<std::size_t>(value) > options_.maxElements) {
      fail("element limit exceeded");
    }
    return static_cast<std::size_t>(value);
  }

  Type type() {
    const auto value = this->number<std::uint8_t>();
    if (value > static_cast<unsigned>(Type::LongArray)) {
      this->fail("unknown tag type");
    }
    return static_cast<Type>(value);
  }

  Tag named(std::optional<std::size_t> parent, std::size_t depth) {
    const auto begin = this->position();
    const Type type = this->type();

    if (type == Type::End) {
      this->fail("TAG_End cannot be a named tag");
    }

    const auto token = this->push(type, begin, parent);
    const auto nameBegin = this->position();

    std::string name = this->string();
    this->pushDone(TokenKind::Name, type, nameBegin, this->position(), token);

    Tag result = this->payload(type, std::move(name), token, depth);
    finish(token, position());
    return result;
  }

  Tag payload(Type tagType, std::string name, std::optional<std::size_t> parent, std::size_t depth) {
    if (depth > options_.maxDepth) {
      fail("depth limit exceeded");
    }
    const auto begin = position();
    Tag result;
    switch (tagType) {
    case Type::Byte:
      result = byteTag(std::move(name), number<Byte>());
      break;
    case Type::Short:
      result = shortTag(std::move(name), number<std::int16_t>());
      break;
    case Type::Int:
      result = intTag(std::move(name), number<std::int32_t>());
      break;
    case Type::Long:
      result = longTag(std::move(name), number<std::int64_t>());
      break;
    case Type::Float:
      result = floatTag(std::move(name), number<float>());
      break;
    case Type::Double:
      result = doubleTag(std::move(name), number<double>());
      break;
    case Type::ByteArray: {
      const auto ncount = count();
      ByteArray values(ncount);
      if (ncount > 0) {
        read(reinterpret_cast<std::byte *>(values.data()), ncount);
      }
      result = byteArrayTag(std::move(name), std::move(values));
      break;
    }
    case Type::String:
      result = stringTag(std::move(name), string());
      break;
    case Type::IntArray: {
      const auto ncount = count();
      IntArray values(ncount);
      for (auto &value : values) {
        value = number<std::int32_t>();
      }
      result = intArrayTag(std::move(name), std::move(values));
      break;
    }
    case Type::LongArray: {
      const auto ncount = count();
      LongArray values(ncount);
      for (auto &value : values) {
        value = number<std::int64_t>();
      }
      result = longArrayTag(std::move(name), std::move(values));
      break;
    }
    case Type::List: {
      const Type element = type();
      const auto ncount = count();
      if (element == Type::End && ncount != 0) {
        fail("non-empty TAG_List has TAG_End element type");
      }
      std::vector<Tag> values;
      values.reserve(ncount);
      for (std::size_t i = 0; i < ncount; ++i) {
        const auto childBegin = position();
        const auto child = this->push(element, childBegin, parent);
        values.push_back(payload(element, {}, child, depth + 1));
        finish(child, position());
      }
      result = listTag(std::move(name), element, List{std::move(values)});
      if (parent && (tokens_ != nullptr)) {
        (*tokens_)[*parent].count = static_cast<std::uint32_t>(ncount);
        (*tokens_)[*parent].elementType = element;
      }
      break;
    }
    case Type::Compound: {
      std::vector<Tag> values;
      while (true) {
        const auto next = stream_.peek();
        if (next == std::char_traits<char>::eof()) {
          throw IncompleteDataError("truncated NBT data", position());
        }
        if (next == 0) {
          stream_.get();
          ++position_;
          break;
        }
        if (values.size() >= options_.maxElements) {
          fail("element limit exceeded");
        }
        values.push_back(named(parent, depth + 1));
      }
      result = compoundTag(std::move(name), Compound{std::move(values)});
      break;
    }
    case Type::End:
      fail("unexpected TAG_End");
    }
    pushDone(TokenKind::Payload, tagType, begin, position(), parent);
    return result;
  }

  std::optional<std::size_t> push(Type type, std::size_t begin, std::optional<std::size_t> parent) {
    if (tokens_ == nullptr) {
      return std::nullopt;
    }
    Token token;
    token.kind = TokenKind::Tag;
    token.type = type;
    token.begin = static_cast<std::uint32_t>(begin);
    token.end = token.subtreeEnd = token.begin;
    token.parent = parent ? static_cast<std::uint32_t>(*parent) : Token::noParent;
    tokens_->push_back(token);
    return tokens_->size() - 1;
  }

  void pushDone(TokenKind kind, Type type, std::size_t begin, std::size_t end, std::optional<std::size_t> parent) {
    if (tokens_ != nullptr) {
      Token token;
      token.kind = kind;
      token.type = type;
      token.begin = static_cast<std::uint32_t>(begin);
      token.end = token.subtreeEnd = static_cast<std::uint32_t>(end);
      token.parent = parent ? static_cast<std::uint32_t>(*parent) : Token::noParent;
      tokens_->push_back(token);
    }
  }

  void finish(std::optional<std::size_t> index, std::size_t end) {
    if (index && (tokens_ != nullptr)) {
      (*tokens_)[*index].end = static_cast<std::uint32_t>(end);
      (*tokens_)[*index].subtreeEnd = static_cast<std::uint32_t>(tokens_->size());
    }
  }

  [[noreturn]] void fail(std::string message) const {
    throw Error(std::move(message), position());
  }

  std::istream &stream_;
  const ParseOptions &options_;
  std::vector<Token> *tokens_;
  std::size_t position_{};
};

class Tokenizer {
public:
  Tokenizer(std::span<const std::byte> data, const ParseOptions &options, std::vector<Token> *dynamic, std::span<Token> fixed) : data_(data), options_(options), dynamic_(dynamic), fixed_(fixed) {
    if (data.size() > UINT32_MAX) {
      throw Error("buffers larger than 4 GiB require 64-bit tokens", 0);
    }
  }

  std::size_t run() {
    if (options_.format == BinaryFormat::File) {
      named(std::nullopt, 0);
    } else {
      const auto begin = pos_;
      const auto tagType = type();
      if (tagType != Type::Compound) {
        fail("Network NBT root must be TAG_Compound");
      }
      const auto root = beginToken(TokenKind::Tag, tagType, begin, Token::noParent);
      payload(tagType, root, 0);
      finish(root);
    }
    if (options_.requireCompleteInput && pos_ != data_.size()) {
      fail("trailing data");
    }
    return used_;
  }

private:
  template <class T>
  T number() {
    require(sizeof(T));
    std::uint64_t value{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      value = (value << 8) | std::to_integer<std::uint8_t>(data_[pos_++]);
    }
    return static_cast<T>(value);
  }

  Type type() {
    const auto value = number<std::uint8_t>();
    if (value > static_cast<unsigned>(Type::LongArray)) {
      fail("unknown tag type");
    }
    return static_cast<Type>(value);
  }

  std::size_t count() {
    const auto value = number<std::int32_t>();
    if (value < 0) {
      fail("negative array or list length");
    }
    if (static_cast<std::size_t>(value) > options_.maxElements) {
      fail("element limit exceeded");
    }
    return static_cast<std::size_t>(value);
  }

  void string() {
    const auto ncount = number<std::uint16_t>();
    require(ncount);
    pos_ += ncount;
  }

  void named(std::optional<std::uint32_t> parent, std::size_t depth) {
    const auto begin = pos_;
    const auto tagType = type();
    if (tagType == Type::End) {
      fail("TAG_End cannot be a named tag");
    }
    const auto tag = beginToken(TokenKind::Tag, tagType, begin, parent.value_or(Token::noParent));
    const auto nameBegin = pos_;
    string();
    emit(doneToken(TokenKind::Name, tagType, nameBegin, pos_, tag));
    payload(tagType, tag, depth);
    finish(tag);
  }

  void payload(Type tagType, std::uint32_t parent, std::size_t depth) {
    if (depth > options_.maxDepth) {
      fail("depth limit exceeded");
    }
    const auto begin = pos_;
    switch (tagType) {
    case Type::Byte:
      skip(1);
      break;
    case Type::Short:
      skip(2);
      break;
    case Type::Int:
    case Type::Float:
      skip(4);
      break;
    case Type::Long:
    case Type::Double:
      skip(8);
      break;
    case Type::String:
      string();
      break;
    case Type::ByteArray: {
      const auto ncount = count();
      skip(ncount);
      break;
    }
    case Type::IntArray: {
      const auto ncount = count();
      if (ncount > remaining() / 4) {
        throw IncompleteDataError("truncated int array", pos_);
      }
      skip(ncount * 4);
      break;
    }
    case Type::LongArray: {
      const auto ncount = count();
      if (ncount > remaining() / 8) {
        throw IncompleteDataError("truncated long array", pos_);
      }
      skip(ncount * 8);
      break;
    }
    case Type::List: {
      const auto element = type();
      const auto ncount = count();
      if (element == Type::End && (ncount != 0U)) {
        fail("non-empty TAG_List has TAG_End element type");
      }
      token(parent).count = static_cast<std::uint32_t>(ncount);
      token(parent).elementType = element;
      for (std::size_t i = 0; i < ncount; ++i) {
        const auto child = beginToken(TokenKind::Tag, element, pos_, parent);
        payload(element, child, depth + 1);
        finish(child);
      }
      break;
    }
    case Type::Compound: {
      std::size_t ncount{};
      while (true) {
        require(1);
        if (std::to_integer<std::uint8_t>(data_[pos_]) == 0) {
          ++pos_;
          break;
        }
        if (++ncount > options_.maxElements) {
          fail("element limit exceeded");
        }
        named(parent, depth + 1);
      }
      token(parent).count = static_cast<std::uint32_t>(ncount);
      break;
    }
    case Type::End:
      fail("unexpected TAG_End");
    }
    emit(doneToken(TokenKind::Payload, tagType, begin, pos_, parent));
  }

  Token doneToken(TokenKind kind, Type type, std::size_t begin, std::size_t end, std::uint32_t parent) {
    Token tkn;
    tkn.kind = kind;
    tkn.type = type;
    tkn.begin = static_cast<std::uint32_t>(begin);
    tkn.end = tkn.subtreeEnd = static_cast<std::uint32_t>(end);
    tkn.parent = parent;
    return tkn;
  }

  std::uint32_t beginToken(TokenKind kind, Type type, std::size_t begin, std::uint32_t parent) {
    const auto index = used_;
    emit(doneToken(kind, type, begin, begin, parent));
    return static_cast<std::uint32_t>(index);
  }

  void finish(std::uint32_t index) {
    auto &tkn = token(index);
    tkn.end = static_cast<std::uint32_t>(pos_);
    tkn.subtreeEnd = static_cast<std::uint32_t>(used_);
  }

  void emit(Token value) {
    if (dynamic_ != nullptr) {
      dynamic_->push_back(value);
    } else {
      if (used_ >= fixed_.size()) {
        fail("token buffer exhausted");
      }
      fixed_[used_] = value;
    }
    ++used_;
  }

  Token &token(std::uint32_t index) {
    return (dynamic_ != nullptr) ? (*dynamic_)[index] : fixed_[index];
  }

  void skip(std::size_t n) {
    require(n);
    pos_ += n;
  }

  void require(std::size_t n) {
    if (n > remaining()) {
      throw IncompleteDataError("truncated NBT data", pos_);
    }
  }

  [[nodiscard]] std::size_t remaining() const {
    return data_.size() - pos_;
  }

  [[noreturn]] void fail(std::string message) const {
    throw Error(std::move(message), pos_);
  }

  std::span<const std::byte> data_;
  const ParseOptions &options_;
  std::vector<Token> *dynamic_;
  std::span<Token> fixed_;
  std::size_t pos_{};
  std::size_t used_{};
};

std::size_t encodedTagSize(const Tag &tag, bool includeName) {
  std::size_t result = includeName ? 3 + tag.name.size() : 0;
  switch (tag.type) {
  case Type::Byte:
    return result + 1;
  case Type::Short:
    return result + 2;
  case Type::Int:
  case Type::Float:
    return result + 4;
  case Type::Long:
  case Type::Double:
    return result + 8;
  case Type::String:
    return result + 2 + tag.as<String>().size();
  case Type::ByteArray:
    return result + 4 + tag.as<ByteArray>().size();
  case Type::IntArray:
    return result + 4 + (tag.as<IntArray>().size() * 4);
  case Type::LongArray:
    return result + 4 + (tag.as<LongArray>().size() * 8);
  case Type::List:
    result += 5;
    for (const auto &child : tag.as<List>().values) {
      result += encodedTagSize(child, false);
    }
    return result;
  case Type::Compound:
    for (const auto &child : tag.as<Compound>().values) {
      result += encodedTagSize(child, true);
    }
    return result + 1;
  case Type::End:
    return result;
  }
  return result;
}

class Writer {
public:
  explicit Writer(std::ostream &output) : output_(output) {
  }

  void run(const Tag &root, bool includeName) {
    if (includeName) {
      named(root);
    } else {
      number<std::uint8_t>(static_cast<std::uint8_t>(root.type));
      payload(root);
    }
    if (!output_) {
      throw std::runtime_error("cannot write NBT data");
    }
  }

private:
  template <class T>
  void number(T value) {
    using U = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::conditional_t<sizeof(T) == 2, std::uint16_t, std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    U bits;
    if constexpr (std::is_floating_point_v<T>) {
      bits = std::bit_cast<U>(value);
    } else {
      bits = static_cast<U>(value);
    }
    for (std::size_t i = sizeof(T); i > 0; --i) {
      output_.put(static_cast<char>((bits >> ((i - 1) * 8)) & 0xff));
    }
  }

  void string(std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint16_t>::max()) {
      throw std::invalid_argument("NBT string exceeds 65535 bytes");
    }
    number<std::uint16_t>(static_cast<std::uint16_t>(value.size()));
    output_.write(value.data(), static_cast<std::streamsize>(value.size()));
  }

  void named(const Tag &tag) {
    if (tag.type == Type::End) {
      throw std::invalid_argument("TAG_End cannot be serialized as a named tag");
    }
    number<std::uint8_t>(static_cast<std::uint8_t>(tag.type));
    string(tag.name);
    payload(tag);
  }

  void length(std::size_t n) {
    if (n > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::invalid_argument("NBT collection is too large");
    }
    number<std::int32_t>(static_cast<std::int32_t>(n));
  }

  void payload(const Tag &tag) {
    switch (tag.type) {
    case Type::Byte:
      number(std::get<Byte>(tag.value));
      break;
    case Type::Short:
      number(std::get<std::int16_t>(tag.value));
      break;
    case Type::Int:
      number(std::get<std::int32_t>(tag.value));
      break;
    case Type::Long:
      number(std::get<std::int64_t>(tag.value));
      break;
    case Type::Float:
      number(std::get<float>(tag.value));
      break;
    case Type::Double:
      number(std::get<double>(tag.value));
      break;
    case Type::ByteArray: {
      const auto &vchar = std::get<ByteArray>(tag.value);
      length(vchar.size());
      for (auto xnumber : vchar) {
        number(xnumber);
      }
      break;
    }
    case Type::String:
      string(std::get<std::string>(tag.value));
      break;
    case Type::IntArray: {
      const auto &vint = std::get<IntArray>(tag.value);
      length(vint.size());
      for (auto xint : vint) {
        number(xint);
      }
      break;
    }
    case Type::LongArray: {
      const auto &vll = std::get<LongArray>(tag.value);
      length(vll.size());
      for (auto xll : vll) {
        number(xll);
      }
      break;
    }
    case Type::List: {
      const auto &vtags = std::get<List>(tag.value).values;
      for (const auto &child : vtags) {
        if (child.type != tag.elementType || !child.name.empty()) {
          throw std::invalid_argument("invalid heterogeneous or named TAG_List element");
        }
      }
      number<std::uint8_t>(static_cast<std::uint8_t>(tag.elementType));
      length(vtags.size());
      for (const auto &child : vtags) {
        payload(child);
      }
      break;
    }
    case Type::Compound:
      for (const auto &child : std::get<Compound>(tag.value).values) {
        named(child);
      }
      number<std::uint8_t>(0);
      break;
    case Type::End:
      throw std::invalid_argument("unexpected TAG_End");
    }
  }

  std::ostream &output_;
};

std::vector<Tag> &children(Tag &tag) {
  return tag.type == Type::List ? tag.as<List>().values : tag.as<Compound>().values;
}

const std::vector<Tag> &children(const Tag &tag) {
  return tag.type == Type::List ? tag.as<List>().values : tag.as<Compound>().values;
}

bool container(const Tag &tag) {
  return tag.type == Type::List || tag.type == Type::Compound;
}

std::string escape(std::string_view text) {
  std::ostringstream out;
  out << '"';
  for (unsigned char ctx : text) {
    if (ctx == '"' || ctx == '\\') {
      out << '\\' << ctx;
    } else if (ctx == '\n') {
      out << "\\n";
    } else if (ctx == '\r') {
      out << "\\r";
    } else if (ctx == '\t') {
      out << "\\t";
    } else if (ctx < 0x20) {
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ctx);
    } else {
      out << ctx;
    }
  }
  return out.str() + '"';
}

void snbt(const Tag &tag, std::ostringstream &out, bool pretty, std::size_t depth, bool named) {
  if (named && !tag.name.empty()) {
    out << escape(tag.name) << ':' << (pretty ? " " : "");
  }
  const auto indent = [&](std::size_t depth) {
    if (pretty) {
      out << '\n' << std::string(depth * 2, ' ');
    }
  };
  switch (tag.type) {
  case Type::Byte:
    out << +tag.as<Byte>() << 'b';
    break;
  case Type::Short:
    out << tag.as<std::int16_t>() << 's';
    break;
  case Type::Int:
    out << tag.as<std::int32_t>();
    break;
  case Type::Long:
    out << tag.as<std::int64_t>() << 'L';
    break;
  case Type::Float:
    out << tag.as<float>() << 'f';
    break;
  case Type::Double:
    out << tag.as<double>() << 'd';
    break;
  case Type::String:
    out << escape(tag.as<std::string>());
    break;
  case Type::ByteArray: {
    out << "[B;";
    const auto &vbyte = tag.as<ByteArray>();
    for (size_t i = 0; i < vbyte.size(); ++i) {
      if (i != 0U) {
        out << ',';
      }
      out << +vbyte[i] << 'b';
    }
    out << ']';
    break;
  }
  case Type::IntArray: {
    out << "[I;";
    const auto &vint = tag.as<IntArray>();
    for (size_t i = 0; i < vint.size(); ++i) {
      if (i != 0U) {
        out << ',';
      }
      out << vint[i];
    }
    out << ']';
    break;
  }
  case Type::LongArray: {
    out << "[L;";
    const auto &vll = tag.as<LongArray>();
    for (size_t i = 0; i < vll.size(); ++i) {
      if (i != 0U) {
        out << ',';
      }
      out << vll[i] << 'L';
    }
    out << ']';
    break;
  }
  case Type::List: {
    out << '[';
    const auto &vtags = tag.as<List>().values;
    for (size_t i = 0; i < vtags.size(); ++i) {
      if (i != 0U) {
        out << ',';
      }
      if (pretty) {
        indent(depth + 1);
      }
      snbt(vtags[i], out, pretty, depth + 1, false);
    }
    if (pretty && !vtags.empty()) {
      indent(depth);
    }
    out << ']';
    break;
  }
  case Type::Compound: {
    out << '{';
    const auto &vtags = tag.as<Compound>().values;
    for (size_t i = 0; i < vtags.size(); ++i) {
      if (i != 0U) {
        out << ',';
      }
      if (pretty) {
        indent(depth + 1);
      }
      snbt(vtags[i], out, pretty, depth + 1, true);
    }
    if (pretty && !vtags.empty()) {
      indent(depth);
    }
    out << '}';
    break;
  }
  case Type::End:
    out << "END";
    break;
  }
}

class SnbtParser {
public:
  SnbtParser(std::string_view input, const ParseOptions &options) : input_(input), options_(options) {
  }

  Tag run() {
    space();
    Tag result;
    if (peek() == '\'' || peek() == '"') {
      auto saved = pos_;
      auto name = quoted();
      space();
      if (accept(':')) {
        result = value(std::move(name), 0);
      } else {
        pos_ = saved;
        result = value({}, 0);
      }
    } else {
      result = value({}, 0);
    }
    space();
    if (pos_ != input_.size()) {
      fail("trailing SNBT data");
    }
    return result;
  }

private:
  Tag value(std::string name, std::size_t depth) {
    if (depth > options_.maxDepth) {
      fail("depth limit exceeded");
    }
    space();
    if (pos_ >= input_.size()) {
      fail("expected SNBT value");
    }
    if (peek() == '{') {
      return compound(std::move(name), depth);
    }
    if (peek() == '[') {
      return list(std::move(name), depth);
    }
    if (peek() == '\'' || peek() == '"') {
      return stringTag(std::move(name), quoted());
    }
    auto text = bare();
    if (text.empty()) {
      fail("expected SNBT value");
    }
    return scalar(std::move(name), text);
  }

  Tag compound(std::string name, std::size_t depth) {
    take('{');
    std::vector<Tag> values;
    space();
    if (accept('}')) {
      return compoundTag(std::move(name));
    }
    while (true) {
      if (values.size() >= options_.maxElements) {
        fail("element limit exceeded");
      }
      std::string key = (peek() == '\'' || peek() == '"') ? quoted() : bareKey();
      space();
      take(':');
      values.push_back(value(std::move(key), depth + 1));
      space();
      if (accept('}')) {
        break;
      }
      take(',');
    }
    return compoundTag(std::move(name), Compound{std::move(values)});
  }

  Tag list(std::string name, std::size_t depth) {
    take('[');
    space();
    if (pos_ + 1 < input_.size() && (input_[pos_] == 'B' || input_[pos_] == 'I' || input_[pos_] == 'L') && input_[pos_ + 1] == ';') {
      return typedArray(std::move(name));
    }
    std::vector<Tag> values;
    if (accept(']')) {
      return listTag(std::move(name), Type::End);
    }
    while (true) {
      if (values.size() >= options_.maxElements) {
        fail("element limit exceeded");
      }
      values.push_back(value({}, depth + 1));
      if (values.size() > 1 && values.back().type != values.front().type) {
        fail("SNBT lists must be homogeneous");
      }
      space();
      if (accept(']')) {
        break;
      }
      take(',');
    }
    const auto elementType = values.front().type;
    return listTag(std::move(name), elementType, List{std::move(values)});
  }

  Tag typedArray(std::string name) {
    char kind = input_[pos_];
    pos_ += 2;
    space();
    if (kind == 'B') {
      ByteArray out;
      if (accept(']')) {
        return byteArrayTag(std::move(name), {});
      }
      while (true) {
        auto tagVar = scalar({}, bare());
        if (tagVar.type != Type::Byte) {
          fail("TAG_Byte_Array requires byte values");
        }
        out.push_back(tagVar.as<Byte>());
        space();
        if (accept(']')) {
          break;
        }
        take(',');
      }
      return byteArrayTag(std::move(name), std::move(out));
    }
    if (kind == 'I') {
      IntArray out;
      if (accept(']')) {
        return intArrayTag(std::move(name), {});
      }
      while (true) {
        auto tVar = scalar({}, bare());
        if (tVar.type != Type::Int) {
          fail("TAG_Int_Array requires int values");
        }
        out.push_back(tVar.as<std::int32_t>());
        space();
        if (accept(']')) {
          break;
        }
        take(',');
      }
      return intArrayTag(std::move(name), std::move(out));
    }
    LongArray out;
    if (accept(']')) {
      return longArrayTag(std::move(name), {});
    }
    while (true) {
      auto tVar = scalar({}, bare());
      if (tVar.type != Type::Long) {
        fail("TAG_Long_Array requires long values");
      }
      out.push_back(tVar.as<std::int64_t>());
      space();
      if (accept(']')) {
        break;
      }
      take(',');
    }
    return longArrayTag(std::move(name), std::move(out));
  }

  Tag scalar(std::string name, std::string_view text) {
    try {
      if (text == "true") {
        return byteTag(std::move(name), 1);
      }
      if (text == "false") {
        return byteTag(std::move(name), 0);
      }
      char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
      auto body = text;
      if (suffix == 'b' || suffix == 's' || suffix == 'l' || suffix == 'f' || suffix == 'd') {
        body.remove_suffix(1);
      }
      std::string copy(body);
      std::size_t used{};
      if (suffix == 'f') {
        return floatTag(std::move(name), std::stof(copy, &used));
      }
      if (suffix == 'd') {
        return doubleTag(std::move(name), std::stod(copy, &used));
      }
      if (copy.find_first_of(".eE") != std::string::npos) {
        return doubleTag(std::move(name), std::stod(copy, &used));
      }
      auto number = std::stoll(copy, &used, 10);
      if (used != copy.size()) {
        return stringTag(std::move(name), std::string(text));
      }
      if (suffix == 'b') {
        if (number < INT8_MIN || number > INT8_MAX) {
          fail("byte out of range");
        }
        return byteTag(std::move(name), static_cast<Byte>(number));
      }
      if (suffix == 's') {
        if (number < INT16_MIN || number > INT16_MAX) {
          fail("short out of range");
        }
        return shortTag(std::move(name), static_cast<std::int16_t>(number));
      }
      if (suffix == 'l') {
        return longTag(std::move(name), number);
      }
      if (number < INT32_MIN || number > INT32_MAX) {
        fail("int out of range");
      }
      return intTag(std::move(name), static_cast<std::int32_t>(number));
    } catch (const std::invalid_argument &) {
      return stringTag(std::move(name), std::string(text));
    } catch (const std::out_of_range &) {
      fail("numeric value out of range");
    }
  }

  std::string quoted() {
    char quote = input_[pos_++];
    std::string out;
    while (pos_ < input_.size()) {
      char cinput = input_[pos_++];
      if (cinput == quote) {
        return out;
      }
      if (cinput == '\\') {
        if (pos_ >= input_.size()) {
          fail("unterminated escape");
        }
        char einput = input_[pos_++];
        if (einput == quote || einput == '\\') {
          out.push_back(einput);
        } else {
          fail("invalid SNBT escape");
        }
      } else {
        out.push_back(cinput);
      }
    }
    fail("unterminated string");
  }

  std::string_view bare() {
    space();
    auto start = pos_;
    while (pos_ < input_.size() && input_[pos_] != ',' && input_[pos_] != ']' && input_[pos_] != '}' && (std::isspace(static_cast<unsigned char>(input_[pos_])) == 0)) {
      ++pos_;
    }
    return input_.substr(start, pos_ - start);
  }

  std::string bareKey() {
    space();
    auto start = pos_;
    while (pos_ < input_.size() && input_[pos_] != ':' && (std::isspace(static_cast<unsigned char>(input_[pos_])) == 0)) {
      ++pos_;
    }
    if (start == pos_) {
      fail("expected compound key");
    }
    return std::string(input_.substr(start, pos_ - start));
  }

  void space() {
    while (pos_ < input_.size() && (std::isspace(static_cast<unsigned char>(input_[pos_])) != 0)) {
      ++pos_;
    }
  }

  char peek() {
    space();
    return pos_ < input_.size() ? input_[pos_] : '\0';
  }

  bool accept(char chr) {
    space();
    if (pos_ < input_.size() && input_[pos_] == chr) {
      ++pos_;
      return true;
    }
    return false;
  }

  void take(char cArg) {
    if (!accept(cArg)) {
      fail(std::string("expected '") + cArg + "'");
    }
  }

  [[noreturn]] void fail(std::string_view message) const {
    throw Error(std::string(message), pos_);
  }

  std::string_view input_;
  const ParseOptions &options_;
  std::size_t pos_{};
};

} // namespace

Error::Error(std::string message, std::size_t offset) : std::runtime_error(message), offset_(offset) {
}

std::size_t Error::offset() const noexcept {
  return offset_;
}

namespace detail {
std::uint64_t readUnsigned(std::span<const std::byte> source, std::uint32_t begin, std::size_t size) {
  if (size > 8 || begin > source.size() || size > source.size() - begin) {
    throw Error("NBT view is outside the source buffer", begin);
  }
  std::uint64_t value{};
  for (std::size_t i = 0; i < size; ++i) {
    value = (value << 8) | std::to_integer<std::uint8_t>(source[begin + i]);
  }
  return value;
}

std::string_view name(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t tag) noexcept {
  if (tag >= tokens.size()) {
    return {};
  }
  const auto limit = std::min<std::size_t>(tokens[tag].subtreeEnd, tokens.size());
  for (std::size_t i = tag + 1; i < limit; ++i) {
    const auto &entry = tokens[i];
    if (entry.kind != TokenKind::Name || entry.parent != tag) {
      continue;
    }
    if (entry.end - entry.begin < 2 || entry.end > source.size()) {
      return {};
    }
    const auto length = static_cast<std::size_t>((std::to_integer<std::uint8_t>(source[entry.begin]) << 8) | std::to_integer<std::uint8_t>(source[entry.begin + 1]));
    if (length > entry.end - entry.begin - 2) {
      return {};
    }
    return {reinterpret_cast<const char *>(source.data() + entry.begin + 2), length};
  }
  return {};
}

std::optional<std::uint32_t> findChild(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view expected) noexcept {
  if (parent >= tokens.size()) {
    return std::nullopt;
  }
  const auto limit = std::min<std::size_t>(tokens[parent].subtreeEnd, tokens.size());
  for (std::size_t i = parent + 1; i < limit; ++i) {
    const auto &entry = tokens[i];
    if (entry.kind != TokenKind::Tag) {
      continue;
    }
    if (entry.parent == parent && name(source, tokens, static_cast<std::uint32_t>(i)) == expected) {
      return static_cast<std::uint32_t>(i);
    }
    if (entry.subtreeEnd > i) {
      i = entry.subtreeEnd - 1;
    }
  }
  return std::nullopt;
}

std::optional<std::uint32_t> findPath(std::span<const std::byte> source, std::span<const Token> tokens, std::uint32_t parent, std::string_view path) noexcept {
  if (parent >= tokens.size()) {
    return std::nullopt;
  }
  const auto firstEnd = path.find('.');
  const auto first = path.substr(0, firstEnd);
  if (first == name(source, tokens, parent)) {
    if (firstEnd == std::string_view::npos) {
      return parent;
    }
    path.remove_prefix(firstEnd + 1);
  }
  while (!path.empty()) {
    const auto end = path.find('.');
    const auto part = path.substr(0, end);
    const auto child = findChild(source, tokens, parent, part);
    if (!child) {
      return std::nullopt;
    }
    parent = *child;
    if (end == std::string_view::npos) {
      return parent;
    }
    path.remove_prefix(end + 1);
  }
  return parent;
}

const Token &payload(std::span<const Token> tokens, std::uint32_t tag) {
  if (tag >= tokens.size()) {
    throw Error("invalid NBT view token", 0);
  }
  const auto limit = std::min<std::size_t>(tokens[tag].subtreeEnd, tokens.size());
  for (std::size_t i = tag + 1; i < limit; ++i) {
    if (tokens[i].kind == TokenKind::Payload && tokens[i].parent == tag) {
      return tokens[i];
    }
  }
  throw Error("NBT tag has no payload token", tokens[tag].begin);
}
} // namespace detail

Tag::Tag(Type type, std::string name, Value value, Type elementType) : type(type), name(std::move(name)), value(std::move(value)), elementType(elementType) {
}

Tag byteTag(std::string name, Byte value) {
  return {Type::Byte, std::move(name), value};
}

Tag shortTag(std::string name, std::int16_t value) {
  return {Type::Short, std::move(name), value};
}

Tag intTag(std::string name, std::int32_t value) {
  return {Type::Int, std::move(name), value};
}

Tag longTag(std::string name, std::int64_t value) {
  return {Type::Long, std::move(name), value};
}

Tag floatTag(std::string name, float value) {
  return {Type::Float, std::move(name), value};
}

Tag doubleTag(std::string name, double value) {
  return {Type::Double, std::move(name), value};
}

Tag byteArrayTag(std::string name, ByteArray value) {
  return {Type::ByteArray, std::move(name), std::move(value)};
}

Tag stringTag(std::string name, std::string value) {
  return {Type::String, std::move(name), std::move(value)};
}

Tag listTag(std::string name, Type elementType, List value) {
  return {Type::List, std::move(name), std::move(value), elementType};
}

Tag compoundTag(std::string name, Compound value) {
  return {Type::Compound, std::move(name), std::move(value)};
}

Tag intArrayTag(std::string name, IntArray value) {
  return {Type::IntArray, std::move(name), std::move(value)};
}

Tag longArrayTag(std::string name, LongArray value) {
  return {Type::LongArray, std::move(name), std::move(value)};
}

Tag parse(std::istream &input, const ParseOptions &options) {
  return Reader(input, options, nullptr).root();
}

std::optional<Tag> tryParse(std::istream &input, const ParseOptions &options) {
  const auto start = input.tellg();
  if (start == std::streampos{-1}) {
    throw std::invalid_argument("tryParse requires a seekable std::istream");
  }
  try {
    return parse(input, options);
  } catch (const IncompleteDataError &) {
    input.clear();
    input.seekg(start);
    return std::nullopt;
  }
}

TokenizedDocument tokenize(std::istream &input, const ParseOptions &options) {
  TokenizedDocument document;
  document.format = options.format;
  (void)Reader(input, options, &document.tokens).root();
  return document;
}

std::size_t encodedSize(const Tag &root, BinaryFormat format) {
  if (root.type == Type::End) {
    throw std::invalid_argument("TAG_End cannot be encoded as a root tag");
  }
  if (format == BinaryFormat::Network && root.type != Type::Compound) {
    throw std::invalid_argument("Network NBT root must be TAG_Compound");
  }
  const bool includeName = format == BinaryFormat::File;
  const std::size_t rootTypeSize = includeName ? 0 : 1;
  return rootTypeSize + encodedTagSize(root, includeName);
}

std::size_t encodedSize(const TokenizedDocument &document) {
  if (document.tokens.empty()) {
    throw std::invalid_argument("tokenized document is empty");
  }
  const auto &root = document.tokens.front();
  if (root.kind != TokenKind::Tag || root.parent != Token::noParent || root.end < root.begin || root.subtreeEnd > document.tokens.size()) {
    throw std::invalid_argument("invalid tokenized document");
  }
  return static_cast<std::size_t>(root.end - root.begin);
}

void serialize(std::ostream &output, const Tag &root, BinaryFormat format) {
  if (root.type == Type::End) {
    throw std::invalid_argument("TAG_End cannot be serialized as a root tag");
  }
  if (format == BinaryFormat::Network && root.type != Type::Compound) {
    throw std::invalid_argument("Network NBT root must be TAG_Compound");
  }
  Writer(output).run(root, format == BinaryFormat::File);
}

Tag load(const std::filesystem::path &path, Compression comp, const ParseOptions &options) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open NBT file");
  }
  if (comp == Compression::None) {
    return parse(file, options);
  }

  const bool autoDetect = comp == Compression::Auto;
  int windowBits{};
  if (comp == Compression::Gzip) {
    windowBits = gzipWindowBits;
  } else if (comp == Compression::Zlib) {
    windowBits = zlibWindowBits;
  }
  zstr::istream input(file, zstr::default_buff_size, autoDetect, windowBits);
  return parse(input, options);
}

void save(const std::filesystem::path &path, const Tag &root, Compression comp, BinaryFormat format) {
  if (comp == Compression::Auto) {
    throw std::invalid_argument("Auto is invalid for compression");
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open NBT file");
  }
  if (comp == Compression::None) {
    serialize(file, root, format);
    return;
  }

  const int windowBits = comp == Compression::Gzip ? gzipWindowBits : zlibWindowBits;
  zstr::ostream output(file, zstr::default_buff_size, -1, windowBits);
  serialize(output, root, format);
  output.flush();
}

Tag clone(const Tag &tag) {
  return tag;
}

bool map(Tag &root, const Visitor &visitor) {
  if (!visitor(root)) {
    return false;
  }
  if (container(root)) {
    for (auto &child : children(root)) {
      if (!map(child, visitor)) {
        return false;
      }
    }
  }
  return true;
}

bool map(const Tag &root, const ConstVisitor &visitor) {
  if (!visitor(root)) {
    return false;
  }
  if (container(root)) {
    for (const auto &child : children(root)) {
      if (!map(child, visitor)) {
        return false;
      }
    }
  }
  return true;
}

std::optional<Tag> filter(const Tag &root, const Predicate &predicate) {
  if (!predicate(root)) {
    return std::nullopt;
  }
  Tag copy = root;
  if (container(copy)) {
    auto &tags = children(copy);
    tags.erase(std::remove_if(tags.begin(),
                              tags.end(),
                              [&](Tag &child) {
                                auto result = filter(child, predicate);
                                if (result) {
                                  child = std::move(*result);
                                }
                                return !result;
                              }),
               tags.end());
  }
  return copy;
}

void filterInPlace(Tag &root, const Predicate &predicate) {
  if (!container(root)) {
    return;
  }
  auto &tags = children(root);
  tags.erase(std::remove_if(tags.begin(),
                            tags.end(),
                            [&](Tag &child) {
                              if (!predicate(child)) {
                                return true;
                              }
                              filterInPlace(child, predicate);
                              return false;
                            }),
             tags.end());
}

Tag *find(Tag &root, const Predicate &predicate) {
  if (predicate(root)) {
    return &root;
  }
  if (container(root)) {
    for (auto &child : children(root)) {
      if (auto *found = find(child, predicate)) {
        return found;
      }
    }
  }
  return nullptr;
}

const Tag *find(const Tag &root, const Predicate &predicate) {
  if (predicate(root)) {
    return &root;
  }
  if (container(root)) {
    for (const auto &child : children(root)) {
      if (const auto *found = find(child, predicate)) {
        return found;
      }
    }
  }
  return nullptr;
}

Tag *findByName(Tag &root, std::string_view name) {
  return find(root, [&](const Tag &tag) {
    return tag.name == name;
  });
}

const Tag *findByName(const Tag &root, std::string_view name) {
  return find(root, [&](const Tag &tag) {
    return tag.name == name;
  });
}

Tag *at(Tag &tag, std::size_t index) {
  return container(tag) && index < children(tag).size() ? &children(tag)[index] : nullptr;
}

const Tag *at(const Tag &tag, std::size_t index) {
  return container(tag) && index < children(tag).size() ? &children(tag)[index] : nullptr;
}

Tag *findByPath(Tag &root, std::string_view path) {
  Tag *cur = &root;
  size_t start = 0;
  if (auto dot = path.find('.'); (dot == std::string_view::npos ? path : path.substr(0, dot)) == root.name) {
    start = dot == std::string_view::npos ? path.size() : dot + 1;
  }
  while (start < path.size()) {
    auto end = path.find('.', start);
    auto part = path.substr(start, end - start);
    if (!container(*cur)) {
      return nullptr;
    }
    auto &tags = children(*cur);
    auto pos = std::find_if(tags.begin(), tags.end(), [&](Tag &tag) {
      return tag.name == part;
    });
    if (pos == tags.end()) {
      return nullptr;
    }
    cur = &*pos;
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return cur;
}

const Tag *findByPath(const Tag &root, std::string_view path) {
  return findByPath(const_cast<Tag &>(root), path);
}

std::size_t size(const Tag &root) {
  std::size_t total = 1;
  if (container(root)) {
    for (const auto &child : children(root)) {
      total += size(child);
    }
  }
  return total;
}

bool equivalent(const Tag &lhs, const Tag &rhs, double eps) {
  if (lhs.type != rhs.type || lhs.name != rhs.name || lhs.elementType != rhs.elementType) {
    return false;
  }
  if (container(lhs)) {
    const auto &lhsChildren = children(lhs);
    const auto &rhsChildren = children(rhs);
    return lhsChildren.size() == rhsChildren.size() && std::equal(lhsChildren.begin(), lhsChildren.end(), rhsChildren.begin(), [&](const Tag &left, const Tag &right) {
             return equivalent(left, right, eps);
           });
  }
  switch (lhs.type) {
  case Type::End:
    return true;
  case Type::Byte:
    return lhs.as<Byte>() == rhs.as<Byte>();
  case Type::Short:
    return lhs.as<std::int16_t>() == rhs.as<std::int16_t>();
  case Type::Int:
    return lhs.as<std::int32_t>() == rhs.as<std::int32_t>();
  case Type::Long:
    return lhs.as<std::int64_t>() == rhs.as<std::int64_t>();
  case Type::Float:
    return std::abs(lhs.as<float>() - rhs.as<float>()) <= eps;
  case Type::Double:
    return std::abs(lhs.as<double>() - rhs.as<double>()) <= eps;
  case Type::ByteArray:
    return lhs.as<ByteArray>() == rhs.as<ByteArray>();
  case Type::String:
    return lhs.as<std::string>() == rhs.as<std::string>();
  case Type::IntArray:
    return lhs.as<IntArray>() == rhs.as<IntArray>();
  case Type::LongArray:
    return lhs.as<LongArray>() == rhs.as<LongArray>();
  case Type::List:
  case Type::Compound:
    return false;
  }
  return false;
}

Tag parseSnbt(std::string_view input, const ParseOptions &options) {
  return SnbtParser(input, options).run();
}

std::string toSnbt(const Tag &root, bool pretty) {
  std::ostringstream out;
  snbt(root, out, pretty, 0, true);
  return out.str();
}

std::string_view typeName(Type type) noexcept {
  static constexpr std::string_view names[] = {"TAG_End", "TAG_Byte", "TAG_Short", "TAG_Int", "TAG_Long", "TAG_Float", "TAG_Double", "TAG_Byte_Array", "TAG_String", "TAG_List", "TAG_Compound", "TAG_Int_Array", "TAG_Long_Array"};
  auto idx = static_cast<size_t>(type);
  return idx < std::size(names) ? names[idx] : "TAG_Unknown";
}

Builder::Builder(std::string name) : root_(compoundTag(std::move(name))), stack_{&root_} {
}

Builder &Builder::add(Tag tag) {
  if (stack_.empty() || !container(*stack_.back())) {
    throw std::logic_error("builder has no open container");
  }
  if (stack_.back()->type == Type::List) {
    if (tag.type != stack_.back()->elementType) {
      throw std::invalid_argument("list element type mismatch");
    }
    tag.name.clear();
  }
  children(*stack_.back()).push_back(std::move(tag));
  return *this;
}

Builder &Builder::beginCompound(std::string name) {
  add(compoundTag(std::move(name)));
  auto &tags = children(*stack_.back());
  stack_.push_back(&tags.back());
  return *this;
}

Builder &Builder::beginList(std::string name, Type type) {
  add(listTag(std::move(name), type));
  auto &tags = children(*stack_.back());
  stack_.push_back(&tags.back());
  return *this;
}

Builder &Builder::end() {
  if (stack_.size() <= 1) {
    throw std::logic_error("cannot close root compound");
  }
  stack_.pop_back();
  return *this;
}

Tag Builder::build() const {
  if (stack_.size() != 1) {
    throw std::logic_error("builder contains unclosed containers");
  }
  return root_;
}

} // namespace nbt
