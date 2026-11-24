# Тестирование

## 1. Состав тестов

| Тест | Файл | Что проверяет | Нужна сетка EGM2008 |
|---|---|---|---|
| `api` | `tests/test_baltic.c` | API, offset, batch, калибровка, потоки, ошибки | нет (синтетическая GTX 3×3) |
| `egm2008` | `tests/test_egm2008.c` | реальные значения EGM2008, round-trip, калибровка | да |
| `cli` | `tests/test_cli.sh` | convert/info/calibrate, CSV/JSONL, коды возврата | да |
| `embedded` | `embedded/tests/test_embedded.c` | целочисленная интерполяция, границы, offset, correction | нет (встроенные сетки) |

При отсутствии `data/egm2008/us_nga_egm08_25.tif` тесты `egm2008` и
`cli` завершаются кодом 77 и CTest помечает их как skipped.

## 2. Запуск

```sh
./scripts/fetch-egm2008.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Ожидаемо:

```text
1/4 Test #1: api ....... Passed
2/4 Test #2: egm2008 ... Passed
3/4 Test #3: cli ....... Passed
4/4 Test #4: embedded .. Passed
100% tests passed
```

Только встроенная библиотека:

```sh
cmake -S embedded -B build-embedded
cmake --build build-embedded -j
ctest --test-dir build-embedded --output-on-failure
```

## 3. Синтетическая сетка

`tests/test_baltic.c` создаёт GTX 3×3 с линейным полем

```text
N(lon, lat) = 10 + 2·(lon − 10) + 3·(lat − 50)
```

Билинейная интерполяция линейного поля точна, поэтому проверяются
аналитические значения в узлах и серединах ячеек. Это позволяет
тестировать API без 80-МБ файла.

Проверяются также:

- нормализация долготы (`370.5°`, `−349.5°`);
- выход за границы;
- пакетная обработка с частичными ошибками;
- калибровка `Δ` и пропуск точек вне сетки;
- коррекция `C`;
- параллельные вызовы из 4 потоков;
- ошибки конфигурации и повреждённой сетки.

## 4. Эталоны EGM2008

Значения получены независимо через `cct` из PROJ:

```sh
echo "37.6173 55.7558 0" | cct -d 12 \
  +proj=pipeline \
  +step +proj=unitconvert +xy_in=deg +xy_out=rad \
  +step +proj=vgridshift +grids=us_nga_egm08_25.tif +multiplier=1 \
  +step +proj=unitconvert +xy_in=rad +xy_out=deg
```

Контрольные точки:

| Точка | Широта | Долгота | N, м |
|---|---|---|---|
| Москва | 55.7558 | 37.6173 | 14.541259 |
| Санкт-Петербург | 59.9500 | 30.3167 | 15.950650 |
| Кронштадт | 60.0000 | 29.7667 | 15.741389 |
| Калининград | 54.7100 | 20.5100 | 27.023646 |
| Сочи | 43.6028 | 39.7303 | 18.652608 |
| Новосибирск | 55.0302 | 82.9204 | −36.230658 |
| Владивосток | 43.1155 | 131.8855 | 26.196278 |

Допуск в тесте — `1e-4` м. Значение в точке (0°, 0°) — 17.225084 м.

## 5. Сверка на случайных точках

Полная библиотека сверялась с `cct` на 500 случайных точках по всему
миру: максимальное расхождение **5·10⁻⁷ м** (ограничение вывода CLI,
6 знаков).

Встроенная библиотека сверялась с полной на 5 000 случайных точках
региона Москвы:

```text
points=5000 bad=0 max_diff=1.000 mm mean=0.286 mm
```

Расхождение обусловлено квантованием 1 мм и округлением fixed-point.

## 6. Санитайзеры и утечки

ASan/UBSan:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

На macOS дополнительно:

```sh
leaks --atExit -- ./build/tests/test_baltic
# 0 leaks for 0 total leaked bytes
```

## 7. Кросс-компиляция

```sh
./embedded/tools/size_report.sh
```

Ожидаемо:

```text
cortex-m7 (-O2):     text 868  data 0  bss 0
cortex-m4 (-Os):     text 846  data 0  bss 0
cortex-m0plus (-Os): text 880  data 0  bss 0
```

Для M7 дополнительно проверяется секция:

```sh
clang --target=arm-none-eabi -mcpu=cortex-m7 -mthumb -O2 \
    -ffreestanding -Iembedded/include \
    -c embedded/grids/egm2008_moscow_2deg.c -o /tmp/grid.o
llvm-objdump -h /tmp/grid.o | grep baltic_grid
```

## 8. CI

GitHub Actions:

- `.github/workflows/ci.yml` — сборка и все тесты на Ubuntu, включая
  загрузку EGM2008 с кэшем, плюс кросс-сборка Cortex-M7;
- `.github/workflows/docs.yml` — Doxygen и публикация на GitHub Pages.

Локальный аналог CI:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```
