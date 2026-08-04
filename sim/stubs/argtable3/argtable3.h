/* sim/stubs/argtable3/argtable3.h - argtable3 stub（仅 cli.c 使用，已 override） */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *hdr;
    const char *opt;
    const char *name;
} arg_lit_t, arg_int_t, arg_str_t, arg_file_t, arg_rex_t, arg_dbl_t;

static inline void arg_print_errors(void *fp, void *argtable, const char *prog) {
    (void)fp; (void)argtable; (void)prog;
}
static inline void arg_freetable(void *argtable, size_t n) { (void)argtable; (void)n; }
static inline void arg_print_glossary(void *fp, void *argtable, const char *fmt) {
    (void)fp; (void)argtable; (void)fmt;
}
static inline void arg_print_syntax(void *fp, void *argtable, const char *suffix) {
    (void)fp; (void)argtable; (void)suffix;
}

#define ARG_DBL_FS ","
#define ARG_LIT_FS ","

#ifdef __cplusplus
}
#endif
