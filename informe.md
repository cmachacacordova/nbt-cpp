## Resultado de la revisión

Revisé los headers principales, utilities, CMake y ambas suites de tests. No modifiqué archivos y preservé los cambios existentes del working tree.

### Verificación ejecutada

- Compilación Debug: correcta.
- 19/19 tests Debug: correctos.
- Compilación Release: correcta.
- 19/19 tests Release: correctos.
- `git diff --check`: correcto.
- `clang-tidy`: no disponible en el entorno.

Que los tests pasen no descarta los problemas siguientes: actualmente no cubren varios caminos críticos.

---

# Hallazgos críticos

## 1. Corrupción de memoria al hacer `append()` sobre entrada prestada

Un `Buffer` construido desde un `span` tiene `size_ > 0` pero `capacity_ == 0`. En `preallocate()`:

```cpp
const auto tailroom = capacity_ - size_;
```

La resta unsigned desborda y produce un valor enorme. Como consecuencia, `preallocate()` cree que existe espacio disponible y devuelve `data_ + size_`. El posterior `memcpy` escribe fuera del buffer prestado, que además puede ser memoria de solo lectura.

Esto puede suceder con un flujo natural:

```cpp
auto document = Nbt::parseAtMost(borrowedPartialSpan);
document.append(nextChunk);
```

Evidencia: `@C:\Workspace\nbt-cpp\include\nbt\buffer.h:55-67`  
La llamada desde `append`: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:597-615`

**Corrección recomendada:** diferenciar explícitamente almacenamiento prestado y propio. Al crecer un buffer prestado, reservar almacenamiento propio y copiar los bytes existentes antes de añadir el fragmento.

---

## 2. `parseAtMost(container)` no cumple su contrato para datos truncados

La sobrecarga de container llama primero a:

```cpp
document.append(view);
```

Pero `append()` ejecuta `validate(false)`, que lanza `NeedMoreDataException`. Por ello nunca se alcanza el posterior `validate(atMost)` y `parseAtMost(vectorTruncado)` lanza en vez de devolver `Status::NeedMoreData`.

Evidencia: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:252-273`  
Validación forzada desde `append`: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:602-615`

El test de truncamientos no lo detecta porque convierte expresamente cada entrada a `span`, seleccionando la sobrecarga prestada: `@C:\Workspace\nbt-cpp\tests\nbt_tests.cpp:254-260`

**Corrección recomendada:** copiar el container directamente al almacenamiento interno y llamar una sola vez a `validate(atMost)`.

---

## 3. No se pueden codificar listas válidas de arrays

`listSize()` solo calcula payloads de escalares, strings, listas y compounds. Omite:

- `Type::ByteArray`
- `Type::IntArray`
- `Type::LongArray`

Para una lista de arrays devuelve prácticamente solo los cinco bytes del encabezado. Después `appendPayload()` intenta escribir todos los arrays y `BufferWriter` lanza overflow. Estas listas son NBT Java válidas.

Evidencia: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:1036-1073`

**Corrección recomendada:** calcular cada payload mediante `payloadSize(element)` para todos los tipos y validar uniformemente `element.type == parent.elementType`.

---

## 4. Puede generarse una lista inválida no vacía de `TAG_End`

El parser rechaza correctamente una lista no vacía cuyo tipo sea `End`, pero el encoder no lo hace. `listSize()` y `appendPayload()` permiten una lista de elementos `Tag{}` con `elementType == End`, generando NBT que la propia biblioteca rechaza al leer.

Parser: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:810-829`  
Encoder: `@C:\Workspace\nbt-cpp\include\nbt\nbt.h:1193-1203`

**Corrección recomendada:** rechazar `elementType == Type::End` cuando la lista no está vacía.

---

# Hallazgos altos

## 5. El contrato documentado y la implementación pública divergen

Las instrucciones del proyecto establecen:

- `Options::requireCompleteInput`, por defecto `true`.
- `options.format`.
- `append(chunk, options)`.
- Rechazo de bytes finales por defecto.

`@C:\Workspace\nbt-cpp\AGENTS.md:14-37`

Sin embargo:

- `Options` no contiene `requireCompleteInput` ni `format`.
- La API recibe `Source` como argumento separado.
- `append` no recibe opciones.
- El parser acepta silenciosamente bytes finales.
- Existe un test que exige que los bytes finales sean aceptados.

`@C:\Workspace\nbt-cpp\tests\nbt_tests.cpp:180-215`

Esto es una incompatibilidad de contrato, no solamente documentación atrasada.

**Recomendación:** decidir cuál es la API definitiva y alinear implementación, README, ejemplos, tests y `AGENTS.md`. Según las reglas actuales, la implementación es la que debe ajustarse.

---

## 6. `append()` no funciona como acumulador no excepcional

Cada fragmento incompleto provoca `NeedMoreDataException`, aunque el objeto queda marcado como `NeedMoreData`. Para un API incremental, los cortes ordinarios de paquetes no deberían ser excepcionales.

El test actual incluso consolida esa semántica:

`@C:\Workspace\nbt-cpp\tests\nbt_tests.cpp:165-178`

Esto contradice la descripción de `append` como acumulación de fragmentos.

**Recomendación:** validar en modo “at most” dentro de `append`; devolver normalmente con `Status::NeedMoreData` y reservar excepciones para entradas malformadas o límites excedidos.

---

## 7. Descompresión sin aplicar `maxInputBytes`: posible agotamiento de memoria

`load()` descomprime completamente antes de validar el límite de entrada. Un archivo comprimido pequeño puede expandirse enormemente y agotar memoria antes de que el parser compruebe `maxInputBytes`.

`@C:\Workspace\nbt-cpp\include\nbt\utilities.h:70-81`  
Bucle sin límite: `@C:\Workspace\nbt-cpp\include\nbt\utilities.h:514-538`

**Corrección recomendada:** pasar el límite a `inflate()` y detener la descompresión antes de que la salida exceda `options.maxInputBytes`.

---

## 8. ZLIB trunca silenciosamente entradas superiores a `uInt`

Se hace:

```cpp
stream.avail_in = static_cast<uInt>(input.size());
```

Si `size_t` es mayor que el máximo de `uInt`, se procesa solo una parte de la entrada. Afecta tanto compresión como descompresión.

`@C:\Workspace\nbt-cpp\include\nbt\utilities.h:514-565`

**Corrección recomendada:** alimentar ZLIB por bloques de tamaño máximo `uInt`.

---

## 9. Falta RAII para `z_stream`

Si `output.append()` o alguna otra operación lanza después de `inflateInit2`/`deflateInit2`, no se ejecuta `inflateEnd`/`deflateEnd`.

Esto puede filtrar recursos internos de ZLIB.

**Corrección recomendada:** encapsular `z_stream` en un guard RAII que invoque automáticamente la función de cierre apropiada.

---

## 10. `NbtUtilities::parseFile<BufferT>` usa el tipo concreto equivocado

Dentro de la función template aparece:

```cpp
Nbt document = nbt::NbtParser<BufferT>::parse(...);
```

`Nbt` es `NbtParser<nbt::Buffer>`, no `NbtParser<BufferT>`. Para un buffer personalizado compatible, la instanciación puede no compilar.

`@C:\Workspace\nbt-cpp\include\nbt\utilities.h:58-81`

**Corrección recomendada:**

```cpp
auto document = nbt::NbtParser<BufferT>::parse(...);
```

o declarar expresamente `nbt::NbtParser<BufferT>`.

---

# Código muerto, variables inútiles y redundancia

## 11. `Status::Error` nunca se asigna

Existe una rama específica en `root()`, pero no encontré ninguna asignación a `Status::Error`. Los errores malformados simplemente propagan una excepción.

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:642-649`

Opciones:

- Eliminar `Status::Error` y su rama.
- O asignarlo al capturar errores de validación si se pretende mantener un documento inválido inspeccionable.

---

## 12. Parámetro `parent` completamente muerto

`beginNode()` recibe `parent`, pero solo ejecuta:

```cpp
(void)parent;
```

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:846-859`

No existe un enlace al padre en `Node`. Puede eliminarse de la firma y de todas las llamadas.

---

## 13. Validación duplicada en parseo de containers

La ruta realiza:

1. `append(view)` → `validate(false)`.
2. `validate(atMost)` otra vez.

Además de romper `parseAtMost`, reindexa todo dos veces en entradas completas.

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:252-273`

---

## 14. Variables structured-binding no utilizadas

Se ignora `available` o `size` en distintos puntos:

- `Buffer::append`
- fast path de `encode`
- `readFile`

Ejemplo: `@C:\Workspace\nbt-cpp\include\nbt\buffer.h:98-108`

Puede usarse `auto allocation = ...` o `auto [buffer, _]`, según la política de warnings, aunque esto es cosmético.

---

## 15. `find()` tiene complejidad cuadrática

`find()` llama `child(index)` para cada posición, y `child(index)` recorre desde el primer hermano. Buscar el último hijo cuesta O(n²).

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:445-465`

**Simplificación:** recorrer una sola vez mediante el iterador o seguir directamente `nextSibling`. Esto deja `find()` en O(n).

---

## 16. Materialización de containers también es O(n²)

Los bucles de `materialize()` llaman repetidamente `child(index)`, generando el mismo patrón cuadrático.

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:990-1007`

Puede simplificarse con range iteration sobre el `View`.

---

# Mejoras adicionales de robustez

## 17. Operaciones inseguras en `ArrayView`

- `operator[]` no comprueba rango.
- `front()` sobre array vacío lee fuera de rango.
- `back()` sobre vacío hace `size() - 1`, que desborda.

`@C:\Workspace\nbt-cpp\include\nbt\nbt.h:328-359`

Si se desea semántica estilo STL, `operator[]` puede continuar sin comprobación, pero convendría añadir `at()` y documentar las precondiciones de `front()`/`back()`.

---

## 18. Los límites SNBT están aplicados parcialmente

`parseSnbt` usa `maxDepth` y `maxContainerElements`, pero ignora:

- `maxInputBytes`
- `maxTotalNodes`

Por tanto, `Options` no ofrece límites uniformes entre NBT binario y SNBT.

`@C:\Workspace\nbt-cpp\include\nbt\utilities.h:37-49`

---

## 19. Riesgos de overflow en cálculos de tamaño

Operaciones como:

```cpp
values.size() * sizeof(T)
size += payloadSize(element)
((needed / 64) + 1) * 64
```

no están verificadas de forma general. En plataformas de 64 bits son difíciles de alcanzar legítimamente, pero una API de serialización debería usar suma y multiplicación comprobadas.

---

# Tests prioritarios que faltan

Recomiendo añadir, en este orden:

1. **Regresión de memoria prestada**
   - `parseAtMost(span parcial)` seguido de `append(resto)`.
   - Verificar que pasa a almacenamiento propio y no modifica la fuente.

2. **Todas las sobrecargas de truncamiento**
   - `parseAtMost(span)`.
   - `parseAtMost(vector)`.
   - Cada punto de corte.

3. **Listas de todos los tipos NBT**
   - Especialmente listas de `ByteArray`, `IntArray`, `LongArray`, listas y compounds.
   - Round-trip y bytes exactos.

4. **Lista `End`**
   - Vacía válida.
   - No vacía rechazada antes de escribir salida.

5. **Trailing bytes**
   - Por defecto rechazados.
   - Aceptados únicamente con `requireCompleteInput == false`.
   - Verificar qué devuelve `bytes()` y qué preserva `encode()`.

6. **Incremental real**
   - Añadir un documento byte por byte sin excepciones.
   - File y Network.
   - Fragmento malformado debe lanzar con offset correcto.

7. **Offsets de error**
   - Actualmente casi ningún test comprueba `Exception::offset()`.

8. **Límites en fronteras**
   - `limit - 1`, `limit`, `limit + 1` para los cuatro límites.

9. **Buffers personalizados**
   - Instanciar `NbtParser<CustomBuffer>`.
   - `parse`, `append`, `encode`, `NbtUtilities::parseFile/save`.

10. **Compresión**
    - Salida descomprimida superior a `maxInputBytes`.
    - Datos gzip/zlib truncados.
    - Nivel de compresión inválido.
    - Entrada dividida en múltiples bloques de ZLIB.

11. **Valores canónicos**
    - Límites enteros.
    - `+0.0` y `-0.0` por bits.
    - Subnormales, infinitos y NaN.
    - Strings de 0, 65535 y 65536 bytes.
    - Arrays vacíos y compounds con nombres repetidos.

12. **Sanitizers/fuzzing**
    - ASan + UBSan en Linux/Clang o GCC.
    - Fuzzer centrado en parser binario y SNBT.

## Prioridad recomendada de corrección

1. Corrupción de memoria en `Buffer::preallocate`.
2. `parseAtMost(container)` y semántica incremental.
3. Codificación de listas de arrays y listas `End`.
4. Límite de descompresión y RAII de ZLIB.
5. Alinear el contrato público (`Options`, trailing bytes y format).
6. Eliminar código muerto y recorridos O(n²).

---

## Actualización tras revisión posterior

### Código muerto eliminado

- Se eliminó `Status::Error` del enum: nunca se asignaba en la implementación actual.
- Se eliminó el parámetro `parent` de `beginNode()` y `parseNamed()`: no existía enlace al padre en `Node` y solo se silenciaba con `(void)parent`.
- Se eliminó la variable `available` no usada del structured binding en `NbtUtilities::readFile()`.
- Se eliminó la rama `else` inalcanzable del constructor `Tag(Name&&, std::vector<T>)`, pues `static_assert` cubre todos los tipos soportados.

### Reevaluación de hallazgos previos

1. **Corrupción de memoria en `Buffer::preallocate`**: *No aplica a la versión actual*. `preallocate` detecta `capacity_ == 0` y reserva nuevo almacenamiento propio antes de copiar los bytes existentes, por lo que nunca escribe sobre el buffer prestado. El riesgo descrito en este informe original correspondía a una implementación anterior.

### Verificación ejecutada tras los cambios

- Compilación Debug: correcta, sin advertencias de parámetros sin referencia.
- Tests Debug: 21/21 correctos (utilities habilitadas).
- Compilación Release: correcta.
- Tests Release: 21/21 correctos.
- Build adicional con `NBT_CPP_BUILD_UTILITIES=OFF`: 17/17 tests core correctos.
- `git diff --check`: correcto.

### Problemas que siguen vigentes

1. **Divergencia entre documentación e implementación**: `README.md`/`AGENTS.md` mencionan `Options::format` y `Options::requireCompleteInput`, pero ninguno existe en `Options`. El parser acepta bytes finales en lugar de rechazarlos por defecto.
2. **`NbtUtilities::parseFile<BufferT>` fuerza `nbt::Nbt`**: usa `Nbt document = nbt::NbtParser<BufferT>::parse(...)` en lugar de `nbt::NbtParser<BufferT>`, rompiendo buffers personalizados.
3. **ZLIB trunca entradas grandes**: `stream.avail_in = static_cast<uInt>(input.size())` trunca silenciosamente si `input.size() > UINT_MAX`.
4. **Descompresión sin límite de memoria**: `inflate()` no aplica `maxInputBytes` mientras expande, permitiendo agotar memoria.
5. **Fuga de recursos ZLIB**: si `output.append` o `::inflate`/`::deflate` lanzan, no se ejecuta `inflateEnd`/`deflateEnd`.
6. **Encoder no soporta listas de arrays**: `listSize()` no contempla `ByteArray`/`IntArray`/`LongArray` como elementos de lista, subasignando memoria.
7. **Encoder permite listas inválidas de `TAG_End`**: el parser las rechaza, pero el encoder las genera.
8. **Recorridos cuadráticos**: `View::find()` y la materialización de listas/compounds usan repetidamente `child(index)`, resultando en `O(n²)`.
9. **`ArrayView::front()`/`back()` sin precondiciones**: sobre arrays vacíos acceden fuera de rango.
10. **`append()` sigue siendo excepcional para datos truncados**: cada fragmento incompleto lanza `NeedMoreDataException`, contradiciendo una semántica incremental limpia.
11. **SNBT aplica límites parcialmente**: respeta `maxDepth` y `maxContainerElements`, pero ignora `maxInputBytes` y `maxTotalNodes`.