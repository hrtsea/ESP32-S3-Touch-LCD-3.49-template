/* sim/stubs/stdlib.h - 包装系统 stdlib.h，添加 POSIX setenv/unsetenv 声明
 *
 * MinGW-w64 标准库不提供 POSIX setenv/unsetenv（仅 Windows API
 * SetEnvironmentVariableA 和 CRT _putenv_s）。项目 i18n.c 调用 setenv
 * 设置 TZ 环境变量，此处通过 #include_next 包裹系统 stdlib.h 并补充声明。
 *
 * 实现在 sim/stubs/stubs.c 中提供（基于 _putenv_s）。
 */
#ifndef SIM_STDLIB_H_WRAPPER
#define SIM_STDLIB_H_WRAPPER

/* 包含系统原生 stdlib.h（编译器沿 include 路径继续搜索下一个 stdlib.h） */
#include_next <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* POSIX setenv：成功返回 0；overwrite=0 且变量已存在时不覆盖 */
int setenv(const char *name, const char *value, int overwrite);

/* POSIX unsetenv：成功返回 0；删除环境变量 */
int unsetenv(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SIM_STDLIB_H_WRAPPER */
