/*
 * baltic_embedded - integer-only height conversion for small MCUs.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file baltic_embedded.h
 * @brief Flash-friendly EGM2008 -> Baltic height conversion.
 *
 * This is a standalone, dependency-free variant of libbaltic for
 * microcontrollers and other systems with tight Flash/RAM budgets.
 *
 * Design constraints:
 *   - C99, integer arithmetic only (no FPU, no libm);
 *   - no heap, no file I/O, no OS, no PROJ, no global mutable state;
 *   - the geoid grid is a const array placed in Flash (typically .rodata);
 *   - one regional grid, cropped and resampled offline from EGM2008.
 *
 * Heights and coordinates:
 *   - heights are int32_t millimetres;
 *   - latitudes/longitudes are int32_t microdegrees (1e-6 degree).
 *
 * The conversion is
 *
 *   H = h - N(lat, lon) + C(lat, lon) + offset
 *
 * where N is interpolated from the embedded geoid grid, C is an optional
 * embedded correction grid, and offset is a caller-supplied constant in
 * millimetres (normally calibrated on a host).
 *
 * Grid encoding: int16_t samples in millimetres relative to per-grid
 * base_mm, row-major from south to north, west to east.  A regional grid
 * must have a peak-to-peak N variation of at most 65.5 m; this is checked
 * by the generator.  Typical 2.5' grids for a 2 x 2 degree area use
 * about 4.8 KiB of Flash.
 */

#ifndef BALTIC_EMBEDDED_H
#define BALTIC_EMBEDDED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BALTIC_EMBEDDED_VERSION_MAJOR 0
#define BALTIC_EMBEDDED_VERSION_MINOR 1
#define BALTIC_EMBEDDED_VERSION_PATCH 0
#define BALTIC_EMBEDDED_VERSION_STRING "0.1.0"

/** Microdegrees per degree. */
#define BALTIC_EMBEDDED_U6_PER_DEGREE 1000000

/** Millimetres per metre. */
#define BALTIC_EMBEDDED_MM_PER_METRE 1000

/** Status codes.  Negative values indicate failure. */
typedef enum baltic_embedded_status {
    BALTIC_EMBEDDED_OK = 0,
    BALTIC_EMBEDDED_ERR_ARGUMENT = -1,
    BALTIC_EMBEDDED_ERR_OUTSIDE = -2,
    BALTIC_EMBEDDED_ERR_GRID = -3
} baltic_embedded_status;

/**
 * @brief Immutable geoid/correction grid descriptor.
 *
 * The descriptor itself is normally const and lives in Flash.  values
 * must stay valid for the lifetime of all calls.
 *
 * values[iy * width + ix] is the sample at
 *   lat = lat_min_u6 + iy * lat_step_u6
 *   lon = lon_min_u6 + ix * lon_step_u6
 * and is stored as millimetres relative to base_mm.
 *
 * The descriptor is 36 bytes on a 32-bit target.
 */
typedef struct baltic_embedded_grid {
    const int16_t *values; /**< Samples, mm relative to base_mm. */
    uint16_t width;        /**< Number of columns (>= 2). */
    uint16_t height;       /**< Number of rows (>= 2). */
    int32_t base_mm;       /**< Added to every stored sample. */
    int32_t lon_min_u6;    /**< Western edge, microdegrees. */
    int32_t lat_min_u6;    /**< Southern edge, microdegrees. */
    int32_t lon_max_u6;    /**< Eastern edge, microdegrees. */
    int32_t lat_max_u6;    /**< Northern edge, microdegrees. */
    int32_t lon_step_u6;   /**< Longitude step, microdegrees (> 0). */
    int32_t lat_step_u6;   /**< Latitude step, microdegrees (> 0). */
} baltic_embedded_grid;

/**
 * @brief Check whether a point is inside the grid's nominal coverage.
 *
 * Longitude is normalised to [-180, 180] degrees.  The edges are
 * inclusive.  Returns 1 if covered, 0 otherwise (including invalid
 * arguments).
 */
int baltic_embedded_grid_contains(const baltic_embedded_grid *grid,
                                  int32_t lat_u6, int32_t lon_u6);

/**
 * @brief Interpolate the geoid undulation N.
 *
 * @param grid    Grid descriptor; must not be NULL.
 * @param lat_u6  Latitude, microdegrees, [-90e6, 90e6].
 * @param lon_u6  Longitude, microdegrees (normalised internally).
 * @param n_mm    Receives N in millimetres.  Not modified on error.
 * @return BALTIC_EMBEDDED_OK, ERR_ARGUMENT, ERR_GRID or ERR_OUTSIDE.
 */
baltic_embedded_status
baltic_embedded_geoid_mm(const baltic_embedded_grid *grid, int32_t lat_u6,
                         int32_t lon_u6, int32_t *n_mm);

/**
 * @brief Convert ellipsoidal height h to Baltic height H.
 *
 * @param geoid       Geoid grid (EGM2008); required.
 * @param correction  Optional additive correction grid; may be NULL.
 * @param lat_u6      Latitude, microdegrees.
 * @param lon_u6      Longitude, microdegrees.
 * @param h_mm        Ellipsoidal height, millimetres.
 * @param offset_mm   Datum offset Delta, millimetres.
 * @param H_mm        Receives Baltic height; not modified on error.
 * @return Status code.
 */
baltic_embedded_status
baltic_embedded_h_to_H(const baltic_embedded_grid *geoid,
                       const baltic_embedded_grid *correction, int32_t lat_u6,
                       int32_t lon_u6, int32_t h_mm, int32_t offset_mm,
                       int32_t *H_mm);

/**
 * @brief Convert Baltic height H to ellipsoidal height h.
 *
 * Inverse of baltic_embedded_h_to_H().
 */
baltic_embedded_status
baltic_embedded_H_to_h(const baltic_embedded_grid *geoid,
                       const baltic_embedded_grid *correction, int32_t lat_u6,
                       int32_t lon_u6, int32_t H_mm, int32_t offset_mm,
                       int32_t *h_mm);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BALTIC_EMBEDDED_H */
