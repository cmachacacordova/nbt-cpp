#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "nbt/error.h"
#include "nbt/export.h"
#include "nbt/type.h"

namespace nbt {

/** @brief Owning representation of one named NBT tag and its payload. */
struct NBT_CPP_API Tag {

  using Byte = std::int8_t;            ///< Signed 8-bit TAG_Byte payload.
  using Short = std::int16_t;          ///< Signed 16-bit TAG_Short payload.
  using Int = std::int32_t;            ///< Signed 32-bit TAG_Int payload.
  using Long = std::int64_t;           ///< Signed 64-bit TAG_Long payload.
  using Float = float;                 ///< IEEE-754 TAG_Float payload.
  using Double = double;               ///< IEEE-754 TAG_Double payload.
  using String = std::string;          ///< Owning UTF-8 TAG_String payload.
  using ByteArray = std::vector<Byte>; ///< Owning TAG_Byte_Array payload.
  using IntArray = std::vector<Int>;   ///< Owning TAG_Int_Array payload.
  using LongArray = std::vector<Long>; ///< Owning TAG_Long_Array payload.

  /** @brief Ordered homogeneous sequence of unnamed tags. */
  struct List {
    std::vector<Tag> values; ///< List elements in serialized order.

    List() = default;

    /** Constructs a list by taking ownership of existing elements. */
    explicit List(std::vector<Tag> value) : values(std::move(value)) {
    }

    /** Constructs a list from an initializer list. */
    List(std::initializer_list<Tag> init) : values(init) {
    }
  };

  /** @brief Ordered collection of named child tags. */
  struct Compound {
    std::vector<Tag> values; ///< Child tags in serialized order.

    Compound() = default;

    /** Constructs a compound by taking ownership of existing children. */
    explicit Compound(std::vector<Tag> value) : values(std::move(value)) {
    }

    /** Constructs a compound from an initializer list. */
    Compound(std::initializer_list<Tag> init) : values(init) {
    }
  };

  /** Variant containing every owning NBT payload representation. */
  using Value = std::variant<std::monostate, Byte, Short, Int, Long, Float, Double, String, List, Compound, ByteArray, IntArray, LongArray>;

  Type type{Type::End};        ///< Runtime NBT payload type.
  std::string name;            ///< Tag name; list elements and Network roots are unnamed.
  Value value;                 ///< Owning payload selected by type.
  Type elementType{Type::End}; ///< Declared element type for lists.

  Tag() = default;

  /** Constructs a tag from its complete runtime representation. */
  Tag(Type type, std::string name, Value value, Type elementType = Type::End);

  /** Returns the payload as T.
   * @tparam T Expected payload alternative.
   * @throws std::bad_variant_access if T is not the active payload type.
   */
  template <class T>
  [[nodiscard]] T &as() {
    return std::get<T>(value);
  }

  /** Returns the payload as a const T.
   * @tparam T Expected payload alternative.
   * @throws std::bad_variant_access if T is not the active payload type.
   */
  template <class T>
  [[nodiscard]] const T &as() const {
    return std::get<T>(value);
  }
};

using Byte = Tag::Byte; ///< Namespace alias for Tag::Byte.
/** Creates an owning TAG_Byte. */
[[nodiscard]] NBT_CPP_API Tag byteTag(std::string name, Byte value);

using Short = Tag::Short; ///< Namespace alias for Tag::Short.
/** Creates an owning TAG_Short. */
[[nodiscard]] NBT_CPP_API Tag shortTag(std::string name, Short value);

using Int = Tag::Int; ///< Namespace alias for Tag::Int.
/** Creates an owning TAG_Int. */
[[nodiscard]] NBT_CPP_API Tag intTag(std::string name, Int value);

using Long = Tag::Long; ///< Namespace alias for Tag::Long.
/** Creates an owning TAG_Long. */
[[nodiscard]] NBT_CPP_API Tag longTag(std::string name, Long value);

using Float = Tag::Float; ///< Namespace alias for Tag::Float.
/** Creates an owning TAG_Float. */
[[nodiscard]] NBT_CPP_API Tag floatTag(std::string name, Float value);

using Double = Tag::Double; ///< Namespace alias for Tag::Double.
/** Creates an owning TAG_Double. */
[[nodiscard]] NBT_CPP_API Tag doubleTag(std::string name, Double value);

using String = Tag::String; ///< Namespace alias for Tag::String.
/** Creates an owning TAG_String. */
[[nodiscard]] NBT_CPP_API Tag stringTag(std::string name, String value);

using List = Tag::List; ///< Namespace alias for Tag::List.
/** Creates an owning homogeneous TAG_List. */
[[nodiscard]] NBT_CPP_API Tag listTag(std::string name, Type elementType, List value = {});

using Compound = Tag::Compound; ///< Namespace alias for Tag::Compound.
/** Creates an owning TAG_Compound. */
[[nodiscard]] NBT_CPP_API Tag compoundTag(std::string name, Compound value = {});

using ByteArray = Tag::ByteArray; ///< Namespace alias for Tag::ByteArray.
/** Creates an owning TAG_Byte_Array. */
[[nodiscard]] NBT_CPP_API Tag byteArrayTag(std::string name, ByteArray value);

using IntArray = Tag::IntArray; ///< Namespace alias for Tag::IntArray.
/** Creates an owning TAG_Int_Array. */
[[nodiscard]] NBT_CPP_API Tag intArrayTag(std::string name, IntArray value);

using LongArray = Tag::LongArray; ///< Namespace alias for Tag::LongArray.
/** Creates an owning TAG_Long_Array. */
[[nodiscard]] NBT_CPP_API Tag longArrayTag(std::string name, LongArray value);

} // namespace nbt
