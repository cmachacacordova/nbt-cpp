#pragma once

#include <istream>
#include <optional>

#include "nbt/export.h"
#include "nbt/tag.h"
#include "nbt/token.h"

namespace nbt {

/** Parses one binary NBT document from the current stream position.
 * @throws IncompleteDataError if the stream ends before the document is complete.
 * @throws Error if the binary representation is malformed.
 */
[[nodiscard]] NBT_CPP_API Tag parse(std::istream &input, const ParseOptions &options = {});

/** Attempts to parse from a seekable stream, restoring its position when more data is needed.
 * @return The parsed root, or std::nullopt when the available data is incomplete.
 * @throws std::invalid_argument if the stream is not seekable.
 * @throws Error if the available data is malformed.
 */
[[nodiscard]] NBT_CPP_API std::optional<Tag> tryParse(std::istream &input, const ParseOptions &options = {});

/** Indexes one binary NBT document without retaining its source stream. */
[[nodiscard]] NBT_CPP_API TokenizedDocument tokenize(std::istream &input, const ParseOptions &options = {});

} // namespace nbt
