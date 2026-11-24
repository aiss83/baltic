# Обновление встроенного PROJ

В `third_party/proj/` лежит немодифицированный исходный код PROJ. Он
собирается вместе с библиотекой, поэтому отдельная установка PROJ не
требуется.

Текущая версия:

| Параметр | Значение |
|---|---|
| Версия | 9.8.1 |
| Источник | <https://download.osgeo.org/proj/proj-9.8.1.tar.gz> |
| SHA-256 архива | `af5b731c145c1d13c4e3b4eeb7d167e94e845e440f71e3496b4ed8dae0291960` |
| Лицензия | MIT/X11, см. `third_party/proj/COPYING` |

## Порядок обновления

```sh
VERSION=9.8.1
curl -fLO "https://download.osgeo.org/proj/proj-${VERSION}.tar.gz"
tar xzf "proj-${VERSION}.tar.gz"
rm -rf third_party/proj
mv "proj-${VERSION}" third_party/proj
```

После обновления:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Проверьте, что:

- `third_party/proj/COPYING` на месте;
- версия в `baltic_proj_version()` соответствует ожидаемой;
- тест `egm2008` проходит (значения не должны измениться, если сетка та же);
- при необходимости обновите `docs/GEODESY.md` и `NOTICE`.

## Зачем нужен весь PROJ

`libbaltic` использует:

- операцию `vgridshift` и её соглашение о знаке (`+multiplier=1`);
- интерполяцию вертикальных сеток (`grids.cpp`);
- чтение GeoTIFF-сеток через libtiff и GTX;
- контексты, поиск файлов и обработку ошибок PROJ.

Можно собрать против системного PROJ:

```sh
cmake -S . -B build -DBALTIC_BUNDLED_PROJ=OFF
```

Требуется PROJ ≥ 9 с GeoTIFF-поддержкой.
