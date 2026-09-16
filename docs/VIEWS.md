# Token metadata

The stream API intentionally does not expose byte-backed typed views. A generic `std::istream` does not guarantee contiguous or persistent storage, so returning `std::string_view`, spans, or pointers into source data would be unsafe.

Use `tokenize` for structural offsets and `parse` when typed values are required.
