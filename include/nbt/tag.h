#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "nbt/error.h"
#include "nbt/export.h"
#include "nbt/type.h"

namespace nbt {

struct NBT_CPP_API Tag {
  using Byte = std::int8_t;
  using Short = std::int16_t;
  using Int = std::int32_t;
  using Long = std::int64_t;
  using Float = float;
  using Double = double;
  using String = std::string;
  using ByteArray = std::vector<std::int8_t>;
  using IntArray = std::vector<std::int32_t>;
  using LongArray = std::vector<std::int64_t>;

  struct List {
    std::vector<Tag> values;

    List() = default;

    explicit List(std::vector<Tag> value) : values(std::move(value)) {
    }

    List(std::initializer_list<Tag> init) : values(init) {
    }
  };

  struct Compound {
    std::vector<Tag> values;

    Compound() = default;

    explicit Compound(std::vector<Tag> value) : values(std::move(value)) {
    }

    Compound(std::initializer_list<Tag> init) : values(init) {
    }
  };

  using Value = std::variant<std::monostate, Byte, Short, Int, Long, Float, Double, String, List, Compound, ByteArray, IntArray, LongArray>;

  Type type{Type::End};
  std::string name;
  Value value;
  Type elementType{Type::End};

  Tag() = default;
  Tag(Type type, std::string name, Value value, Type elementType = Type::End);

  template <class T>
  [[nodiscard]] T &as() {
    return std::get<T>(value);
  }

  template <class T>
  [[nodiscard]] const T &as() const {
    return std::get<T>(value);
  }
};

using Byte = Tag::Byte;
[[nodiscard]] NBT_CPP_API Tag byteTag(std::string name, Byte value);

using Short = Tag::Short;
[[nodiscard]] NBT_CPP_API Tag shortTag(std::string name, Short value);

using Int = Tag::Int;
[[nodiscard]] NBT_CPP_API Tag intTag(std::string name, Int value);

using Long = Tag::Long;
[[nodiscard]] NBT_CPP_API Tag longTag(std::string name, Long value);

using Float = Tag::Float;
[[nodiscard]] NBT_CPP_API Tag floatTag(std::string name, Float value);

using Double = Tag::Double;
[[nodiscard]] NBT_CPP_API Tag doubleTag(std::string name, Double value);

using String = Tag::String;
[[nodiscard]] NBT_CPP_API Tag stringTag(std::string name, String value);

using List = Tag::List;
[[nodiscard]] NBT_CPP_API Tag listTag(std::string name, Type elementType, List value = {});
using Compound = Tag::Compound;
[[nodiscard]] NBT_CPP_API Tag compoundTag(std::string name, Compound value = {});

using ByteArray = Tag::ByteArray;
[[nodiscard]] NBT_CPP_API Tag byteArrayTag(std::string name, ByteArray value);

using IntArray = Tag::IntArray;
[[nodiscard]] NBT_CPP_API Tag intArrayTag(std::string name, IntArray value);

using LongArray = Tag::LongArray;
[[nodiscard]] NBT_CPP_API Tag longArrayTag(std::string name, LongArray value);

} // namespace nbt
