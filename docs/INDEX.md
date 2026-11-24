# Документация baltic

`baltic` — библиотека на C для перевода высот между эллипсоидальной
высотой WGS 84 и Балтийской системой высот (БСВ-77) по модели EGM2008.
Проект содержит два продукта:

- **полную библиотеку** `libbaltic` (PROJ + GeoTIFF/GTX, файловые сетки,
  CLI, калибровка поправки датума);
- **встраиваемую библиотеку** `baltic_embedded` (C99, целочисленная,
  без heap/OS/PROJ, региональная сетка в Flash, порт STM32H743/Cortex-M7).

## С чего начать

1. [README](https://github.com/aiss83/baltic/blob/main/README.md) — краткий обзор, сборка, примеры.
2. [ARCHITECTURE](ARCHITECTURE.md) — как устроены обе библиотеки.
3. [BUILD](BUILD.md) — сборка, установка, системный и встроенный PROJ.
4. [API](API.md) — публичный API и единицы измерения.
5. [CLI](CLI.md) — `baltic convert/info/calibrate`.
6. [CALIBRATION](CALIBRATION.md) — привязка к БСВ-77, поправка `Δ`.
7. [TESTING](TESTING.md) — тесты, эталоны, CI.
8. [Embedded](https://github.com/aiss83/baltic/blob/main/embedded/README.md) — встраиваемая версия.
9. [STM32H743 / Cortex-M7](https://github.com/aiss83/baltic/blob/main/embedded/ports/stm32h743/README.md) —
   linker-фрагменты, кэш, QSPI, размеры.
10. [FAQ](FAQ.md) — частые вопросы и ограничения.

## Ключевая формула

```
H = h − N(lat, lon) + C(lat, lon) + Δ
```

- `h` — эллипсоидальная высота WGS 84;
- `N` — высота геоида EGM2008;
- `C` — необязательная региональная поправка;
- `Δ` — постоянная поправка датума, калибруемая по GNSS-нивелированию.

> **Важно:** при `Δ = 0` результат — высота относительно геоида EGM2008,
> а не высота БСВ-77. Для БСВ-77 нужна калибровка `Δ` (и, при
> необходимости, региональная поправка `C`). Подробности —
> в [CALIBRATION](CALIBRATION.md).

## Быстрые ссылки

- Исходный код: <https://github.com/aiss83/baltic>
- Документация (GitHub Pages): <https://aiss83.github.io/baltic/>
- API-справочник Doxygen: <https://aiss83.github.io/baltic/>
- Данные EGM2008 и лицензии: [data/README.md](https://github.com/aiss83/baltic/blob/main/data/README.md)
- Встроенный PROJ: [VENDORING](VENDORING.md)

## Статус

Версия 0.1.0. Полная библиотека проверена на macOS/arm64 (Apple clang),
встраиваемая — кросс-компиляцией под Cortex-M4/M0+/M7; на реальной
плате STM32H743 не запускалась. Подробности и результаты — в
[TESTING](TESTING.md).
