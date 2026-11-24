/*
 * baltic - conversion between ellipsoidal heights and the Baltic height
 * system using EGM2008 and PROJ.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "baltic_internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define BALTIC_ACCESS _access
#define BALTIC_GETCWD _getcwd
#ifndef R_OK
#define R_OK 4
#endif
#else
#include <unistd.h>
#define BALTIC_ACCESS access
#define BALTIC_GETCWD getcwd
#endif

#define BALTIC_PI 3.14159265358979323846
#define BALTIC_RAD_TO_DEG (180.0 / BALTIC_PI)

#if defined(_MSC_VER)
#define BALTIC_TLS __declspec(thread)
#else
#define BALTIC_TLS _Thread_local
#endif

static BALTIC_TLS char g_last_error[512];

/* proj_grid_info() uses PROJ's default context, so serialise calls and
 * silence PROJ's stderr diagnostics while probing a grid header. */
#if defined(_WIN32)
static SRWLOCK g_grid_info_mutex = SRWLOCK_INIT;
#define GRID_INFO_LOCK() AcquireSRWLockExclusive(&g_grid_info_mutex)
#define GRID_INFO_UNLOCK() ReleaseSRWLockExclusive(&g_grid_info_mutex)
#else
static pthread_mutex_t g_grid_info_mutex = PTHREAD_MUTEX_INITIALIZER;
#define GRID_INFO_LOCK() (void)pthread_mutex_lock(&g_grid_info_mutex)
#define GRID_INFO_UNLOCK() (void)pthread_mutex_unlock(&g_grid_info_mutex)
#endif

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

static void baltic_set_error(const char *fmt, ...) {
    va_list ap;

    if (fmt == NULL) {
        g_last_error[0] = '\0';
        return;
    }
    va_start(ap, fmt);
    (void)vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

static void baltic_clear_error(void) { g_last_error[0] = '\0'; }

const char *baltic_last_error(void) { return g_last_error; }

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static char *baltic_strdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

static int path_is_absolute(const char *path) {
#if defined(_WIN32)
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
#else
    return path[0] == '/';
#endif
}

static char *path_join(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int need_sep = dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\';
    char *out = (char *)malloc(dlen + (size_t)need_sep + nlen + 1);

    if (out == NULL)
        return NULL;
    memcpy(out, dir, dlen);
    if (need_sep)
        out[dlen++] = '/';
    memcpy(out + dlen, name, nlen + 1);
    return out;
}

static char *path_absolute(const char *path) {
    char cwd[4096];
    char *out;

    if (path == NULL || path[0] == '\0')
        return NULL;
    if (path_is_absolute(path))
        return baltic_strdup(path);
    if (BALTIC_GETCWD(cwd, sizeof(cwd)) == NULL)
        return baltic_strdup(path);
    out = path_join(cwd, path);
    return out;
}

static char *path_dirname(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL || path[0] == '\0')
        return baltic_strdup(".");
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char *bslash = strrchr(path, '\\');
        if (bslash != NULL && (slash == NULL || bslash > slash))
            slash = bslash;
    }
#endif
    if (slash == NULL)
        return baltic_strdup(".");
    if (slash == path)
        return baltic_strdup("/");
    n = (size_t)(slash - path);
    out = (char *)malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *path_basename(const char *path) {
    const char *slash;
    const char *base;

    if (path == NULL || path[0] == '\0')
        return baltic_strdup("");
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char *bslash = strrchr(path, '\\');
        if (bslash != NULL && (slash == NULL || bslash > slash))
            slash = bslash;
    }
#endif
    base = slash != NULL ? slash + 1 : path;
    return baltic_strdup(base);
}

static char *resolve_grid_path(const char *path, const char *search_path) {
    char *absolute = path_absolute(path);

    if (absolute == NULL)
        return NULL;
    if (BALTIC_ACCESS(absolute, R_OK) == 0)
        return absolute;

    if (!path_is_absolute(path) && search_path != NULL &&
        search_path[0] != '\0') {
        char *candidate = path_join(search_path, path);
        char *candidate_absolute =
            candidate != NULL ? path_absolute(candidate) : NULL;

        free(candidate);
        if (candidate_absolute != NULL &&
            BALTIC_ACCESS(candidate_absolute, R_OK) == 0) {
            free(absolute);
            return candidate_absolute;
        }
        free(candidate_absolute);
    }
    return absolute;
}

static double normalize_longitude(double lon_deg) {
    double lon = fmod(lon_deg, 360.0);

    if (lon > 180.0)
        lon -= 360.0;
    else if (lon < -180.0)
        lon += 360.0;
    return lon;
}

static baltic_status map_proj_error(int proj_errno_value) {
    switch (proj_errno_value) {
    case PROJ_ERR_COORD_TRANSFM_OUTSIDE_GRID:
    case PROJ_ERR_COORD_TRANSFM_GRID_AT_NODATA:
        return BALTIC_ERR_OUT_OF_GRID;
    case PROJ_ERR_INVALID_OP_FILE_NOT_FOUND_OR_INVALID:
        return BALTIC_ERR_GRID_INVALID;
    case 0:
        return BALTIC_OK;
    default:
        return BALTIC_ERR_PROJ;
    }
}

/* ------------------------------------------------------------------------- */
/* Grid handling                                                             */
/* ------------------------------------------------------------------------- */

static baltic_status read_grid_info(const char *path, baltic_grid_info *out) {
    PJ_GRID_INFO gi;
    PJ_LOG_LEVEL previous_level;

    GRID_INFO_LOCK();
    previous_level = proj_log_level(NULL, PJ_LOG_NONE);
    gi = proj_grid_info(path);
    (void)proj_log_level(NULL, previous_level);
    GRID_INFO_UNLOCK();

    if (gi.n_lon <= 0 || gi.n_lat <= 0) {
        baltic_set_error("cannot read grid header: %s", path);
        return BALTIC_ERR_GRID_INVALID;
    }

    memset(out, 0, sizeof(*out));
    (void)snprintf(out->path, sizeof(out->path), "%s", path);
    (void)snprintf(out->format, sizeof(out->format), "%s",
                   gi.format[0] != '\0' ? gi.format : "unknown");
    out->west_deg = gi.lowerleft.lam * BALTIC_RAD_TO_DEG;
    out->south_deg = gi.lowerleft.phi * BALTIC_RAD_TO_DEG;
    out->east_deg = gi.upperright.lam * BALTIC_RAD_TO_DEG;
    out->north_deg = gi.upperright.phi * BALTIC_RAD_TO_DEG;
    out->width = gi.n_lon;
    out->height = gi.n_lat;
    out->dlon_deg = fabs(gi.cs_lon) * BALTIC_RAD_TO_DEG;
    out->dlat_deg = fabs(gi.cs_lat) * BALTIC_RAD_TO_DEG;

    if (out->width <= 0 || out->height <= 0)
        return BALTIC_ERR_GRID_INVALID;
    return BALTIC_OK;
}

/*
 * Build a pipeline that accepts and returns degrees, runs vgridshift in
 * radians and leaves the vertical shift to PROJ:
 *
 *   +proj=vgridshift forward: z += N
 *   +proj=vgridshift inverse: z -= N
 *
 * The pipeline therefore maps (lon, lat, H) -> (lon, lat, h) in the
 * forward direction for an EGM2008 grid, matching EPSG:3855 -> EPSG:4979.
 */
static PJ *create_vgrid_pipeline(PJ_CONTEXT *proj_ctx, const char *grid_path) {
    static const char prefix[] = "+proj=pipeline "
                                 "+step +proj=unitconvert +xy_in=deg +xy_out=rad "
                                 "+step +proj=vgridshift +grids=";
    static const char suffix[] =
        " +multiplier=1 "
        "+step +proj=unitconvert +xy_in=rad +xy_out=deg";
    char *base = path_basename(grid_path);
    char *definition;
    size_t len;
    PJ *op;

    if (base == NULL)
        return NULL;
    len = sizeof(prefix) - 1 + strlen(base) + sizeof(suffix);
    definition = (char *)malloc(len);
    if (definition == NULL) {
        free(base);
        return NULL;
    }
    (void)snprintf(definition, len, "%s%s%s", prefix, base, suffix);
    free(base);
    op = proj_create(proj_ctx, definition);
    free(definition);
    return op;
}

static void free_search_dirs(char **dirs, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i)
        free(dirs[i]);
}

static size_t add_search_dir(char **dirs, size_t count, size_t capacity,
                             const char *dir) {
    size_t i;

    if (dir == NULL || dir[0] == '\0' || count >= capacity)
        return count;
    for (i = 0; i < count; ++i) {
        if (strcmp(dirs[i], dir) == 0)
            return count;
    }
    dirs[count] = baltic_strdup(dir);
    return dirs[count] != NULL ? count + 1 : count;
}

static void configure_search_paths(PJ_CONTEXT *proj_ctx, const char *grid_path,
                                   const char *extra_path) {
    char *dirs[5];
    char *grid_dir = NULL;
    char *extra_abs = NULL;
    size_t count = 0;

    grid_dir = path_dirname(grid_path);
    count = add_search_dir(dirs, count, 5, grid_dir);

    if (extra_path != NULL && extra_path[0] != '\0') {
        extra_abs = path_absolute(extra_path);
        count = add_search_dir(dirs, count, 5, extra_abs);
    }
    if (count > 0) {
        proj_context_set_search_paths(proj_ctx, (int)count,
                                      (const char *const *)dirs);
    }

    free_search_dirs(dirs, count);
    free(grid_dir);
    free(extra_abs);
}

static baltic_status create_backend(const char *grid_path, const char *extra_path,
                                    const char *kind, PJ_CONTEXT **out_ctx,
                                    PJ **out_op) {
    PJ_CONTEXT *proj_ctx;
    PJ *op;
    int proj_err;

    *out_ctx = NULL;
    *out_op = NULL;

    proj_ctx = proj_context_create();
    if (proj_ctx == NULL) {
        baltic_set_error("cannot create PROJ context for %s", kind);
        return BALTIC_ERR_PROJ;
    }
    (void)proj_context_set_enable_network(proj_ctx, 0);
    (void)proj_log_level(proj_ctx, PJ_LOG_NONE);
    configure_search_paths(proj_ctx, grid_path, extra_path);

    op = create_vgrid_pipeline(proj_ctx, grid_path);
    if (op == NULL) {
        proj_err = proj_context_errno(proj_ctx);
        {
            const char *proj_message =
                proj_context_errno_string(proj_ctx, proj_err);
            baltic_set_error(
                "cannot create vertical grid operation for %s: %s", grid_path,
                proj_message != NULL ? proj_message : "unknown PROJ error");
        }
        proj_context_destroy(proj_ctx);
        if (proj_err == PROJ_ERR_INVALID_OP_FILE_NOT_FOUND_OR_INVALID)
            return BALTIC_ERR_GRID_INVALID;
        return BALTIC_ERR_PROJ;
    }

    *out_ctx = proj_ctx;
    *out_op = op;
    return BALTIC_OK;
}

/* ------------------------------------------------------------------------- */
/* Context lifecycle                                                         */
/* ------------------------------------------------------------------------- */

baltic_status baltic_ctx_create(const baltic_config *config,
                                baltic_ctx **out_ctx) {
    baltic_ctx *ctx;
    baltic_status status = BALTIC_ERR_INTERNAL;
    int mutex_ready = 0;

    baltic_clear_error();

    if (out_ctx == NULL) {
        baltic_set_error("out_ctx is NULL");
        return BALTIC_ERR_INVALID_ARG;
    }
    *out_ctx = NULL;

    if (config == NULL) {
        baltic_set_error("config is NULL");
        return BALTIC_ERR_INVALID_ARG;
    }
    if (config->geoid_grid == NULL || config->geoid_grid[0] == '\0') {
        baltic_set_error("config->geoid_grid is not set");
        return BALTIC_ERR_INVALID_ARG;
    }
    if (!isfinite(config->datum_offset_m)) {
        baltic_set_error("config->datum_offset_m is not finite");
        return BALTIC_ERR_INVALID_ARG;
    }

    ctx = (baltic_ctx *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        baltic_set_error("out of memory");
        return BALTIC_ERR_NO_MEMORY;
    }
    ctx->offset_m = config->datum_offset_m;
    if (baltic_mutex_init(&ctx->mutex) != 0) {
        baltic_set_error("cannot initialise mutex");
        free(ctx);
        return BALTIC_ERR_INTERNAL;
    }
    mutex_ready = 1;

    ctx->geoid_path = resolve_grid_path(config->geoid_grid,
                                        config->grid_search_path);
    if (ctx->geoid_path == NULL) {
        baltic_set_error("out of memory");
        status = BALTIC_ERR_NO_MEMORY;
        goto fail;
    }
    if (BALTIC_ACCESS(ctx->geoid_path, R_OK) != 0) {
        baltic_set_error("geoid grid not found: %s", ctx->geoid_path);
        status = BALTIC_ERR_GRID_NOT_FOUND;
        goto fail;
    }
    status = read_grid_info(ctx->geoid_path, &ctx->geoid_info);
    if (status != BALTIC_OK)
        goto fail;

    if (config->correction_grid != NULL && config->correction_grid[0] != '\0') {
        baltic_grid_info corr_info;

        ctx->correction_path = resolve_grid_path(config->correction_grid,
                                                 config->grid_search_path);
        if (ctx->correction_path == NULL) {
            baltic_set_error("out of memory");
            status = BALTIC_ERR_NO_MEMORY;
            goto fail;
        }
        if (BALTIC_ACCESS(ctx->correction_path, R_OK) != 0) {
            baltic_set_error("correction grid not found: %s",
                             ctx->correction_path);
            status = BALTIC_ERR_GRID_NOT_FOUND;
            goto fail;
        }
        status = read_grid_info(ctx->correction_path, &corr_info);
        if (status != BALTIC_OK)
            goto fail;
    }

    status = create_backend(ctx->geoid_path, config->grid_search_path,
                            "geoid grid", &ctx->geoid_ctx, &ctx->geoid_op);
    if (status != BALTIC_OK)
        goto fail;

    if (ctx->correction_path != NULL) {
        status = create_backend(ctx->correction_path, config->grid_search_path,
                                "correction grid", &ctx->correction_ctx,
                                &ctx->correction_op);
        if (status != BALTIC_OK)
            goto fail;
    }

    *out_ctx = ctx;
    return BALTIC_OK;

fail:
    if (ctx->geoid_op != NULL)
        proj_destroy(ctx->geoid_op);
    if (ctx->correction_op != NULL)
        proj_destroy(ctx->correction_op);
    if (ctx->geoid_ctx != NULL)
        proj_context_destroy(ctx->geoid_ctx);
    if (ctx->correction_ctx != NULL)
        proj_context_destroy(ctx->correction_ctx);
    if (mutex_ready)
        baltic_mutex_destroy(&ctx->mutex);
    free(ctx->geoid_path);
    free(ctx->correction_path);
    free(ctx);
    return status;
}

void baltic_ctx_destroy(baltic_ctx *ctx) {
    if (ctx == NULL)
        return;
    if (ctx->geoid_op != NULL)
        proj_destroy(ctx->geoid_op);
    if (ctx->correction_op != NULL)
        proj_destroy(ctx->correction_op);
    if (ctx->geoid_ctx != NULL)
        proj_context_destroy(ctx->geoid_ctx);
    if (ctx->correction_ctx != NULL)
        proj_context_destroy(ctx->correction_ctx);
    baltic_mutex_destroy(&ctx->mutex);
    free(ctx->geoid_path);
    free(ctx->correction_path);
    free(ctx);
}

baltic_status baltic_ctx_set_offset(baltic_ctx *ctx, double offset_m) {
    baltic_clear_error();
    if (ctx == NULL) {
        baltic_set_error("ctx is NULL");
        return BALTIC_ERR_INVALID_ARG;
    }
    if (!isfinite(offset_m)) {
        baltic_set_error("offset is not finite");
        return BALTIC_ERR_INVALID_ARG;
    }
    baltic_mutex_lock(&ctx->mutex);
    ctx->offset_m = offset_m;
    baltic_mutex_unlock(&ctx->mutex);
    return BALTIC_OK;
}

double baltic_ctx_get_offset(const baltic_ctx *ctx) {
    double value;

    if (ctx == NULL)
        return NAN;
    baltic_mutex_lock((baltic_mutex *)&ctx->mutex);
    value = ctx->offset_m;
    baltic_mutex_unlock((baltic_mutex *)&ctx->mutex);
    return value;
}

/* ------------------------------------------------------------------------- */
/* Conversion                                                                */
/* ------------------------------------------------------------------------- */

static baltic_status run_vgrid(PJ *op, PJ_DIRECTION direction, double lon_deg,
                               double lat_deg, double z_in, double *z_out) {
    PJ_COORD in;
    PJ_COORD out;
    int proj_err;

    proj_errno_reset(op);
    in = proj_coord(lon_deg, lat_deg, z_in, 0.0);
    out = proj_trans(op, direction, in);
    proj_err = proj_errno(op);
    if (proj_err != 0)
        return map_proj_error(proj_err);
    if (!isfinite(out.xyz.z))
        return BALTIC_ERR_OUT_OF_GRID;
    *z_out = out.xyz.z;
    return BALTIC_OK;
}

static baltic_status convert_locked(baltic_ctx *ctx, baltic_direction direction,
                                    double lat_deg, double lon_deg,
                                    double height_in_m, double offset_m,
                                    double *height_out_m,
                                    baltic_conversion_info *info) {
    double lon;
    double z;
    double undulation = 0.0;
    double correction = 0.0;
    baltic_status status;

    if (ctx == NULL || height_out_m == NULL)
        return BALTIC_ERR_INVALID_ARG;
    if (!isfinite(lat_deg) || !isfinite(lon_deg) || !isfinite(height_in_m) ||
        !isfinite(offset_m))
        return BALTIC_ERR_INVALID_ARG;
    if (lat_deg < -90.0 || lat_deg > 90.0)
        return BALTIC_ERR_INVALID_ARG;

    lon = normalize_longitude(lon_deg);
    z = height_in_m;

    if (direction == BALTIC_ELLIPSOIDAL_TO_BALTIC) {
        double z_geoid;

        /* EPSG:3855 -> EPSG:4979 is the forward direction of vgridshift. */
        status = run_vgrid(ctx->geoid_op, PJ_INV, lon, lat_deg, z, &z_geoid);
        if (status != BALTIC_OK)
            return status;
        undulation = z - z_geoid;
        z = z_geoid;

        if (ctx->correction_op != NULL) {
            double z_corrected;

            status = run_vgrid(ctx->correction_op, PJ_FWD, lon, lat_deg, z,
                               &z_corrected);
            if (status != BALTIC_OK)
                return status;
            correction = z_corrected - z;
            z = z_corrected;
        }
        z += offset_m;
    } else if (direction == BALTIC_BALTIC_TO_ELLIPSOIDAL) {
        z -= offset_m;
        if (ctx->correction_op != NULL) {
            double z_corrected;

            status = run_vgrid(ctx->correction_op, PJ_INV, lon, lat_deg, z,
                               &z_corrected);
            if (status != BALTIC_OK)
                return status;
            correction = z - z_corrected;
            z = z_corrected;
        }

        {
            double z_ellipsoidal;

            status =
                run_vgrid(ctx->geoid_op, PJ_FWD, lon, lat_deg, z, &z_ellipsoidal);
            if (status != BALTIC_OK)
                return status;
            undulation = z_ellipsoidal - z;
            z = z_ellipsoidal;
        }
    } else {
        return BALTIC_ERR_INVALID_ARG;
    }

    *height_out_m = z;
    if (info != NULL) {
        info->geoid_undulation_m = undulation;
        info->correction_m = correction;
        info->datum_offset_m = offset_m;
    }
    return BALTIC_OK;
}

baltic_status baltic_convert(const baltic_ctx *ctx, baltic_direction direction,
                             double lat_deg, double lon_deg, double height_in_m,
                             double *height_out_m,
                             baltic_conversion_info *info) {
    baltic_status status;

    baltic_clear_error();
    if (ctx == NULL) {
        baltic_set_error("ctx is NULL");
        return BALTIC_ERR_INVALID_ARG;
    }
    if (height_out_m == NULL) {
        baltic_set_error("height_out_m is NULL");
        return BALTIC_ERR_INVALID_ARG;
    }

    baltic_mutex_lock((baltic_mutex *)&ctx->mutex);
    status = convert_locked((baltic_ctx *)ctx, direction, lat_deg, lon_deg,
                            height_in_m, ctx->offset_m, height_out_m, info);
    baltic_mutex_unlock((baltic_mutex *)&ctx->mutex);

    if (status == BALTIC_ERR_OUT_OF_GRID)
        baltic_set_error("point (%.9f, %.9f) is outside the grid coverage",
                         lat_deg, lon_deg);
    else if (status == BALTIC_ERR_INVALID_ARG)
        baltic_set_error("invalid coordinate or height");
    else if (status != BALTIC_OK)
        baltic_set_error("vertical grid transformation failed (%s)",
                         baltic_status_string(status));
    return status;
}

baltic_status baltic_ellipsoidal_to_baltic(const baltic_ctx *ctx, double lat_deg,
                                           double lon_deg, double h_m,
                                           double *H_m,
                                           baltic_conversion_info *info) {
    return baltic_convert(ctx, BALTIC_ELLIPSOIDAL_TO_BALTIC, lat_deg, lon_deg,
                          h_m, H_m, info);
}

baltic_status baltic_baltic_to_ellipsoidal(const baltic_ctx *ctx, double lat_deg,
                                           double lon_deg, double H_m,
                                           double *h_m,
                                           baltic_conversion_info *info) {
    return baltic_convert(ctx, BALTIC_BALTIC_TO_ELLIPSOIDAL, lat_deg, lon_deg,
                          H_m, h_m, info);
}

size_t baltic_convert_batch(const baltic_ctx *ctx, baltic_direction direction,
                            baltic_batch_point *points, size_t count) {
    size_t i;
    size_t ok = 0;

    baltic_clear_error();
    if (ctx == NULL || points == NULL || count == 0)
        return 0;

    baltic_mutex_lock((baltic_mutex *)&ctx->mutex);
    for (i = 0; i < count; ++i) {
        baltic_conversion_info info;
        double out = NAN;
        baltic_status status;

        points[i].height_out_m = NAN;
        points[i].geoid_undulation_m = NAN;
        points[i].correction_m = NAN;
        status = convert_locked((baltic_ctx *)ctx, direction, points[i].lat_deg,
                                points[i].lon_deg, points[i].height_in_m,
                                ctx->offset_m, &out, &info);
        points[i].status = status;
        if (status == BALTIC_OK) {
            points[i].height_out_m = out;
            points[i].geoid_undulation_m = info.geoid_undulation_m;
            points[i].correction_m = info.correction_m;
            ++ok;
        }
    }
    baltic_mutex_unlock((baltic_mutex *)&ctx->mutex);

    if (ok != count)
        baltic_set_error("%zu of %zu points failed", count - ok, count);
    return ok;
}

baltic_status baltic_calibrate_offset(const baltic_ctx *ctx,
                                      const baltic_control_point *points,
                                      size_t count, baltic_calibration *out) {
    size_t i;
    size_t used = 0;
    size_t skipped = 0;
    int saw_out_of_grid = 0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min_r = INFINITY;
    double max_r = -INFINITY;

    baltic_clear_error();
    if (ctx == NULL || points == NULL || out == NULL || count == 0) {
        baltic_set_error("invalid calibration input");
        return BALTIC_ERR_INVALID_ARG;
    }

    baltic_mutex_lock((baltic_mutex *)&ctx->mutex);
    for (i = 0; i < count; ++i) {
        double model_height = 0.0;
        double residual;
        baltic_status status;

        if (!isfinite(points[i].height_baltic_m)) {
            ++skipped;
            continue;
        }
        status = convert_locked((baltic_ctx *)ctx,
                                BALTIC_ELLIPSOIDAL_TO_BALTIC, points[i].lat_deg,
                                points[i].lon_deg, points[i].height_ellipsoidal_m,
                                0.0, &model_height, NULL);
        if (status != BALTIC_OK) {
            ++skipped;
            if (status == BALTIC_ERR_OUT_OF_GRID)
                saw_out_of_grid = 1;
            continue;
        }
        residual = points[i].height_baltic_m - model_height;
        sum += residual;
        sum_sq += residual * residual;
        if (residual < min_r)
            min_r = residual;
        if (residual > max_r)
            max_r = residual;
        ++used;
    }
    baltic_mutex_unlock((baltic_mutex *)&ctx->mutex);

    if (used == 0) {
        baltic_set_error("no usable control points");
        return saw_out_of_grid ? BALTIC_ERR_OUT_OF_GRID
                               : BALTIC_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->offset_m = sum / (double)used;
    {
        double variance = sum_sq / (double)used - out->offset_m * out->offset_m;
        if (variance < 0.0)
            variance = 0.0;
        out->rms_m = sqrt(variance);
    }
    out->min_residual_m = min_r - out->offset_m;
    out->max_residual_m = max_r - out->offset_m;
    out->used_points = used;
    out->skipped_points = skipped;
    return BALTIC_OK;
}

baltic_status baltic_ctx_grid_info(const baltic_ctx *ctx, baltic_grid_info *out) {
    baltic_clear_error();
    if (ctx == NULL || out == NULL) {
        baltic_set_error("invalid grid info arguments");
        return BALTIC_ERR_INVALID_ARG;
    }
    baltic_mutex_lock((baltic_mutex *)&ctx->mutex);
    *out = ctx->geoid_info;
    baltic_mutex_unlock((baltic_mutex *)&ctx->mutex);
    return BALTIC_OK;
}

/* ------------------------------------------------------------------------- */
/* Introspection                                                             */
/* ------------------------------------------------------------------------- */

const char *baltic_status_string(baltic_status status) {
    switch (status) {
    case BALTIC_OK:
        return "ok";
    case BALTIC_ERR_INVALID_ARG:
        return "invalid argument";
    case BALTIC_ERR_NO_MEMORY:
        return "out of memory";
    case BALTIC_ERR_GRID_NOT_FOUND:
        return "grid not found";
    case BALTIC_ERR_GRID_INVALID:
        return "grid is invalid or unreadable";
    case BALTIC_ERR_OUT_OF_GRID:
        return "point is outside grid coverage";
    case BALTIC_ERR_PROJ:
        return "PROJ error";
    case BALTIC_ERR_INTERNAL:
        return "internal error";
    default:
        return "unknown status";
    }
}

const char *baltic_version(void) { return BALTIC_VERSION_STRING; }

const char *baltic_proj_version(void) {
    PJ_INFO info = proj_info();
    return info.version != NULL ? info.version : "unknown";
}
