# STM32H743 / Cortex-M7

Порт `baltic_embedded` для STM32H743 (Cortex-M7, 480 МГц, 2 МБ Flash,
1 МБ RAM) и близких MCU. Библиотека не зависит от HAL, CMSIS, FreeRTOS
или конкретного загрузчика: это статическая библиотека и несколько
`const`-массивов.

## Что важно для H743

- Код и сетка помещаются во внутренний Flash; D-cache/I-cache сильно
  ускоряют выборку.
- Сетка может быть вынесена в memory-mapped QSPI Flash (0x90000000) —
  удобно для больших регионов.
- ITCM/DTCM не обязательны: код занимает <1 КБ, статической RAM нет.
- FPU M7 не используется: преобразование целочисленное, поэтому его
  можно вызывать из ISR без сохранения FPU-контекста.
- Нет `malloc`, файловых операций и блокировок — функции реентерабельны.

## Сборка

GNU Arm Embedded (рекомендуется, тот же компилятор, что в STM32CubeIDE):

```sh
cmake -S embedded -B build-m7 \
    -DCMAKE_TOOLCHAIN_FILE=embedded/cmake/arm-none-eabi-gcc-cortex-m7.cmake \
    -DBALTIC_EMBEDDED_BUILD_TESTS=OFF

cmake --build build-m7 -j
```

Если `arm-none-eabi-gcc` не в `PATH`:

```sh
cmake -S embedded -B build-m7 \
    -DCMAKE_TOOLCHAIN_FILE=embedded/cmake/arm-none-eabi-gcc-cortex-m7.cmake \
    -DARM_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain/bin/arm-none-eabi- \
    -DBALTIC_EMBEDDED_BUILD_TESTS=OFF
```

Clang-вариант — [embedded/cmake/arm-none-eabi-clang-cortex-m7.cmake](../../cmake/arm-none-eabi-clang-cortex-m7.cmake);
для него нужны `llvm-ar`/`llvm-ranlib` (в Xcode их нет).

В STM32CubeIDE/Keil проще добавить в проект файлы:

- `embedded/src/baltic_embedded.c`;
- `embedded/include`;
- выбранные `embedded/grids/*.c` и `*.h`.

Флаги: `-O2 -ffunction-sections -fdata-sections`, линковка с
`-Wl,--gc-sections`.

## Размещение сетки

Сгенерированные массивы имеют атрибут
`__attribute__((section(".baltic_grid"), aligned(32), used))` (GCC/Clang,
ELF). Это позволяет независимо управлять их размещением.

### Внутренний Flash

Добавьте в linker script фрагмент
[linker/internal_flash_grid.ld](linker/internal_flash_grid.ld):

```ld
.baltic_grid : ALIGN(32)
{
    __baltic_grid_start = .;
    KEEP(*(.baltic_grid))
    KEEP(*(.baltic_grid.*))
    __baltic_grid_end = .;
} > FLASH
```

Секция уникальна, поэтому её можно ставить в любом месте после `MEMORY`,
не меняя `.rodata`.

### QSPI Flash

1. Включите QSPI в memory-mapped mode (например, 0x90000000).
2. Добавьте регион:

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2048K
    QSPI  (rx) : ORIGIN = 0x90000000, LENGTH = 32768K
    ...
}
```

3. Подключите [linker/qspi_grid.ld](linker/qspi_grid.ld).
4. Настройте external loader STM32CubeProgrammer для записи `.baltic_grid`.
5. Включите D-cache и MPU: QSPI-регион — read-only, cacheable, XN
   (если код оттуда не исполняется). После прошивки инвалидируйте
   D-cache, чтобы не прочитать старые строки.

Для случайного доступа к сетке кэш важнее, чем сама частота QSPI:
билинейная интерполяция читает 4 узла из двух соседних строк, поэтому
соседние преобразования обычно попадают в кэш.

Аналогично можно разместить сетку в SDRAM через FMC (например,
0xC0000000): настройте MPU-регион как read-only/cacheable и обеспечьте
инициализацию SDRAM до первого обращения.

## Сколько сетки влезает

H743 имеет 2 МБ внутреннего Flash; оставьте место прошивке и загрузчику.

| Регион | Шаг | Узлы | Данные |
|---|---|---|---|
| 2° × 2° | 2.5′ | 2 401 | 4.8 КБ |
| 10° × 10° | 2.5′ | 58 081 | 116 КБ |
| 20° × 20° | 2.5′ | 231 361 | 463 КБ |
| 30° × 30° | 2.5′ | 519 841 | 1.04 МБ |
| 5° × 5° | 1′ | 90 601 | 181 КБ |
| 10° × 10° | 1′ | 361 201 | 724 КБ |

Код: 868 Б при `-O2` (Cortex-M7), 846 Б при `-Os`; `.data/.bss` — 0.
Сетка 30° × 30° с шагом 2.5′ оставляет около 1 МБ Flash под прошивку.

Если linker script делит Flash H743 на два банка по 1 МБ, сетка больше
1 МБ не поместится в один регион `FLASH`; используйте QSPI или опишите
непрерывный регион на оба банка.

Пример генерации для 10° × 10°:

```sh
./build/egm2008_to_c --grid data/egm2008/us_nga_egm08_25.tif \
    --name egm2008_ural_10deg --out-dir embedded/grids \
    --lat-min 52 --lat-max 62 --lon-min 55 --lon-max 65
```

## Кэш, MPU и производительность

- Включите I-cache и D-cache после настройки MPU:
  `SCB_EnableICache(); SCB_EnableDCache();`
- Внутренний Flash: обычная cacheable-память, ничего специально делать
  не нужно.
- QSPI: MPU-регион read-only, non-shared, cacheable; при необходимости
  отметьте XN.
- DMA с сеткой не используется, cache maintenance не требуется.
- Код можно разместить в ITCM (64 КБ) для предсказуемости, но при
  <1 КБ кода и I-cache выигрыш невелик.

Ориентир: одно преобразование — 4 аппаратных деления (`sdiv`/`udiv`),
около 150–250 инструкций и несколько обращений к сетке. На 480 МГц это
ориентировочно **0.3–1 мкс** при попадании в кэш; точное значение
зависит от тактирования Flash, кэша и размещения сетки. Измеряйте DWT:

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

uint32_t start = DWT->CYCCNT;
for (uint32_t i = 0; i < 100000; ++i) {
    baltic_embedded_h_to_H(&grid, NULL, lat_u6, lon_u6, h_mm, offset_mm, &H_mm);
}
uint32_t cycles = DWT->CYCCNT - start;
```

## Интеграция с приложением

```c
#include "baltic_embedded.h"
#include "egm2008_moscow_2deg.h"

int32_t H_mm;
baltic_embedded_status st = baltic_embedded_h_to_H(
    &egm2008_moscow_2deg, NULL,
    55755800, 37617300,    /* 55.755800°N, 37.617300°E */
    250000, 350,           /* h = 250.000 м, offset = 0.350 м */
    &H_mm);
if (st == BALTIC_EMBEDDED_ERR_OUTSIDE) {
    /* точка вне региона: использовать другую сетку/модель */
}
```

`Δ` (offset) калибруется на ПК полной библиотекой:

```sh
./build/baltic calibrate --grid data/egm2008/us_nga_egm08_25.tif \
    --points points.csv
```

и передаётся в прошивку как `offset_mm`.

## Ограничения

- Регион должен целиком помещаться в одну сетку; 180-й меридиан не
  поддерживается.
- Размах `N` в регионе — не более 65.5 м (ограничение `int16`).
- Нет автоматического переключения между сетками и нет файлового I/O.
- Для IAR/Keil атрибут секции может отличаться; при необходимости
  определите `BALTIC_GRID_SECTION` под свой компилятор.
- M7 без FPU: уберите `-mfpu=fpv5-d16 -mfloat-abi=hard`; код всё равно
  целочисленный.

Порт проверен кросс-компиляцией для `cortex-m7` и анализом секций; на
реальной плате результат зависит от тактирования Flash, кэша, MPU и
linker script. Измеряйте DWT на своём стенде.
