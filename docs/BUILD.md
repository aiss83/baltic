# Сборка и установка

## 1. Требования

| Компонент | Версия / примечание |
|---|---|
| CMake | ≥ 3.20 |
| Компилятор C | C11 для `libbaltic`, C99 для `baltic_embedded` |
| Компилятор C++ | C++17 — для встроенного PROJ |
| libtiff | обязателен для GeoTIFF-сеток |
| SQLite3 | ≥ 3.11, требование PROJ |
| pthreads | для полной библиотеки |
| Doxygen + Graphviz | только для документации |

macOS (MacPorts):

```sh
sudo port install cmake tiff sqlite3 doxygen graphviz
```

macOS (Homebrew):

```sh
brew install cmake libtiff sqlite doxygen graphviz
```

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config \
    libtiff-dev libsqlite3-dev doxygen graphviz
```

## 2. Обычная сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Если зависимости установлены в нестандартный префикс (MacPorts —
`/opt/local`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/local
```

## 3. PROJ: встроенный или системный

По умолчанию собирается PROJ 9.8.1 из `third_party/proj`:

```sh
cmake -S . -B build -DBALTIC_BUNDLED_PROJ=ON
```

Это занимает несколько минут: компилируется C++-код PROJ. Готовый
`libproj` устанавливается рядом с `libbaltic`; `proj.db`, утилиты и
заголовки PROJ не устанавливаются, поскольку `baltic` их не использует.

Системный PROJ ≥ 9:

```sh
cmake -S . -B build -DBALTIC_BUNDLED_PROJ=OFF \
    -DCMAKE_PREFIX_PATH=/usr/local
```

CMake сначала ищет `PROJConfig.cmake`, затем `proj.pc` (pkg-config).
Системный PROJ должен быть собран с GeoTIFF (`ENABLE_TIFF=ON`).

## 4. Опции CMake

| Опция | По умолчанию | Назначение |
|---|---|---|
| `BALTIC_BUNDLED_PROJ` | `ON` | собирать PROJ из `third_party/proj` |
| `BALTIC_BUILD_CLI` | `ON` | утилита `baltic` |
| `BALTIC_BUILD_TESTS` | `ON` | host-тесты |
| `BALTIC_BUILD_EMBEDDED` | `ON` | библиотека `baltic_embedded` |
| `BALTIC_EMBEDDED_BUILD_TESTS` | `ON` | тесты встроенной библиотеки |
| `BALTIC_BUILD_GENERATOR` | `ON` | host-генератор `egm2008_to_c` |
| `BALTIC_INSTALL` | `ON` | правила установки |

## 5. Установка

```sh
cmake --install build --prefix /usr/local
```

Устанавливаются:

- `lib/libbaltic.*`;
- `lib/libproj.*` (если `BALTIC_BUNDLED_PROJ=ON`);
- `bin/baltic`;
- `include/baltic/baltic.h`;
- `lib/pkgconfig/baltic.pc`;
- документация в `share/doc/baltic`.

`baltic.pc` использует `${pcfiledir}`, поэтому работает и при установке
в нестандартный префикс:

```sh
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
cc example.c $(pkg-config --cflags --libs baltic) -o example
```

RPATH установленной библиотеки — `@loader_path` (macOS) или `$ORIGIN`
(Linux), что позволяет найти `libproj` рядом в `lib/`.

## 6. Встраиваемая сборка

Только `baltic_embedded`, без PROJ:

```sh
cmake -S embedded -B build-embedded -DCMAKE_BUILD_TYPE=Release
cmake --build build-embedded -j
ctest --test-dir build-embedded --output-on-failure
```

Cortex-M7 (STM32H743):

```sh
cmake -S embedded -B build-m7 \
    -DCMAKE_TOOLCHAIN_FILE=embedded/cmake/arm-none-eabi-gcc-cortex-m7.cmake \
    -DBALTIC_EMBEDDED_BUILD_TESTS=OFF
cmake --build build-m7 -j
```

Нужен `arm-none-eabi-gcc`; путь задаётся через `ARM_TOOLCHAIN_PREFIX`.
Подробности, linker-фрагменты и кэш — в
[STM32H743 / Cortex-M7](https://github.com/aiss83/baltic/blob/main/embedded/ports/stm32h743/README.md).

## 7. Документация

```sh
doxygen Doxyfile
# результат: docs/doxygen/html/index.html
```

На GitHub Pages документация собирается workflow `.github/workflows/docs.yml`.

## 8. Санитайзеры

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

## 9. Типичные проблемы

**CMake не находит TIFF/SQLite3.** Укажите
`-DCMAKE_PREFIX_PATH=/opt/local` (MacPorts) или `/usr/local` (Homebrew).

**Долго собирается.** Это PROJ. Для повторных сборок используйте тот же
build-каталог; для установки системного PROJ —
`-DBALTIC_BUNDLED_PROJ=OFF`.

**`projsync requires Curl`.** Не должно возникать: в проекте
`ENABLE_CURL=OFF` и `BUILD_APPS=OFF`. Если вы собираете PROJ отдельно,
добавьте `-DBUILD_PROJSYNC=OFF`.

**Сетка не находится.** Проверьте путь и `--search-path`; имена файлов
с пробелами не поддерживаются (каталоги с пробелами — поддерживаются).

**Тесты `egm2008`/`cli` пропущены.** Нет файла
`data/egm2008/us_nga_egm08_25.tif`; выполните
`./scripts/fetch-egm2008.sh`. API-тест и embedded-тест работают без него.

**`find_package(PROJ)` не находит системный PROJ.** Убедитесь, что
установлен `proj.pc` (`pkg-config --modversion proj`) и GeoTIFF включён.

## 10. Версии

- проект: 0.1.0;
- встроенный PROJ: 9.8.1 (см. [VENDORING](VENDORING.md));
- минимальные API: CMake 3.20, C11, C++17, PROJ 9.
