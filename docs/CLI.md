# CLI `baltic`

## 1. Команды

```text
baltic convert   --grid FILE [опции]
baltic info      --grid FILE
baltic calibrate --grid FILE --points FILE
baltic version
```

## 2. convert

Входной CSV: `lon,lat,height` (долгота, широта, высота). Заголовок и
строки, начинающиеся с `#`, игнорируются. Десятичный разделитель — точка.

```sh
echo "37.6173,55.7558,200" | ./build/baltic convert \
    --grid data/egm2008/us_nga_egm08_25.tif --offset 0.35
```

```text
lon,lat,ellipsoidal_h,baltic_h,egm2008_n,correction,offset,status
37.617300000,55.755800000,200.000000,185.808741,14.541259,0.000000,0.350000,ok
```

Обратное преобразование:

```sh
./build/baltic convert --grid ... --reverse \
    --input H.csv --output h.csv
```

Опции:

| Опция | Значение |
|---|---|
| `--grid FILE` | сетка EGM2008 (GeoTIFF/GTX), обязательна |
| `--correction FILE` | поправочная сетка `C`, добавляется к `H` |
| `--search-path DIR` | дополнительный каталог поиска относительных имён |
| `--offset M` | постоянная поправка `Δ`, метры |
| `--reverse` | `H → h` вместо `h → H` |
| `--input FILE` | входной CSV; `-` = stdin |
| `--output FILE` | выходной файл; по умолчанию stdout |
| `--format csv\|jsonl` | формат вывода |
| `--precision N` | знаков после запятой для высот (0…15, по умолчанию 6) |
| `--no-header` | не печатать заголовок CSV |
| `--quiet` | не печатать итоговую сводку в stderr |

JSONL:

```json
{"lon":37.617300000,"lat":55.755800000,"in":200.000000,"out":185.808741,"n":14.541259,"correction":0.000000,"status":"ok"}
```

Координаты всегда печатаются с 9 знаками, высоты — с `--precision`.

## 3. info

```sh
./build/baltic info --grid data/egm2008/us_nga_egm08_25.tif
```

```text
path=/.../us_nga_egm08_25.tif
format=gtiff
west_deg=-180.000000000
south_deg=-90.000000000
east_deg=180.000000000
north_deg=90.000000000
width=8641
height=4321
dlon_deg=0.041666667
dlat_deg=0.041666667
```

Формат `key=value` удобен для скриптов.

## 4. calibrate

Вход: `lon,lat,h,H`, где `h` — GNSS-высота, `H` — известная высота
БСВ-77.

```sh
./build/baltic calibrate --grid data/egm2008/us_nga_egm08_25.tif \
    --points points.csv
```

```text
points_used=12
points_skipped=0
offset_m=0.350000
rms_m=0.021000
min_residual_m=-0.031000
max_residual_m=0.028000
```

Полученное `offset_m` передайте в `--offset` при `convert` или в
`cfg.datum_offset_m` в API. Калибровка не изменяет файлы.

## 5. Коды возврата

| Код | Значение |
|---|---|
| `0` | все точки обработаны |
| `1` | были точки с ошибкой (например, вне сетки) |
| `2` | ошибка аргументов/файла/контекста |

Строки с ошибками всё равно выводятся со статусом; итоговая сводка —
в stderr, если не указан `--quiet`.

## 6. Пример скрипта

```bash
set -euo pipefail

GRID=data/egm2008/us_nga_egm08_25.tif

# 1. Откалибровать Δ по контрольным пунктам
OFFSET=$(./build/baltic calibrate --grid "$GRID" --points control.csv |
         awk -F= '$1=="offset_m" {print $2}')

# 2. Пересчитать точки
./build/baltic convert --grid "$GRID" --offset "$OFFSET" \
    --input points.csv --output result.csv

# 3. Только успешные строки
awk -F, 'NR==1 || $NF=="ok"' result.csv > result_ok.csv
```

## 7. Замечания

- `--reverse` меняет смысл третьей колонки входа и выхода, но не формат.
- `--correction` применяется и к прямым, и к обратным преобразованиям.
- `--search-path` помогает, если сетка задана относительным именем.
- Имена файлов сеток с пробелами не поддерживаются; каталоги с
  пробелами — поддерживаются.
- Для больших файлов используйте `--format csv` и перенаправляйте вывод
  в файл: обработка потоковая, пакетами по 4096 точек.
