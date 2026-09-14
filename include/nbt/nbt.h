#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "nbt/buffer.h"
#include "nbt/builder.h"
#include "nbt/error.h"
#include "nbt/tag.h"
#include "nbt/token.h"
#include "nbt/type.h"

namespace nbt {

[[nodiscard]] NBT_CPP_API TokenizedDocument tokenize(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API TokenizedView tokenize(std::span<const std::byte> input, std::span<Token> output, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(std::span<const std::byte> input, const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const TokenizedDocument &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Tag parse(const Buffer &input, const TokenizedView &document, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API Buffer serialize(const Tag &root, BinaryFormat format = BinaryFormat::File);

[[nodiscard]] NBT_CPP_API Buffer compress(std::span<const std::byte> input, Compression compression);
[[nodiscard]] NBT_CPP_API Buffer compress(const Buffer &input, Compression compression);
[[nodiscard]] NBT_CPP_API Buffer decompress(std::span<const std::byte> input, Compression compression = Compression::Auto);
[[nodiscard]] NBT_CPP_API Buffer decompress(const Buffer &input, Compression compression = Compression::Auto);
[[nodiscard]] NBT_CPP_API Tag load(const std::filesystem::path &path, Compression compression = Compression::Auto, const ParseOptions &options = {});
NBT_CPP_API void save(const std::filesystem::path &path, const Tag &root, Compression compression = Compression::None);

[[nodiscard]] NBT_CPP_API Tag clone(const Tag &tag);
using Visitor = std::function<bool(Tag &)>;
using ConstVisitor = std::function<bool(const Tag &)>;
using Predicate = std::function<bool(const Tag &)>;
NBT_CPP_API bool map(Tag &root, const Visitor &visitor);
NBT_CPP_API bool map(const Tag &root, const ConstVisitor &visitor);
[[nodiscard]] NBT_CPP_API std::optional<Tag> filter(const Tag &root, const Predicate &predicate);
NBT_CPP_API void filterInPlace(Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API Tag *find(Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API const Tag *find(const Tag &root, const Predicate &predicate);
[[nodiscard]] NBT_CPP_API Tag *findByName(Tag &root, std::string_view name);
[[nodiscard]] NBT_CPP_API const Tag *findByName(const Tag &root, std::string_view name);
[[nodiscard]] NBT_CPP_API Tag *findByPath(Tag &root, std::string_view path);
[[nodiscard]] NBT_CPP_API const Tag *findByPath(const Tag &root, std::string_view path);
[[nodiscard]] NBT_CPP_API Tag *at(Tag &tag, std::size_t index);
[[nodiscard]] NBT_CPP_API const Tag *at(const Tag &tag, std::size_t index);
[[nodiscard]] NBT_CPP_API std::size_t size(const Tag &root);
[[nodiscard]] NBT_CPP_API bool equivalent(const Tag &lhs, const Tag &rhs, double eps = 1e-6);
[[nodiscard]] NBT_CPP_API Tag parseSnbt(std::string_view input, const ParseOptions &options = {});
[[nodiscard]] NBT_CPP_API std::string toSnbt(const Tag &root, bool pretty = true);
[[nodiscard]] NBT_CPP_API std::string_view typeName(Type type) noexcept;

} // namespace nbt
