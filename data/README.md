# Данные EGM2008

В репозитории нет файлов геоида: глобальная сетка EGM2008 2.5′ занимает
около 77 МиБ. Она скачивается скриптом и не коммитится (см. `.gitignore`).

## Рекомендуемая сетка

| Параметр | Значение |
|---|---|
| Файл | `us_nga_egm08_25.tif` |
| Формат | GeoTIFF (PROJ grid), deflate |
| Размер | 80 671 172 байт |
| SHA-256 | `891553e91f7a64502d2a6300abb78026c17cb32657c2b37da0780238f2258ed2` |
| Разрешение | 2.5′ (0.041666667°) |
| Размерность | 8641 × 4321 |
| Покрытие | 90°S … 90°N, 180°W … 180°E |
| Единицы | метры (высота геоида) |
| Горизонтальная система | WGS 84 |

Скачивание:

```sh
./scripts/fetch-egm2008.sh
```

Скрипт проверяет SHA-256 и кладёт файл в
`data/egm2008/us_nga_egm08_25.tif`. Можно указать другой путь/URL:

```sh
BALTIC_EGM2008_URL=https://cdn.proj.org/us_nga_egm08_25.tif \
    ./scripts/fetch-egm2008.sh /path/to/us_nga_egm08_25.tif
```

## Источники

- Зеркало Agisoft (используется скриптом):
  <https://download.agisoft.com/gtg/us_nga_egm08_25.tif>
- Официальный CDN PROJ: <https://cdn.proj.org/us_nga_egm08_25.tif>
  (в некоторых сетях недоступен; файл входит в пакет `proj-data`)
- Каталог EGM2008 NGA: <https://earth-info.nga.mil/>
- Описание модели EGM2008: <https://earth-info.nga.mil/GandG/wgs84/gravitymod/egm2008/>

Сетка 1′ (`us_nga_egm2008_1.tif`, около 408 МиБ) даёт более высокое
разрешение; её можно использовать тем же API. Для 1′ файла из зеркала
Agisoft:

```sh
BALTIC_EGM2008_URL=https://download.agisoft.com/gtg/us_nga_egm2008_1.tif \
    ./scripts/fetch-egm2008.sh data/egm2008/us_nga_egm2008_1.tif
```

(У 1′ файла другая контрольная сумма, поэтому скрипт попросит обновить
ожидаемое значение или пропустить проверку через `BALTIC_SKIP_SHA256=1`.)

## Проверка файла

```sh
shasum -a 256 data/egm2008/us_nga_egm08_25.tif
./build/baltic info --grid data/egm2008/us_nga_egm08_25.tif
```

Ожидаемые `width=8641`, `height=4321`, `dlon_deg=0.041666667`.

## Условия использования

EGM2008 разработана Национальным геопространственным
разведывательным управлением США (NGA). Файлы сеток распространяются
NGA и зеркалами (OSGeo/PROJ, Agisoft и др.); условия использования
могут отличаться от лицензии самого кода библиотеки. Перед включением
сетки в продукт или публикацию ознакомьтесь с условиями NGA и
выбранного зеркала и сохраните ссылку на источник.

Ссылка для цитирования модели:

> Pavlis, N.K., S.A. Holmes, S.C. Kenyon, and J.K. Factor, 2012:
> The development and evaluation of the Earth Gravitational Model 2008
> (EGM2008), J. Geophys. Res., 117, B04406, doi:10.1029/2011JB008916.
