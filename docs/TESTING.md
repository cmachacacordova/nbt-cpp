# Testing y compatibilidad

## Alcance

`nbt-cpp` implementa NBT binario de Minecraft Java Edition. Los bytes Java NBT son big-endian y pueden aparecer como NBT de archivo (`nbt::Source::File`) o como NBT de red (`nbt::Source::Network`).

La biblioteca no implementa Bedrock NBT little-endian, Bedrock network/VarInt NBT ni formatos privados de herramientas externas. Esos formatos no deben tratarse como compatibles ni usarse como fixtures positivos.

Las pruebas validan comportamiento observable mediante la API pública, no detalles internos. Las áreas cubiertas y previstas son:

- Construcción de `Tag`, literales, nombres string-like y valores convertibles a `Tag`.
- Codificación, parseo, materialización y round-trip de todos los tipos NBT.
- Formatos File y Network.
- Entrada prestada, entrada propia y acumulación con `append`.
- Vistas lazy, iteración, arrays y ciclo de vida de documentos y vistas.
- SNBT, filesystem y compresión None, Gzip y Zlib.
- Truncamientos, entradas malformadas, offsets de error y límites de recursos.
- Fixtures canónicos, interoperabilidad, datos reales de Minecraft, fuzzing y rendimiento a medida que se incorporen.

## Organización actual

- `tests/nbt_tests.cpp`: tests del codec core sin ZLIB.
- `tests/utilities_tests.cpp`: tests de SNBT, filesystem y compresión; solo se compila con `NBT_CPP_BUILD_UTILITIES=ON`.
- `examples/`: programas autónomos que muestran usos de la biblioteca. Se compilan, pero no se registran como tests de CTest.
- `nbt-cpp-core-tests`: test CTest con etiquetas `core;codec`.
- `nbt-cpp-utilities-tests`: test CTest con etiquetas `utilities;io`, cuando utilities está habilitado.

El arnés actual es un ejecutable dependency-free que comunica el resultado mediante el código de salida. La adopción futura de GoogleTest es opcional y no debe convertir ZLIB ni otra dependencia en requisito del target core.

## Matriz de valores

Cada operación aplicable debe probar, como mínimo:

- `Byte`, `Short`, `Int` y `Long`: límites, negativos, cero y positivos.
- `Float` y `Double`: ceros con signo, finitos extremos, subnormales, infinitos y NaN cuando la operación los admite.
- `String`: vacía, ASCII, UTF-8, NUL embebido y límites medidos en bytes.
- `ByteArray`, `IntArray` y `LongArray`: vacío, un elemento, límites y tamaños mayores.
- `List` y `Compound`: vacío, un elemento, anidamiento, muchos hijos, nombres repetidos y tipos válidos.
- `End`: terminador válido y aparición ilegal como tag normal.

Los floats se comparan por representación cuando el contrato exige preservar bits, incluyendo el signo de cero y el tratamiento de NaN.

## Reglas para nuevos tests

1. Probar una capacidad pública y observable, no campos o funciones privadas.
2. Preferir ciclos de integración `Tag -> encode -> parse -> View -> materialize` y `bytes -> parse -> encode`.
3. Mantener separados los tests del core y de utilities.
4. Usar vectores golden independientes cuando se comprueben bytes exactos.
5. Para entradas truncadas, probar cada punto de corte relevante y distinguir `NeedMoreData` de `Error`.
6. Para límites, probar `limit - 1`, `limit` y `limit + 1` cuando sea aplicable.
7. Los tests que creen archivos temporales deben limpiarlos y no depender de un directorio del repositorio.
8. Los ejemplos no deben usarse como sustituto de tests ni añadirse a CTest solo para verificar que compilan.

## Infraestructura y compatibilidad

Las configuraciones mínimas que deben verificarse son:

- Utilities ON y OFF.
- Debug y Release.
- MSVC con `/W4 /permissive-`; en otros compiladores, warnings estrictos equivalentes.
- `git diff --check`.

La compatibilidad instalada debe probarse además con un consumidor que use `find_package(nbt-cpp CONFIG REQUIRED)`, enlace `nbt::nbt`, y repita el caso con `nbt::utilities` cuando ZLIB esté habilitado.

## Decisiones pendientes

Antes de fijar nuevos vectores de conformidad deben documentarse explícitamente:

- La nomenclatura pública `Source` frente a cualquier nombre alternativo como `Format`.
- La ausencia actual de `encodeView()`; no documentar ni probar una API que no existe.
- Política para UTF-8 inválido.
- Preservación de NaN y valores no finitos en SNBT.
- Gzip concatenado.
- Semántica de `append` después de `Status::Complete`.
- Política de bytes finales cuando `requireCompleteInput` está deshabilitado.
- Alcance explícito de Bedrock.

## Roadmap

1. Consolidar helpers y vectores canónicos sin introducir dependencias obligatorias.
2. Completar conformance de todos los tipos, File/Network, borrowed/owned y round-trips.
3. Ampliar streaming, vistas, errores y límites.
4. Completar SNBT e I/O, incluyendo fallos de filesystem y compresión.
5. Incorporar fixtures reales de Minecraft y un corpus de interoperabilidad reproducible.
6. Añadir fuzz smoke tests, sanitizers y benchmarks fuera de los umbrales funcionales estrictos de CI.
