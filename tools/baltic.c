/*
 * baltic command line utility.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "baltic/baltic.h"

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_SIZE 4096

typedef enum output_format {
    FORMAT_CSV = 0,
    FORMAT_JSONL = 1
} output_format;

typedef struct options {
    const char *grid;
    const char *correction;
    const char *input;
    const char *output;
    const char *points;
    const char *search_path;
    const char *format_name;
    double offset;
    int precision;
    int reverse;
    int no_header;
    int quiet;
    output_format format;
} options;

static const char *progname = "baltic";

static void print_usage(FILE *out) {
    fprintf(out,
            "baltic %s - преобразование высот EGM2008 <-> Балтийская система\n"
            "\n"
            "Использование:\n"
            "  %s convert --grid FILE [опции]\n"
            "  %s info --grid FILE\n"
            "  %s calibrate --grid FILE --points FILE\n"
            "  %s version\n"
            "\n"
            "Общие опции:\n"
            "  --grid FILE         сетка EGM2008 (GeoTIFF или GTX), обязательна\n"
            "  --correction FILE   необязательная поправочная сетка, м\n"
            "  --search-path DIR   дополнительный каталог поиска сеток\n"
            "  --offset M          постоянная поправка датума, м (по умолчанию 0)\n"
            "  --quiet             не печатать итоговую сводку\n"
            "  --version           версия библиотеки и PROJ\n"
            "  --help              эта справка\n"
            "\n"
            "convert:\n"
            "  --reverse           Балтийская -> эллипсоидальная (по умолчанию h -> H)\n"
            "  --input FILE        входной CSV (по умолчанию stdin, '-' = stdin)\n"
            "  --output FILE       выходной файл (по умолчанию stdout)\n"
            "  --format csv|jsonl  формат вывода (по умолчанию csv)\n"
            "  --precision N       знаков после запятой для высот (по умолчанию 6)\n"
            "  --no-header         не печатать заголовок CSV\n"
            "\n"
            "calibrate:\n"
            "  --points FILE       CSV с пунктами lon,lat,h,H (обязателен)\n"
            "\n"
            "Входной CSV: lon,lat,height. Строки с '#' и заголовок игнорируются.\n"
            "Поправка датума оценивается по пунктам GNSS-нивелирования:\n"
            "  offset = mean(H_изв - (h - N + C))\n",
            baltic_version(), progname, progname, progname, progname);
}

static void print_version(void) {
    printf("baltic %s\n", baltic_version());
    printf("PROJ %s\n", baltic_proj_version());
}

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > 15)
        return -1;
    *out = (int)value;
    return 0;
}

static int parse_double(const char *text, double *out) {
    char *end = NULL;

    errno = 0;
    *out = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(*out))
        return -1;
    return 0;
}

/*
 * Parse up to max numbers from a CSV/whitespace line.
 * Returns the number of values, 0 for an empty/comment line, or -1 when
 * the line is malformed.
 */
static int parse_numbers(char *line, double *values, int max) {
    char *p = line;
    int count = 0;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p == '\0' || *p == '\r' || *p == '\n' || *p == '#')
        return 0;

    while (count < max) {
        char *end = NULL;
        double value;

        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';')
            ++p;
        if (*p == '\0' || *p == '\r' || *p == '\n' || *p == '#')
            break;

        errno = 0;
        value = strtod(p, &end);
        if (errno != 0 || end == p || !isfinite(value))
            return -1;
        values[count++] = value;
        p = end;
    }

    while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';')
        ++p;
    if (*p != '\0' && *p != '\r' && *p != '\n' && *p != '#')
        return -1;
    return count;
}

static int looks_like_header(const char *line) {
    const unsigned char *p = (const unsigned char *)line;

    while (*p != '\0') {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
            return 1;
        if (*p >= '0' && *p <= '9')
            return 0;
        ++p;
    }
    return 0;
}

static void print_number(FILE *out, double value, int precision) {
    if (isfinite(value))
        fprintf(out, "%.*f", precision, value);
    else
        fputs("nan", out);
}

static const char *command_status(baltic_status status) {
    return baltic_status_string(status);
}

static int open_input(const options *opt, FILE **out) {
    if (opt->input == NULL || strcmp(opt->input, "-") == 0) {
        *out = stdin;
        return 0;
    }
    *out = fopen(opt->input, "r");
    if (*out == NULL) {
        fprintf(stderr, "%s: не удалось открыть '%s': %s\n", progname,
                opt->input, strerror(errno));
        return -1;
    }
    return 0;
}

static int open_output(const options *opt, FILE **out) {
    if (opt->output == NULL || strcmp(opt->output, "-") == 0) {
        *out = stdout;
        return 0;
    }
    *out = fopen(opt->output, "w");
    if (*out == NULL) {
        fprintf(stderr, "%s: не удалось создать '%s': %s\n", progname,
                opt->output, strerror(errno));
        return -1;
    }
    return 0;
}

static void write_csv_header(FILE *out, int reverse) {
    fprintf(out, "lon,lat,%s,%s,egm2008_n,correction,offset,status\n",
            reverse ? "baltic_h" : "ellipsoidal_h",
            reverse ? "ellipsoidal_h" : "baltic_h");
}

static void write_csv_row(FILE *out, const options *opt,
                          const baltic_batch_point *point) {
    fprintf(out, "%.9f,%.9f,", point->lon_deg, point->lat_deg);
    print_number(out, point->height_in_m, opt->precision);
    fputc(',', out);
    print_number(out, point->height_out_m, opt->precision);
    fputc(',', out);
    print_number(out, point->geoid_undulation_m, opt->precision);
    fputc(',', out);
    print_number(out, point->correction_m, opt->precision);
    fprintf(out, ",");
    print_number(out, opt->offset, opt->precision);
    fprintf(out, ",%s\n", command_status(point->status));
}

static void write_json_row(FILE *out, const options *opt,
                           const baltic_batch_point *point) {
    fprintf(out, "{\"lon\":%.9f,\"lat\":%.9f,\"in\":", point->lon_deg,
            point->lat_deg);
    if (isfinite(point->height_in_m))
        fprintf(out, "%.*f", opt->precision, point->height_in_m);
    else
        fputs("null", out);
    fputs(",\"out\":", out);
    if (isfinite(point->height_out_m))
        fprintf(out, "%.*f", opt->precision, point->height_out_m);
    else
        fputs("null", out);
    fputs(",\"n\":", out);
    if (isfinite(point->geoid_undulation_m))
        fprintf(out, "%.*f", opt->precision, point->geoid_undulation_m);
    else
        fputs("null", out);
    fputs(",\"correction\":", out);
    if (isfinite(point->correction_m))
        fprintf(out, "%.*f", opt->precision, point->correction_m);
    else
        fputs("null", out);
    fputs(",\"status\":\"", out);
    fputs(command_status(point->status), out);
    fputs("\"}\n", out);
}

static int convert_command(const options *opt) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    baltic_status status;
    baltic_direction direction = opt->reverse
                                     ? BALTIC_BALTIC_TO_ELLIPSOIDAL
                                     : BALTIC_ELLIPSOIDAL_TO_BALTIC;
    FILE *in = NULL;
    FILE *out = NULL;
    char line[8192];
    baltic_batch_point batch[CHUNK_SIZE];
    size_t batch_count = 0;
    size_t total = 0;
    size_t failed = 0;
    int header_seen = 0;
    int exit_code = 0;

    config.geoid_grid = opt->grid;
    config.correction_grid = opt->correction;
    config.grid_search_path = opt->search_path;
    config.datum_offset_m = opt->offset;

    status = baltic_ctx_create(&config, &ctx);
    if (status != BALTIC_OK) {
        fprintf(stderr, "%s: %s (%s)\n", progname,
                baltic_last_error()[0] ? baltic_last_error()
                                       : baltic_status_string(status),
                baltic_status_string(status));
        return 2;
    }

    if (open_input(opt, &in) != 0 || open_output(opt, &out) != 0) {
        baltic_ctx_destroy(ctx);
        if (in != NULL && in != stdin)
            fclose(in);
        if (out != NULL && out != stdout)
            fclose(out);
        return 2;
    }

    if (opt->format == FORMAT_CSV && !opt->no_header)
        write_csv_header(out, opt->reverse);

    while (fgets(line, sizeof(line), in) != NULL) {
        double values[3];
        int parsed;

        if (strchr(line, '\n') == NULL && !feof(in)) {
            fprintf(stderr, "%s: строка слишком длинная\n", progname);
            exit_code = 2;
            break;
        }
        parsed = parse_numbers(line, values, 3);
        if (parsed == 0)
            continue;
        if (parsed != 3) {
            if (!header_seen && looks_like_header(line)) {
                header_seen = 1;
                continue;
            }
            fprintf(stderr, "%s: некорректная строка: %s", progname, line);
            exit_code = 2;
            break;
        }
        header_seen = 1;

        batch[batch_count].lon_deg = values[0];
        batch[batch_count].lat_deg = values[1];
        batch[batch_count].height_in_m = values[2];
        ++batch_count;
        ++total;

        if (batch_count == CHUNK_SIZE) {
            size_t i;

            (void)baltic_convert_batch(ctx, direction, batch, batch_count);
            for (i = 0; i < batch_count; ++i) {
                if (batch[i].status != BALTIC_OK)
                    ++failed;
                if (opt->format == FORMAT_CSV)
                    write_csv_row(out, opt, &batch[i]);
                else
                    write_json_row(out, opt, &batch[i]);
            }
            batch_count = 0;
        }
    }

    if (batch_count > 0 && exit_code == 0) {
        size_t i;

        (void)baltic_convert_batch(ctx, direction, batch, batch_count);
        for (i = 0; i < batch_count; ++i) {
            if (batch[i].status != BALTIC_OK)
                ++failed;
            if (opt->format == FORMAT_CSV)
                write_csv_row(out, opt, &batch[i]);
            else
                write_json_row(out, opt, &batch[i]);
        }
    }

    if (ferror(in)) {
        fprintf(stderr, "%s: ошибка чтения: %s\n", progname, strerror(errno));
        exit_code = 2;
    }
    if (ferror(out)) {
        fprintf(stderr, "%s: ошибка записи: %s\n", progname, strerror(errno));
        exit_code = 2;
    }

    if (in != stdin)
        fclose(in);
    if (out != stdout)
        fclose(out);
    baltic_ctx_destroy(ctx);

    if (!opt->quiet)
        fprintf(stderr, "%s: обработано %zu, ошибок %zu\n", progname, total,
                failed);
    if (exit_code != 0)
        return exit_code;
    return failed > 0 ? 1 : 0;
}

static int info_command(const options *opt) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    baltic_grid_info info;
    baltic_status status;

    config.geoid_grid = opt->grid;
    config.grid_search_path = opt->search_path;
    status = baltic_ctx_create(&config, &ctx);
    if (status != BALTIC_OK) {
        fprintf(stderr, "%s: %s\n", progname,
                baltic_last_error()[0] ? baltic_last_error()
                                       : baltic_status_string(status));
        return 2;
    }
    status = baltic_ctx_grid_info(ctx, &info);
    if (status != BALTIC_OK) {
        fprintf(stderr, "%s: %s\n", progname, baltic_status_string(status));
        baltic_ctx_destroy(ctx);
        return 2;
    }

    printf("path=%s\n", info.path);
    printf("format=%s\n", info.format);
    printf("west_deg=%.9f\n", info.west_deg);
    printf("south_deg=%.9f\n", info.south_deg);
    printf("east_deg=%.9f\n", info.east_deg);
    printf("north_deg=%.9f\n", info.north_deg);
    printf("width=%d\n", info.width);
    printf("height=%d\n", info.height);
    printf("dlon_deg=%.9f\n", info.dlon_deg);
    printf("dlat_deg=%.9f\n", info.dlat_deg);

    baltic_ctx_destroy(ctx);
    return 0;
}

static int calibrate_command(const options *opt) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    baltic_status status;
    FILE *in = NULL;
    char line[8192];
    baltic_control_point *points = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int header_seen = 0;
    int exit_code = 0;
    baltic_calibration calibration;

    config.geoid_grid = opt->grid;
    config.correction_grid = opt->correction;
    config.grid_search_path = opt->search_path;
    config.datum_offset_m = 0.0;

    status = baltic_ctx_create(&config, &ctx);
    if (status != BALTIC_OK) {
        fprintf(stderr, "%s: %s\n", progname,
                baltic_last_error()[0] ? baltic_last_error()
                                       : baltic_status_string(status));
        return 2;
    }

    if (opt->points == NULL) {
        fprintf(stderr, "%s: для calibrate нужен --points FILE\n", progname);
        baltic_ctx_destroy(ctx);
        return 2;
    }
    if (open_input(&(options){.input = opt->points}, &in) != 0) {
        baltic_ctx_destroy(ctx);
        return 2;
    }

    while (fgets(line, sizeof(line), in) != NULL) {
        double values[4];
        int parsed;

        if (strchr(line, '\n') == NULL && !feof(in)) {
            fprintf(stderr, "%s: строка слишком длинная\n", progname);
            exit_code = 2;
            break;
        }
        parsed = parse_numbers(line, values, 4);
        if (parsed == 0)
            continue;
        if (parsed != 4) {
            if (!header_seen && looks_like_header(line)) {
                header_seen = 1;
                continue;
            }
            fprintf(stderr, "%s: некорректная строка: %s", progname, line);
            exit_code = 2;
            break;
        }
        header_seen = 1;

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 128 : capacity * 2;
            baltic_control_point *new_points = (baltic_control_point *)realloc(
                points, new_capacity * sizeof(*new_points));

            if (new_points == NULL) {
                fprintf(stderr, "%s: недостаточно памяти\n", progname);
                exit_code = 2;
                break;
            }
            points = new_points;
            capacity = new_capacity;
        }
        points[count].lon_deg = values[0];
        points[count].lat_deg = values[1];
        points[count].height_ellipsoidal_m = values[2];
        points[count].height_baltic_m = values[3];
        ++count;
    }

    if (in != stdin)
        fclose(in);

    if (exit_code == 0) {
        status = baltic_calibrate_offset(ctx, points, count, &calibration);
        if (status != BALTIC_OK) {
            fprintf(stderr, "%s: %s\n", progname,
                    baltic_last_error()[0] ? baltic_last_error()
                                           : baltic_status_string(status));
            exit_code = 2;
        } else {
            printf("points_used=%zu\n", calibration.used_points);
            printf("points_skipped=%zu\n", calibration.skipped_points);
            printf("offset_m=%.9f\n", calibration.offset_m);
            printf("rms_m=%.9f\n", calibration.rms_m);
            printf("min_residual_m=%.9f\n", calibration.min_residual_m);
            printf("max_residual_m=%.9f\n", calibration.max_residual_m);
        }
    }

    free(points);
    baltic_ctx_destroy(ctx);
    return exit_code;
}

int main(int argc, char **argv) {
    static const struct option long_options[] = {
        {"grid", required_argument, NULL, 'g'},
        {"correction", required_argument, NULL, 'c'},
        {"search-path", required_argument, NULL, 's'},
        {"offset", required_argument, NULL, 'o'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'O'},
        {"points", required_argument, NULL, 'p'},
        {"format", required_argument, NULL, 'f'},
        {"precision", required_argument, NULL, 'P'},
        {"reverse", no_argument, NULL, 'r'},
        {"no-header", no_argument, NULL, 'H'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}};
    options opt;
    const char *command;
    int option;
    int parse_error = 0;

    memset(&opt, 0, sizeof(opt));
    opt.input = NULL;
    opt.output = NULL;
    opt.precision = 6;
    opt.format_name = "csv";
    opt.format = FORMAT_CSV;

    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    command = argv[1];
    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0) {
        print_version();
        return 0;
    }

    optind = 1;
    while ((option = getopt_long(argc - 1, argv + 1, "", long_options, NULL)) !=
           -1) {
        switch (option) {
        case 'g':
            opt.grid = optarg;
            break;
        case 'c':
            opt.correction = optarg;
            break;
        case 's':
            opt.search_path = optarg;
            break;
        case 'o':
            if (parse_double(optarg, &opt.offset) != 0) {
                fprintf(stderr, "%s: некорректный --offset '%s'\n", progname,
                        optarg);
                parse_error = 1;
            }
            break;
        case 'i':
            opt.input = optarg;
            break;
        case 'O':
            opt.output = optarg;
            break;
        case 'p':
            opt.points = optarg;
            break;
        case 'f':
            opt.format_name = optarg;
            break;
        case 'P':
            if (parse_int(optarg, &opt.precision) != 0) {
                fprintf(stderr, "%s: некорректный --precision '%s'\n", progname,
                        optarg);
                parse_error = 1;
            }
            break;
        case 'r':
            opt.reverse = 1;
            break;
        case 'H':
            opt.no_header = 1;
            break;
        case 'q':
            opt.quiet = 1;
            break;
        case 'h':
            print_usage(stdout);
            return 0;
        case 'V':
            print_version();
            return 0;
        case '?':
            parse_error = 1;
            break;
        default:
            parse_error = 1;
            break;
        }
    }

    if (parse_error) {
        print_usage(stderr);
        return 2;
    }
    if (strcmp(opt.format_name, "csv") == 0)
        opt.format = FORMAT_CSV;
    else if (strcmp(opt.format_name, "jsonl") == 0)
        opt.format = FORMAT_JSONL;
    else {
        fprintf(stderr, "%s: неизвестный формат '%s'\n", progname,
                opt.format_name);
        return 2;
    }

    if (strcmp(command, "convert") == 0) {
        if (opt.grid == NULL) {
            fprintf(stderr, "%s: укажите --grid FILE\n", progname);
            return 2;
        }
        return convert_command(&opt);
    }
    if (strcmp(command, "info") == 0) {
        if (opt.grid == NULL) {
            fprintf(stderr, "%s: укажите --grid FILE\n", progname);
            return 2;
        }
        return info_command(&opt);
    }
    if (strcmp(command, "calibrate") == 0) {
        if (opt.grid == NULL || opt.points == NULL) {
            fprintf(stderr, "%s: укажите --grid FILE и --points FILE\n",
                    progname);
            return 2;
        }
        return calibrate_command(&opt);
    }

    fprintf(stderr, "%s: неизвестная команда '%s'\n", progname, command);
    print_usage(stderr);
    return 2;
}
