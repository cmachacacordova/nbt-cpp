#pragma once

#include <filesystem>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "nbt/builder.h"
#include "nbt/error.h"
#include "nbt/stream.h"
#include "nbt/tag.h"
#include "nbt/token.h"
#include "nbt/type.h"

namespace nbt {

/** Returns the exact number of bytes serialize will write for a tree. */
[[nodiscard]] NBT_CPP_API std::size_t encodedSize(const Tag &root, BinaryFormat format = BinaryFormat::File);

/** Returns the indexed root byte range of a validated token document. */
[[nodiscard]] NBT_CPP_API std::size_t encodedSize(const TokenizedDocument &document);

/** Writes one binary NBT document at the current output position. */
NBT_CPP_API void serialize(std::ostream &output, const Tag &root, BinaryFormat format = BinaryFormat::File);

/** Reads an uncompressed, gzip, or zlib NBT file.
 * Compression::Auto detects compressed input and treats other input as plain NBT.
 */
[[nodiscard]] NBT_CPP_API Tag load(const std::filesystem::path &path, Compression compression = Compression::Auto, const ParseOptions &options = {});

/** Writes an NBT file, optionally compressed with gzip or zlib.
 * @throws std::invalid_argument when compression is Compression::Auto.
 */
NBT_CPP_API void save(const std::filesystem::path &path, const Tag &root, Compression compression = Compression::None, BinaryFormat format = BinaryFormat::File);

/** Returns a deep copy of an owning tag tree. */
[[nodiscard]] NBT_CPP_API Tag clone(const Tag &tag);

/** Mutable depth-first visitor; return false to stop traversal. */
using Visitor = std::function<bool(Tag &)>;

/** Const depth-first visitor; return false to stop traversal. */
using ConstVisitor = std::function<bool(const Tag &)>;

/** Predicate used by filtering and search utilities. */
using Predicate = std::function<bool(const Tag &)>;

/** Visits a mutable tree in depth-first order. */
NBT_CPP_API bool map(Tag &root, const Visitor &visitor);

/** Visits a const tree in depth-first order. */
NBT_CPP_API bool map(const Tag &root, const ConstVisitor &visitor);

/** Copies tags accepted by predicate while preserving their ancestry. */
[[nodiscard]] NBT_CPP_API std::optional<Tag> filter(const Tag &root, const Predicate &predicate);

/** Removes rejected tags from a tree in place. */
NBT_CPP_API void filterInPlace(Tag &root, const Predicate &predicate);

/** Returns the first mutable tag accepted in depth-first order. */
[[nodiscard]] NBT_CPP_API Tag *find(Tag &root, const Predicate &predicate);

/** Returns the first const tag accepted in depth-first order. */
[[nodiscard]] NBT_CPP_API const Tag *find(const Tag &root, const Predicate &predicate);

/** Returns the first mutable tag with the requested name. */
[[nodiscard]] NBT_CPP_API Tag *findByName(Tag &root, std::string_view name);

/** Returns the first const tag with the requested name. */
[[nodiscard]] NBT_CPP_API const Tag *findByName(const Tag &root, std::string_view name);

/** Resolves a dot-separated path from the root tag. */
[[nodiscard]] NBT_CPP_API Tag *findByPath(Tag &root, std::string_view path);

/** Resolves a dot-separated path from a const root tag. */
[[nodiscard]] NBT_CPP_API const Tag *findByPath(const Tag &root, std::string_view path);

/** Returns a mutable list or compound child, or nullptr when out of range. */
[[nodiscard]] NBT_CPP_API Tag *at(Tag &tag, std::size_t index);

/** Returns a const list or compound child, or nullptr when out of range. */
[[nodiscard]] NBT_CPP_API const Tag *at(const Tag &tag, std::size_t index);

/** Counts the root and all descendants in an owning tree. */
[[nodiscard]] NBT_CPP_API std::size_t size(const Tag &root);

/** Compares complete trees, using eps for floating-point payloads. */
[[nodiscard]] NBT_CPP_API bool equivalent(const Tag &lhs, const Tag &rhs, double eps = 1e-6);

/** Parses one SNBT value and rejects trailing input. */
[[nodiscard]] NBT_CPP_API Tag parseSnbt(std::string_view input, const ParseOptions &options = {});

/** Serializes a tree as compact or indented SNBT. */
[[nodiscard]] NBT_CPP_API std::string toSnbt(const Tag &root, bool pretty = true);

/** Returns the canonical NBT type name. */
[[nodiscard]] NBT_CPP_API std::string_view typeName(Type type) noexcept;

} // namespace nbt
