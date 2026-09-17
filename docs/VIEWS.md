# Lazy views

`Nbt::root()` returns a lightweight `Nbt::View`. Names, strings, scalar values and arrays are decoded when their accessors are called.

A view is valid only while its originating `Nbt` object remains alive, unmoved, and attached to the same input. For borrowed input, the caller must also keep the complete byte range alive and unchanged.

Call `View::materialize()` to create an owning value subtree, or `Nbt::materialize()` for the complete document.
