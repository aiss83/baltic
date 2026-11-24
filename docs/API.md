# API

## 1. Единицы и порядок аргументов

| Величина | Тип | Единица |
|---|---|---|
| широта, долгота в API | `double` | градусы, долгота восточная |
| высота `h`, `H`, `Δ` | `double` | метры |
| координаты embedded | `int32_t` | микроградусы (1e-6°) |
| высоты embedded | `int32_t` | миллиметры |

В C-API порядок аргументов — **`lat, lon`**. В CLI и CSV — **`lon, lat`**.
Это сознательное различие: API следует геодезической традиции, CSV —
традиции GIS.

## 2. Жизненный цикл контекста

```c
#include <baltic/baltic.h>

baltic_config cfg = BALTIC_CONFIG_INIT;
cfg.geoid_grid = "data/egm2008/us_nga_egm08_25.tif";
cfg.correction_grid = NULL;       /* опционально */
cfg.grid_search_path = NULL;     /* опционально */
cfg.datum_offset_m = 0.0;

baltic_ctx *ctx = NULL;
baltic_status st = baltic_ctx_create(&cfg, &ctx);
if (st != BALTIC_OK) {
    fprintf(stderr, "%s\n", baltic_last_error());
    return 1;
}
...
baltic_ctx_destroy(ctx);
```

`baltic_ctx` — непрозрачный тип. Строки конфигурации копируются.

## 3. Основные функции

### `baltic_convert`

```c
baltic_status baltic_convert(const baltic_ctx *ctx,
                             baltic_direction direction,
                             double lat_deg, double lon_deg,
                             double height_in_m,
                             double *height_out_m,
                             baltic_conversion_info *info);
```

- `BALTIC_ELLIPSOIDAL_TO_BALTIC`: `h → H`;
- `BALTIC_BALTIC_TO_ELLIPSOIDAL`: `H → h`;
- `info` может быть `NULL`;
- при ошибке `height_out_m` не изменяется.

```c
double H;
baltic_conversion_info info;
st = baltic_ellipsoidal_to_baltic(ctx, 55.7558, 37.6173, 200.0, &H, &info);
if (st == BALTIC_OK) {
    printf("H=%.6f N=%.6f C=%.6f d=%.6f\n",
           H, info.geoid_undulation_m, info.correction_m,
           info.datum_offset_m);
}
```

### Пакетная обработка

```c
baltic_batch_point points[1000];
/* заполнить lat_deg, lon_deg, height_in_m */
size_t ok = baltic_convert_batch(ctx, BALTIC_ELLIPSOIDAL_TO_BALTIC,
                                 points, 1000);
for (size_t i = 0; i < 1000; ++i) {
    if (points[i].status == BALTIC_OK)
        use(points[i].height_out_m, points[i].geoid_undulation_m);
}
```

Возвращается число успешных точек; каждая точка имеет свой `status`.
Точки вне сетки не прерывают пакет.

### Калибровка `Δ`

```c
baltic_control_point pts[] = {
    {55.7558, 37.6173, 200.000, 185.808741},
    {59.9500, 30.3167, 150.000, 134.399350},
};

baltic_calibration cal;
st = baltic_calibrate_offset(ctx, pts, 2, &cal);
if (st == BALTIC_OK) {
    printf("offset=%.6f m rms=%.6f used=%zu skipped=%zu\n",
           cal.offset_m, cal.rms_m, cal.used_points, cal.skipped_points);
    baltic_ctx_set_offset(ctx, cal.offset_m);
}
```

Калибровка **не изменяет** контекст. Она считает модель без текущего
`Δ`; полученное значение нужно применить самостоятельно. Больше —
в [CALIBRATION](CALIBRATION.md).

### Сетка

```c
baltic_grid_info gi;
baltic_ctx_grid_info(ctx, &gi);
printf("%s: %d x %d, %.9f..%.9f\n", gi.path, gi.width, gi.height,
       gi.west_deg, gi.east_deg);
```

### Смещение

```c
baltic_ctx_set_offset(ctx, 0.35);
double current = baltic_ctx_get_offset(ctx);
```

Изменение атомарно относительно преобразований.

## 4. Ошибки

| Код | Значение |
|---|---|
| `BALTIC_OK` | успех |
| `BALTIC_ERR_INVALID_ARG` | координата/указатель/направление |
| `BALTIC_ERR_NO_MEMORY` | нехватка памяти |
| `BALTIC_ERR_GRID_NOT_FOUND` | файл сетки отсутствует |
| `BALTIC_ERR_GRID_INVALID` | файл не читается как сетка |
| `BALTIC_ERR_OUT_OF_GRID` | точка вне покрытия |
| `BALTIC_ERR_PROJ` | прочая ошибка PROJ |
| `BALTIC_ERR_INTERNAL` | внутренняя ошибка |

`baltic_status_string()` возвращает короткую строку. Для диагностики:

```c
if (st != BALTIC_OK)
    fprintf(stderr, "%s: %s\n", baltic_status_string(st),
            baltic_last_error());
```

`baltic_last_error()` — thread-local буфер; действителен до следующего
вызова библиотеки в этом потоке.

## 5. Потокобезопасность

- Один контекст можно использовать из нескольких потоков: все обращения
  к PROJ сериализуются мьютексом.
- `baltic_convert_batch` удерживает мьютекс на весь пакет — не смешивайте
  очень большие пакеты с требованиями к малой задержке в других потоках.
- `baltic_ctx_destroy` нельзя вызывать параллельно с использованием
  контекста.
- `baltic_ctx_get_offset`/`set_offset` безопасны.

## 6. Встроенный API

Заголовок: `embedded/include/baltic_embedded.h`.

```c
baltic_embedded_status
baltic_embedded_h_to_H(const baltic_embedded_grid *geoid,
                       const baltic_embedded_grid *correction,
                       int32_t lat_u6, int32_t lon_u6,
                       int32_t h_mm, int32_t offset_mm,
                       int32_t *H_mm);
```

- `geoid` обязателен;
- `correction` может быть `NULL`;
- `lat_u6`, `lon_u6` — микроградусы;
- `h_mm`, `offset_mm`, `H_mm` — миллиметры;
- статусы: `BALTIC_EMBEDDED_OK`, `ERR_ARGUMENT`, `ERR_OUTSIDE`,
  `ERR_GRID`.

Дополнительно:

- `baltic_embedded_geoid_mm()` — только `N`;
- `baltic_embedded_H_to_h()` — обратное преобразование;
- `baltic_embedded_grid_contains()` — проверка покрытия.

Встроенные функции не выделяют память, не используют файлы и не имеют
глобального состояния. Подробности — в
[embedded/README.md](https://github.com/aiss83/baltic/blob/main/embedded/README.md).

## 7. Версии и ABI

- `baltic_version()` — версия библиотеки;
- `baltic_proj_version()` — версия PROJ;
- `BALTIC_VERSION_MAJOR/MINOR/PATCH` — макросы.

Публичный заголовок не включает `proj.h`; зависимость от PROJ скрыта.
При обновлении мажорной версии возможны ABI-изменения структур
конфигурации.

## 8. Полный справочник

Doxygen-справочник собирается из заголовков:

```sh
doxygen Doxyfile
```

Онлайн: <https://aiss83.github.io/baltic/>.
