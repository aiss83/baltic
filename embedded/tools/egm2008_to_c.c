/*
 * egm2008_to_c - crop/resample an EGM2008 grid into a compact C array for
 * the baltic_embedded library.
 *
 * The generator runs on a host, uses the full libbaltic (PROJ + GeoTIFF)
 * and writes two files:
 *
 *   <name>.h  - extern declaration of the grid descriptor
 *   <name>.c  - int16_t samples and const descriptor
 *
 * Samples are stored in millimetres relative to a per-grid base, row-major
 * from south to north.  The peak-to-peak variation must fit into int16_t
 * (65.5 m), which is always true for reasonably sized regions.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "baltic/baltic.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct options {
    const char *grid;
    const char *name;
    const char *out_dir;
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
    int step_arcsec;
} options;

static void usage(FILE *out) {
    fprintf(out,
            "Usage: egm2008_to_c --grid FILE --name NAME --out-dir DIR\n"
            "                     --lat-min DEG --lat-max DEG\n"
            "                     --lon-min DEG --lon-max DEG\n"
            "                     [--step-arcsec N]\n"
            "\n"
            "Crops EGM2008 into a C array for baltic_embedded.\n"
            "Default step is 150 arc-seconds (2.5 arc-minutes).\n"
            "Example:\n"
            "  egm2008_to_c --grid data/egm2008/us_nga_egm08_25.tif \\\n"
            "      --name egm2008_moscow_2deg --out-dir embedded/grids \\\n"
            "      --lat-min 54 --lat-max 56 --lon-min 36 --lon-max 38\n");
}

static int parse_double_arg(const char *text, double *out) {
    char *end = NULL;

    errno = 0;
    *out = strtod(text, &end);
    return errno == 0 && end != text && *end == '\0' && isfinite(*out) ? 0
                                                                      : -1;
}

static int parse_int_arg(const char *text, int *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 ||
        value > 3600)
        return -1;
    *out = (int)value;
    return 0;
}

static int valid_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;

    if (p == NULL || *p == '\0')
        return 0;
    if (!(isalpha(*p) || *p == '_'))
        return 0;
    for (++p; *p != '\0'; ++p) {
        if (!(isalnum(*p) || *p == '_'))
            return 0;
    }
    return 1;
}

static int parse_args(int argc, char **argv, options *opt) {
    int i;

    memset(opt, 0, sizeof(*opt));
    opt->step_arcsec = 150;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            exit(0);
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", arg);
            return -1;
        }
        value = argv[++i];
        if (strcmp(arg, "--grid") == 0)
            opt->grid = value;
        else if (strcmp(arg, "--name") == 0)
            opt->name = value;
        else if (strcmp(arg, "--out-dir") == 0)
            opt->out_dir = value;
        else if (strcmp(arg, "--lat-min") == 0) {
            if (parse_double_arg(value, &opt->lat_min) != 0)
                return -1;
        } else if (strcmp(arg, "--lat-max") == 0) {
            if (parse_double_arg(value, &opt->lat_max) != 0)
                return -1;
        } else if (strcmp(arg, "--lon-min") == 0) {
            if (parse_double_arg(value, &opt->lon_min) != 0)
                return -1;
        } else if (strcmp(arg, "--lon-max") == 0) {
            if (parse_double_arg(value, &opt->lon_max) != 0)
                return -1;
        } else if (strcmp(arg, "--step-arcsec") == 0) {
            if (parse_int_arg(value, &opt->step_arcsec) != 0)
                return -1;
        } else {
            fprintf(stderr, "unknown option: %s\n", arg);
            return -1;
        }
    }

    if (opt->grid == NULL || opt->name == NULL || opt->out_dir == NULL) {
        fprintf(stderr, "--grid, --name and --out-dir are required\n");
        return -1;
    }
    if (!valid_name(opt->name) || strlen(opt->name) > 64) {
        fprintf(stderr,
                "invalid --name: use up to 64 letters, digits and "
                "underscores, starting with a letter or underscore\n");
        return -1;
    }
    if (opt->lat_min < -90.0 || opt->lat_max > 90.0 ||
        opt->lat_min >= opt->lat_max || opt->lon_min < -180.0 ||
        opt->lon_max > 180.0 || opt->lon_min >= opt->lon_max) {
        fprintf(stderr, "invalid region bounds\n");
        return -1;
    }
    return 0;
}

static char *make_path(const char *dir, const char *name, const char *ext) {
    size_t n = strlen(dir) + strlen(name) + strlen(ext) + 2;
    char *path = (char *)malloc(n);

    if (path != NULL)
        (void)snprintf(path, n, "%s/%s%s", dir, name, ext);
    return path;
}

static void upper_identifier(const char *name, char *out, size_t out_size) {
    size_t i;

    for (i = 0; name[i] != '\0' && i + 1 < out_size; ++i)
        out[i] = (char)toupper((unsigned char)name[i]);
    out[i] = '\0';
}

static int write_header(const char *path, const char *name) {
    FILE *fp = fopen(path, "w");
    char upper[128];

    if (fp == NULL) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    upper_identifier(name, upper, sizeof(upper));
    fprintf(fp,
            "/* Generated by egm2008_to_c. Do not edit. */\n"
            "#ifndef BALTIC_GRID_%s_H\n"
            "#define BALTIC_GRID_%s_H\n"
            "\n"
            "#include \"baltic_embedded.h\"\n"
            "\n"
            "#ifdef __cplusplus\n"
            "extern \"C\" {\n"
            "#endif\n"
            "\n"
            "extern const baltic_embedded_grid %s;\n"
            "\n"
            "#ifdef __cplusplus\n"
            "}\n"
            "#endif\n"
            "\n"
            "#endif /* BALTIC_GRID_%s_H */\n",
            upper, upper, name, upper);
    if (fclose(fp) != 0) {
        fprintf(stderr, "cannot close %s\n", path);
        return -1;
    }
    return 0;
}

static int write_source(const char *path, const char *name,
                        const char *source_grid, const int16_t *values,
                        int width, int height, int32_t base_mm,
                        int32_t lon_min_u6, int32_t lat_min_u6,
                        int32_t lon_max_u6, int32_t lat_max_u6,
                        int32_t lon_step_u6, int32_t lat_step_u6,
                        int step_arcsec) {
    FILE *fp = fopen(path, "w");
    size_t count = (size_t)width * (size_t)height;
    size_t i;

    if (fp == NULL) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(fp,
            "/* Generated by egm2008_to_c from %s. Do not edit. */\n"
            "/* %d x %d nodes, step %d arcsec, int16 mm relative to base. */\n"
            "#include \"baltic_embedded.h\"\n"
            "#include \"%s.h\"\n"
            "\n"
            "#ifndef BALTIC_GRID_SECTION\n"
            "#if (defined(__GNUC__) || defined(__clang__)) && defined(__ELF__)\n"
            "#define BALTIC_GRID_SECTION \\\n"
            "    __attribute__((section(\".baltic_grid\"), aligned(32), used))\n"
            "#else\n"
            "#define BALTIC_GRID_SECTION\n"
            "#endif\n"
            "#endif\n"
            "\n"
            "static const int16_t %s_values[%d * %d] BALTIC_GRID_SECTION = {\n",
            source_grid, width, height, step_arcsec, name, name, width,
            height);

    for (i = 0; i < count; ++i) {
        if (i % 12 == 0)
            fputs("    ", fp);
        fprintf(fp, "%6d", (int)values[i]);
        if (i + 1 != count)
            fputc(',', fp);
        if (i % 12 == 11 || i + 1 == count)
            fputc('\n', fp);
        else
            fputc(' ', fp);
    }

    fprintf(fp,
            "};\n"
            "\n"
            "const baltic_embedded_grid %s = {\n"
            "    .values = %s_values,\n"
            "    .width = %d,\n"
            "    .height = %d,\n"
            "    .base_mm = %ld,\n"
            "    .lon_min_u6 = %ld,\n"
            "    .lat_min_u6 = %ld,\n"
            "    .lon_max_u6 = %ld,\n"
            "    .lat_max_u6 = %ld,\n"
            "    .lon_step_u6 = %ld,\n"
            "    .lat_step_u6 = %ld\n"
            "};\n",
            name, name, width, height, (long)base_mm, (long)lon_min_u6,
            (long)lat_min_u6, (long)lon_max_u6, (long)lat_max_u6,
            (long)lon_step_u6, (long)lat_step_u6);

    if (fclose(fp) != 0) {
        fprintf(stderr, "cannot close %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    options opt;
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    int width;
    int height;
    int32_t lon_min_u6;
    int32_t lat_min_u6;
    int32_t lon_step_u6;
    int32_t lat_step_u6;
    int32_t lon_max_u6;
    int32_t lat_max_u6;
    int32_t base_mm;
    int16_t *values = NULL;
    int32_t *raw = NULL;
    int32_t min_mm = 2147483647L;
    int32_t max_mm = -2147483647L - 1L;
    size_t count;
    int iy;
    int ix;
    char *header_path = NULL;
    char *source_path = NULL;
    const char *source_grid;
    int rc = 1;

    if (parse_args(argc, argv, &opt) != 0) {
        usage(stderr);
        return 2;
    }

    {
        double lat_span = (opt.lat_max - opt.lat_min) * 3600.0;
        double lon_span = (opt.lon_max - opt.lon_min) * 3600.0;

        width = (int)floor(lon_span / (double)opt.step_arcsec + 1e-9) + 1;
        height = (int)floor(lat_span / (double)opt.step_arcsec + 1e-9) + 1;
    }
    if (width < 2 || height < 2 || width > 65535 || height > 65535) {
        fprintf(stderr, "invalid grid dimensions: %d x %d\n", width, height);
        return 2;
    }
    count = (size_t)width * (size_t)height;
    if (count > 16u * 1024u * 1024u) {
        fprintf(stderr, "grid too large for a C array: %zu nodes\n", count);
        return 2;
    }

    lat_min_u6 = (int32_t)llround(opt.lat_min * 1e6);
    lon_min_u6 = (int32_t)llround(opt.lon_min * 1e6);
    lat_step_u6 = (int32_t)llround((double)opt.step_arcsec * 1e6 / 3600.0);
    lon_step_u6 = lat_step_u6;
    lon_max_u6 = lon_min_u6 + (int32_t)(width - 1) * lon_step_u6;
    lat_max_u6 = lat_min_u6 + (int32_t)(height - 1) * lat_step_u6;
    if (lon_min_u6 < -180000000L || lon_max_u6 > 180000000L ||
        lat_min_u6 < -90000000L || lat_max_u6 > 90000000L) {
        fprintf(stderr, "grid bounds exceed the WGS 84 coordinate range\n");
        return 2;
    }

    raw = (int32_t *)malloc(count * sizeof(*raw));
    values = (int16_t *)malloc(count * sizeof(*values));
    if (raw == NULL || values == NULL) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }

    config.geoid_grid = opt.grid;
    if (baltic_ctx_create(&config, &ctx) != BALTIC_OK) {
        fprintf(stderr, "cannot open geoid grid: %s\n", baltic_last_error());
        goto cleanup;
    }

    fprintf(stderr, "sampling %d x %d nodes from %s ...\n", width, height,
            opt.grid);
    for (iy = 0; iy < height; ++iy) {
        double lat = opt.lat_min + (double)iy * (double)opt.step_arcsec / 3600.0;

        for (ix = 0; ix < width; ++ix) {
            double lon =
                opt.lon_min + (double)ix * (double)opt.step_arcsec / 3600.0;
            baltic_conversion_info info;
            double H = 0.0;
            long long mm;
            baltic_status status;

            status = baltic_ellipsoidal_to_baltic(ctx, lat, lon, 0.0, &H,
                                                  &info);
            if (status != BALTIC_OK) {
                fprintf(stderr, "sampling failed at %.7f, %.7f: %s\n", lat,
                        lon, baltic_status_string(status));
                goto cleanup;
            }
            mm = llround(info.geoid_undulation_m * 1000.0);
            if (mm < -2147483647LL || mm > 2147483647LL) {
                fprintf(stderr, "geoid value out of int32 range\n");
                goto cleanup;
            }
            raw[(size_t)iy * (size_t)width + (size_t)ix] = (int32_t)mm;
            if ((int32_t)mm < min_mm)
                min_mm = (int32_t)mm;
            if ((int32_t)mm > max_mm)
                max_mm = (int32_t)mm;
        }
    }

    if ((long long)max_mm - (long long)min_mm > 65534LL) {
        fprintf(stderr,
                "region variation is %.3f m; int16 encoding supports at most "
                "65.534 m. Reduce the region (a coarser step does not reduce "
                "the physical range).\n",
                ((double)max_mm - (double)min_mm) / 1000.0);
        goto cleanup;
    }

    base_mm = (int32_t)(((int64_t)min_mm + (int64_t)max_mm) / 2);
    for (size_t i = 0; i < count; ++i) {
        int32_t relative = raw[i] - base_mm;

        if (relative < -32768 || relative > 32767) {
            fprintf(stderr, "sample does not fit int16 after base offset\n");
            goto cleanup;
        }
        values[i] = (int16_t)relative;
    }

    header_path = make_path(opt.out_dir, opt.name, ".h");
    source_path = make_path(opt.out_dir, opt.name, ".c");
    if (header_path == NULL || source_path == NULL) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }
    source_grid = strrchr(opt.grid, '/');
    source_grid = source_grid != NULL ? source_grid + 1 : opt.grid;

    if (write_header(header_path, opt.name) != 0 ||
        write_source(source_path, opt.name, source_grid, values, width, height,
                     base_mm, lon_min_u6, lat_min_u6, lon_max_u6, lat_max_u6,
                     lon_step_u6, lat_step_u6, opt.step_arcsec) != 0)
        goto cleanup;

    printf("generated %s and %s\n", header_path, source_path);
    printf("  nodes: %zu (%zu bytes of samples)\n", count, count * 2);
    printf("  region: lat %.7f..%.7f, lon %.7f..%.7f\n", opt.lat_min,
           opt.lat_min + (double)(height - 1) * opt.step_arcsec / 3600.0,
           opt.lon_min,
           opt.lon_min + (double)(width - 1) * opt.step_arcsec / 3600.0);
    printf("  N range: %.3f..%.3f m, base %ld mm\n", (double)min_mm / 1000.0,
           (double)max_mm / 1000.0, (long)base_mm);
    rc = 0;

cleanup:
    if (ctx != NULL)
        baltic_ctx_destroy(ctx);
    free(raw);
    free(values);
    free(header_path);
    free(source_path);
    return rc;
}
