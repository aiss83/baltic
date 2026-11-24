# baltic

[![CI](https://github.com/aiss83/baltic/actions/workflows/ci.yml/badge.svg)](https://github.com/aiss83/baltic/actions/workflows/ci.yml)
[![Docs](https://github.com/aiss83/baltic/actions/workflows/docs.yml/badge.svg)](https://aiss83.github.io/baltic/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Библиотека на C11 для перевода высот между эллипсоидальной (геодезической)
высотой **h** в WGS 84 и высотой **H** в Балтийской системе высот
(БСВ-77, Кронштадтский футшток).

Расчёт опирается на глобальную модель геоида **EGM2008** и на исходный код
**PROJ** (операция `vgridshift`, интерполяция вертикальных сеток, чтение
GeoTIFF/GTX). PROJ 9.8.1 включён в репозиторий как исходный код в
`third_party/proj/`, поэтому отдельная установка PROJ не требуется.

```
H = h - N(lat, lon) + C(lat, lon) + Δ
```

| Величина | Смысл | Источник |
|---|---|---|
| `h` | эллипсоидальная высота над WGS 84 | GNSS |
| `N` | высота геоида EGM2008 | сетка `us_nga_egm08_25.tif` |
| `C` | необязательная региональная поправка | ваша сетка (`--correction`) |
| `Δ` | постоянная поправка датума | калибровка по GNSS-нивелированию |

> **Важно.** По умолчанию `Δ = 0`, и библиотека возвращает ортометрическую
> высоту относительно геоида EGM2008, а **не** высоту БСВ-77. Балтийская
> система — это система нормальных высот с нулём Кронштадтского футштока;
> её смещение относительно EGM2008 и различие «геоид/квазигеоид» должны
> быть учтены поправками `Δ` и `C`. `Δ` оценивается по пунктам
> GNSS-нивелирования (см. [Калибровка](#калибровка-поправки-датума)).
> Без такой калибровки не используйте результат как высоту БСВ-77.

## Документация

- [docs/INDEX.md](docs/INDEX.md) — карта документации;
- [Архитектура](docs/ARCHITECTURE.md), [сборка](docs/BUILD.md),
  [API](docs/API.md), [CLI](docs/CLI.md),
  [калибровка](docs/CALIBRATION.md), [тесты](docs/TESTING.md),
  [FAQ](docs/FAQ.md);
- онлайн: <https://aiss83.github.io/baltic/>.

## Возможности

- Прямое и обратное преобразование `h ↔ H` с контролем покрытия сетки.
- Сетки EGM2008 2.5′ и 1′ (GeoTIFF), а также любые вертикальные сетки
  PROJ в форматах GeoTIFF/GTX.
- Необязательная поправочная сетка `C` (например, региональная модель
  квазигеоида или разность национальной модели и EGM2008).
- Калибровка постоянной поправки `Δ` методом наименьших квадратов
  (среднее residuals) и статистика RMS/min/max.
- Пакетное преобразование, потоковый CLI, CSV/JSONL.
- Потокобезопасный контекст: один контекст можно использовать из
  нескольких потоков.
- Тесты: синтетическая сетка с аналитически известными значениями,
  интеграционные проверки на реальном EGM2008 и сверка с `cct` из PROJ.

## Быстрый старт

### Зависимости

- CMake ≥ 3.20;
- компиляторы C11 и C++17 (PROJ собирается из исходников);
- libtiff (обязательно для GeoTIFF-сеток EGM2008);
- SQLite3 (требование PROJ);
- pthreads.

macOS (MacPorts):

```sh
sudo port install cmake tiff sqlite3
```

macOS (Homebrew):

```sh
brew install cmake libtiff sqlite
```

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libtiff-dev libsqlite3-dev
```

### Сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Если CMake не находит libtiff/SQLite3 в нестандартном префиксе (например,
MacPorts), добавьте `-DCMAKE_PREFIX_PATH=/opt/local`.

### Сетка EGM2008

Файл сетки не хранится в репозитории (80 МБ). Скачайте его:

```sh
./scripts/fetch-egm2008.sh
```

Скрипт кладёт `data/egm2008/us_nga_egm08_25.tif` (80 585 622 байт,
SHA-256 `4191d471eefebf24091b56dbc604353cb3b8cf8cc70e448bb9ae56a272bef17a`).
Подробности и альтернативные источники — в [data/README.md](data/README.md).

### CLI

```sh
$ ./build/baltic info --grid data/egm2008/us_nga_egm08_25.tif
path=/.../data/egm2008/us_nga_egm08_25.tif
format=gtiff
west_deg=-180.000000000
south_deg=-90.000000000
east_deg=179.958333333
north_deg=90.000000000
width=8640
height=4321
dlon_deg=0.041666667
dlat_deg=0.041666667
```

```sh
$ echo "37.6173,55.7558,200" | \
    ./build/baltic convert --grid data/egm2008/us_nga_egm08_25.tif --offset 0.35
lon,lat,ellipsoidal_h,baltic_h,egm2008_n,correction,offset,status
37.617300000,55.755800000,200.000000,185.808741,14.541259,0.000000,0.350000,ok
```

Формат входного CSV: `lon,lat,height` (долгота, широта, высота; десятичный
разделитель — точка). Заголовок и строки, начинающиеся с `#`, игнорируются. Обратное преобразование —
флаг `--reverse`, тогда на вход подаётся высота `H`, а на выходе — `h`.

```sh
$ ./build/baltic convert --grid ... --reverse --input H.csv --output h.csv
$ ./build/baltic convert --grid ... --format jsonl --input points.csv
$ ./build/baltic convert --grid ... --correction region.tif --offset 0.12
```

`--correction FILE` задаёт сетку `C`, значение которой **прибавляется** к
балтийской высоте. `--search-path DIR` добавляет каталог для поиска
относительных имён сеток и ресурсов PROJ. Это может быть разность «национальная модель − EGM2008»
или региональная модель квазигеоида.

### API

```c
#include <baltic/baltic.h>
#include <stdio.h>

int main(void) {
    baltic_config cfg = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    baltic_conversion_info info;
    double H = 0.0;
    baltic_status st;

    cfg.geoid_grid = "data/egm2008/us_nga_egm08_25.tif";
    cfg.datum_offset_m = 0.35;   /* результат калибровки */

    st = baltic_ctx_create(&cfg, &ctx);
    if (st != BALTIC_OK) {
        fprintf(stderr, "create: %s\n", baltic_last_error());
        return 1;
    }

    /* Внимание: аргументы API — (lat, lon, height). */
    st = baltic_ellipsoidal_to_baltic(ctx, 55.7558, 37.6173, 200.0, &H, &info);
    if (st != BALTIC_OK) {
        fprintf(stderr, "convert: %s\n", baltic_last_error());
        baltic_ctx_destroy(ctx);
        return 1;
    }

    printf("H = %.3f m (N = %.3f m)\n", H, info.geoid_undulation_m);
    baltic_ctx_destroy(ctx);
    return 0;
}
```

Сборка примера:

```sh
cc example.c -Iinclude -Lbuild -lbaltic -Wl,-rpath,"$PWD/build" -o example
```

Основные функции:

| Функция | Назначение |
|---|---|
| `baltic_ctx_create` / `baltic_ctx_destroy` | создание/уничтожение контекста |
| `baltic_ellipsoidal_to_baltic` | `h → H` |
| `baltic_baltic_to_ellipsoidal` | `H → h` |
| `baltic_convert` | преобразование с явным направлением |
| `baltic_convert_batch` | пакетное преобразование |
| `baltic_calibrate_offset` | оценка `Δ` по контрольным пунктам |
| `baltic_ctx_grid_info` | границы/разрешение сетки |
| `baltic_ctx_set_offset` | изменить `Δ` без пересоздания контекста |
| `baltic_last_error` | текст последней ошибки (thread-local) |

Полные контракты и комментарии — в [include/baltic/baltic.h](include/baltic/baltic.h).

## Калибровка поправки датума

Для каждого контрольного пункта с известной высотой БСВ-77 вычисляется
модельная высота `H_мод = h − N + C`, затем невязка `r = H_изв − H_мод`.
Оценка `Δ` — среднее невязок; RMS характеризует разброс.

```sh
$ cat points.csv
lon,lat,h,H
37.6173,55.7558,200.000,185.808741
30.3167,59.9500,150.000,134.399350

$ ./build/baltic calibrate --grid data/egm2008/us_nga_egm08_25.tif \
      --points points.csv
points_used=2
points_skipped=0
offset_m=0.350000123
rms_m=0.000000000
min_residual_m=...
max_residual_m=...
```

Значение `offset_m` передайте в `--offset` или `cfg.datum_offset_m`.
Калибровка не изменяет контекст; она возвращает оценку.

Рекомендации:

- не менее 3–5 пунктов, равномерно распределённых по району работ;
- использовать пункты с известными нормальными высотами БСВ-77
  (нивелирование I–II класса) и качественными GNSS-высотами;
- проверять RMS и остатки: большой разброс означает, что одной константы
  недостаточно и нужна региональная поправка `C`;
- при переходе в другой регион калибровку нужно повторить.

## Точность и ограничения

Подробный разбор формул, систем высот и бюджета ошибок — в
[docs/GEODESY.md](docs/GEODESY.md).

- Сетка 2.5′ даёт шаг ≈ 4.6 км; билинейная интерполяция PROJ — порядка
  сантиметров. Модельная точность EGM2008 — дециметровый уровень и
  зависит от региона.
- Основной источник ошибки практического результата — привязка к БСВ-77:
  постоянная `Δ`, различие геоид/квазигеоид и региональные особенности.
  Их поглощают `Δ` и `C`.
- Горизонтальные координаты должны быть в WGS 84. Если исходные данные в
  ПЗ-90.11, расхождение с WGS 84 мало для высот, но для строгих работ
  нужно предварительное преобразование горизонтальных координат.
- `N` вычисляется в точке с входными `lat/lon`; итерации не требуются,
  так как геоид меняется плавно.
- Точка вне покрытия сетки — ошибка `BALTIC_ERR_OUT_OF_GRID`. Для EGM2008
  покрытие глобальное; для региональных поправочных сеток это важно.
- Имена файлов сеток с пробелами не поддерживаются (каталоги с пробелами —
  поддерживаются). Переименуйте файл, если в пути есть пробелы.
- Библиотека не выполняет преобразование систем координат и не заменяет
  геодезическое обеспечение: это инструмент для вертикального преобразования.

## Сборка с системным PROJ

По умолчанию собирается PROJ из `third_party/proj`. Если в системе уже есть
PROJ ≥ 9 с заголовками, поддержкой GeoTIFF (`ENABLE_TIFF=ON`) и
`PROJConfig.cmake` или `proj.pc`:

```sh
cmake -S . -B build -DBALTIC_BUNDLED_PROJ=OFF -DCMAKE_PREFIX_PATH=/opt/local
```

Опции CMake:

| Опция | По умолчанию | Смысл |
|---|---|---|
| `BALTIC_BUNDLED_PROJ` | `ON` | собирать PROJ из `third_party/proj` |
| `BALTIC_BUILD_CLI` | `ON` | собрать `baltic` |
| `BALTIC_BUILD_TESTS` | `ON` | собрать тесты |
| `BALTIC_BUILD_EMBEDDED` | `ON` | собрать `baltic_embedded` |
| `BALTIC_BUILD_GENERATOR` | `ON` | собрать `egm2008_to_c` (host) |
| `BALTIC_INSTALL` | `ON` | правила установки |

Установка:

```sh
cmake --install build --prefix /usr/local
```

При `BALTIC_BUNDLED_PROJ=ON` вместе с библиотекой устанавливается
`libproj` (только runtime-библиотека; заголовки PROJ, `proj.db` и утилиты
не нужны для работы `baltic`). Для установленной `libbaltic` прописан
RPATH `@loader_path` (macOS) / `$ORIGIN` (Linux), поэтому `libproj`
находится рядом в каталоге `lib`.

Использование через pkg-config:

```sh
cc example.c $(pkg-config --cflags --libs baltic) -o example
```

## Встраиваемая версия (MCU, ограниченная Flash)

В каталоге [embedded/](embedded/README.md) находится самостоятельная
версия без PROJ, libtiff, SQLite, heap и файлового ввода-вывода:
только C99 и целочисленная арифметика.

- высота — `int32_t` в миллиметрах, координаты — микроградусы;
- сетка EGM2008 вырезается под регион и хранится как `const int16_t[]`
  в Flash (1 мм на узел, относительно базового смещения);
- код: **868 B** на Cortex-M7 (`-O2`), **846 B** на Cortex-M4,
  **880 B** на Cortex-M0+ (плюс libgcc для деления), `.data/.bss` — 0;
- регион 2° × 2° с шагом 2.5′ — **4.8 КБ** данных, всего ~5.7 КБ;
- для STM32H743 (2 МБ Flash) 10° × 10° — 116 КБ, 30° × 30° — 1.04 МБ;
- генератор `egm2008_to_c` входит в host-сборку.

```sh
./build/egm2008_to_c --grid data/egm2008/us_nga_egm08_25.tif \
    --name egm2008_moscow_2deg --out-dir embedded/grids \
    --lat-min 54 --lat-max 56 --lon-min 36 --lon-max 38
cmake -S embedded -B build-embedded
cmake --build build-embedded
```

Подробности, таблица размеров и пример интеграции — в
[embedded/README.md](embedded/README.md). Для STM32H743/Cortex-M7 есть
отдельный порт с linker-фрагментами и настройкой кэша:
[embedded/ports/stm32h743/README.md](embedded/ports/stm32h743/README.md).

## Структура репозитория

```
include/baltic/baltic.h   публичный API
src/baltic.c              реализация
tools/baltic.c            CLI
tests/                    unit/integration/CLI тесты
embedded/                 вариант для MCU + генератор сеток
third_party/proj/         PROJ 9.8.1 (исходный код, MIT)
data/                     место для сетки EGM2008 (не коммитится)
scripts/                  загрузка сетки
docs/                     геодезические пояснения
```

## Лицензии

- Код библиотеки и CLI — MIT, см. [LICENSE](LICENSE).
- PROJ — MIT/X11, см. `third_party/proj/COPYING`.
- EGM2008 — модель NGA; условия использования и источники сеток описаны в
  [data/README.md](data/README.md).

Проект не является официальным продуктом NGA, OSGeo/PROJ или
Росреестра; ответственность за применение в производственных работах
лежит на пользователе.
