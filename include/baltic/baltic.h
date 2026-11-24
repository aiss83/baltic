/*
 * baltic - conversion between ellipsoidal (geodetic) heights and the
 * Baltic height system using the EGM2008 geoid model and PROJ.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file baltic.h
 * @brief Public C API of the baltic library.
 *
 * The library converts between:
 *   - ellipsoidal heights h referenced to the WGS 84 ellipsoid, and
 *   - heights H in the Baltic height system (BSV-77, Kronstadt zero).
 *
 * The conversion is based on the EGM2008 geoid model:
 *
 *   H = h - N(lat, lon) + C(lat, lon) + offset
 *
 * where N is the EGM2008 geoid undulation, C is an optional regional
 * correction grid (for example a quasi-geoid model), and offset is a
 * constant datum bias between EGM2008 and the Baltic system.  The offset
 * is normally derived from GNSS/levelling control points; see
 * baltic_calibrate_offset().
 *
 * All heights are in metres, all horizontal coordinates are in degrees
 * (geographic latitude/longitude on WGS 84, longitude positive east).
 *
 * A context is required for every conversion.  Contexts are safe to use
 * from several threads simultaneously; a context must not be destroyed
 * while another thread is using it.
 */

#ifndef BALTIC_BALTIC_H
#define BALTIC_BALTIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BALTIC_VERSION_MAJOR 0
#define BALTIC_VERSION_MINOR 1
#define BALTIC_VERSION_PATCH 0
#define BALTIC_VERSION_STRING "0.1.0"

#if defined(_WIN32) && defined(BALTIC_BUILD_SHARED)
#if defined(BALTIC_EXPORTS)
#define BALTIC_API __declspec(dllexport)
#else
#define BALTIC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define BALTIC_API __attribute__((visibility("default")))
#else
#define BALTIC_API
#endif

/** Opaque conversion context. */
typedef struct baltic_ctx baltic_ctx;

/** Status codes returned by the library. */
typedef enum baltic_status {
    BALTIC_OK = 0,             /**< Success. */
    BALTIC_ERR_INVALID_ARG = 1,/**< Invalid argument (coordinates, pointer, ...). */
    BALTIC_ERR_NO_MEMORY = 2,  /**< Memory allocation failure. */
    BALTIC_ERR_GRID_NOT_FOUND = 3, /**< Geoid/correction grid file is missing. */
    BALTIC_ERR_GRID_INVALID = 4,   /**< Grid exists but cannot be read/parsed. */
    BALTIC_ERR_OUT_OF_GRID = 5,    /**< Point is outside the grid coverage. */
    BALTIC_ERR_PROJ = 6,           /**< Unclassified PROJ error. */
    BALTIC_ERR_INTERNAL = 7        /**< Internal consistency error. */
} baltic_status;

/** Direction of the height conversion. */
typedef enum baltic_direction {
    /** h (ellipsoidal, WGS 84) -> H (Baltic system). */
    BALTIC_ELLIPSOIDAL_TO_BALTIC = 1,
    /** H (Baltic system) -> h (ellipsoidal, WGS 84). */
    BALTIC_BALTIC_TO_ELLIPSOIDAL = 2
} baltic_direction;

/**
 * @brief Context configuration.
 *
 * Zero-initialise the structure (or use BALTIC_CONFIG_INIT) and fill in
 * at least geoid_grid.  All string pointers are copied by
 * baltic_ctx_create(); they do not need to outlive the call.
 */
typedef struct baltic_config {
    /**
     * Path to the geoid grid used for the primary vertical shift.
     * For EGM2008 this is normally us_nga_egm08_25.tif (2.5 arc-minute
     * global grid) or a 1 arc-minute variant.  GeoTIFF and GTX grids
     * supported by PROJ are accepted.  Required.
     */
    const char *geoid_grid;

    /**
     * Optional additive correction grid, in metres.  Its value is added
     * to the resulting Baltic height.  This can be a regional
     * quasi-geoid/geoid difference model (for example the difference
     * between a national model and EGM2008).  May be NULL.
     */
    const char *correction_grid;

    /**
     * Optional extra directory.  Relative grid paths that do not exist
     * in the current working directory are also looked up here, and the
     * directory is added to PROJ's search path.  May be NULL.
     */
    const char *grid_search_path;

    /**
     * Constant datum offset in metres added to the Baltic height.  This
     * is the bias between the EGM2008-derived height and the Baltic
     * system, i.e. offset = H_baltic - (h - N + C).  Default 0.
     */
    double datum_offset_m;
} baltic_config;

/** Initialiser for baltic_config. */
#define BALTIC_CONFIG_INIT { NULL, NULL, NULL, 0.0 }

/**
 * @brief Diagnostic values produced by one conversion.
 *
 * All values are in metres.  The fields allow the caller to inspect the
 * model contributions and to reproduce the result:
 *
 *   H = h - geoid_undulation_m + correction_m + datum_offset_m
 */
typedef struct baltic_conversion_info {
    double geoid_undulation_m; /**< EGM2008 geoid undulation N at the point. */
    double correction_m;       /**< Correction grid value C at the point. */
    double datum_offset_m;     /**< Datum offset used for the conversion. */
} baltic_conversion_info;

/** One point for batch conversion. */
typedef struct baltic_batch_point {
    double lat_deg;              /**< Input: latitude, degrees. */
    double lon_deg;              /**< Input: longitude, degrees, east positive. */
    double height_in_m;          /**< Input: h or H depending on direction. */
    double height_out_m;         /**< Output: H or h. */
    double geoid_undulation_m;   /**< Output: EGM2008 undulation N. */
    double correction_m;         /**< Output: correction grid value C. */
    baltic_status status;        /**< Output: per-point status. */
} baltic_batch_point;

/** GNSS/levelling control point used for datum offset calibration. */
typedef struct baltic_control_point {
    double lat_deg;              /**< Latitude, degrees. */
    double lon_deg;              /**< Longitude, degrees, east positive. */
    double height_ellipsoidal_m; /**< GNSS ellipsoidal height h. */
    double height_baltic_m;      /**< Known Baltic height H. */
} baltic_control_point;

/** Result of datum offset calibration. */
typedef struct baltic_calibration {
    double offset_m;        /**< Estimated offset to pass to baltic_ctx_set_offset(). */
    double rms_m;           /**< RMS of residuals after removing the offset. */
    double min_residual_m;  /**< Minimum residual after removing the offset. */
    double max_residual_m;  /**< Maximum residual after removing the offset. */
    size_t used_points;     /**< Number of points used. */
    size_t skipped_points;  /**< Points outside the grid or invalid. */
} baltic_calibration;

/** Metadata of the primary geoid grid. */
typedef struct baltic_grid_info {
    char path[512];      /**< Absolute path of the grid. */
    char format[16];     /**< PROJ grid format, e.g. "gtiff" or "gtx". */
    double west_deg;     /**< Western bound, degrees. */
    double south_deg;    /**< Southern bound, degrees. */
    double east_deg;     /**< Eastern bound, degrees. */
    double north_deg;    /**< Northern bound, degrees. */
    int width;           /**< Number of columns. */
    int height;          /**< Number of rows. */
    double dlon_deg;     /**< Longitude cell size, degrees. */
    double dlat_deg;     /**< Latitude cell size, degrees. */
} baltic_grid_info;

/**
 * @brief Create a conversion context.
 *
 * Opens and validates the geoid grid (and the optional correction grid),
 * creates a private PROJ context and disables PROJ network access.
 *
 * @param config   Configuration; must not be NULL and geoid_grid must be set.
 * @param out_ctx  Receives the new context; set to NULL on failure.
 * @return BALTIC_OK or an error code.
 */
BALTIC_API baltic_status baltic_ctx_create(const baltic_config *config,
                                           baltic_ctx **out_ctx);

/**
 * @brief Destroy a context and release all resources.
 *
 * Passing NULL is allowed.  The caller must ensure that no other thread
 * is using the context.
 */
BALTIC_API void baltic_ctx_destroy(baltic_ctx *ctx);

/**
 * @brief Change the constant datum offset of an existing context.
 *
 * The change is atomic with respect to concurrent conversions.
 */
BALTIC_API baltic_status baltic_ctx_set_offset(baltic_ctx *ctx,
                                               double offset_m);

/** @brief Return the current datum offset in metres. */
BALTIC_API double baltic_ctx_get_offset(const baltic_ctx *ctx);

/**
 * @brief Convert a single height.
 *
 * @param ctx          Context created by baltic_ctx_create().
 * @param direction    Conversion direction.
 * @param lat_deg      WGS 84 latitude, degrees, [-90, 90].
 * @param lon_deg      Longitude, degrees, east positive (any value; normalised).
 * @param height_in_m  Input height in metres.
 * @param height_out_m Output height in metres; must not be NULL.
 * @param info         Optional model diagnostics; may be NULL.
 * @return BALTIC_OK, BALTIC_ERR_OUT_OF_GRID if the point is outside the
 *         grid, or another error code.
 */
BALTIC_API baltic_status baltic_convert(const baltic_ctx *ctx,
                                        baltic_direction direction,
                                        double lat_deg, double lon_deg,
                                        double height_in_m,
                                        double *height_out_m,
                                        baltic_conversion_info *info);

/** @brief Convenience wrapper: h (ellipsoidal) -> H (Baltic). */
BALTIC_API baltic_status
baltic_ellipsoidal_to_baltic(const baltic_ctx *ctx, double lat_deg,
                             double lon_deg, double h_m, double *H_m,
                             baltic_conversion_info *info);

/** @brief Convenience wrapper: H (Baltic) -> h (ellipsoidal). */
BALTIC_API baltic_status
baltic_baltic_to_ellipsoidal(const baltic_ctx *ctx, double lat_deg,
                             double lon_deg, double H_m, double *h_m,
                             baltic_conversion_info *info);

/**
 * @brief Convert an array of points in one call.
 *
 * The context lock is taken once for the whole batch.  Every point gets
 * its own status; points outside the grid do not abort the batch.
 *
 * @return Number of successfully converted points.
 */
BALTIC_API size_t baltic_convert_batch(const baltic_ctx *ctx,
                                       baltic_direction direction,
                                       baltic_batch_point *points,
                                       size_t count);

/**
 * @brief Estimate the constant datum offset from GNSS/levelling points.
 *
 * For every usable point the model height H_model = h - N + C is
 * computed (without the context offset), the residual
 * r = H_known - H_model is formed, and the returned offset is mean(r).
 * The RMS/min/max fields describe (r - offset), i.e. the scatter around
 * the fitted offset.  The context is not modified.
 *
 * Points outside the grid or with invalid coordinates are skipped and
 * counted in skipped_points.
 *
 * @return BALTIC_OK, BALTIC_ERR_INVALID_ARG if count == 0, or
 *         BALTIC_ERR_OUT_OF_GRID if no point could be used.
 */
BALTIC_API baltic_status baltic_calibrate_offset(
    const baltic_ctx *ctx, const baltic_control_point *points, size_t count,
    baltic_calibration *out);

/**
 * @brief Get metadata of the primary geoid grid.
 */
BALTIC_API baltic_status baltic_ctx_grid_info(const baltic_ctx *ctx,
                                              baltic_grid_info *out);

/** @brief Human-readable status description (static string). */
BALTIC_API const char *baltic_status_string(baltic_status status);

/** @brief Library version, e.g. "0.1.0". */
BALTIC_API const char *baltic_version(void);

/** @brief Version of the PROJ library used at runtime. */
BALTIC_API const char *baltic_proj_version(void);

/**
 * @brief Description of the last fatal error on this thread.
 *
 * Returns an empty string if the last call succeeded.  The pointer is
 * valid until the next library call on the same thread.
 */
BALTIC_API const char *baltic_last_error(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BALTIC_BALTIC_H */
