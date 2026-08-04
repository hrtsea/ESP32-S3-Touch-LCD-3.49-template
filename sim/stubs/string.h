/* sim/stubs/string.h - 包装系统 string.h，添加 BSD strlcpy 兼容声明
 *
 * MinGW 标准库不提供 strlcpy（BSD/Apple/ESP-IDF 函数）。
 * 项目代码假定 <string.h> 提供 strlcpy，故此处通过 #include_next
 * 包裹系统 string.h 并补充声明。
 *
 * 实现在 sim/stubs/stubs.c 中提供。
 */
#ifndef SIM_STRING_H_WRAPPER
#define SIM_STRING_H_WRAPPER

/* 包含系统原生 string.h（编译器沿 include 路径继续搜索下一个 string.h） */
#include_next <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BSD strlcpy：返回 src 长度，按 siz 截断拷贝到 dst（永远 NUL 结尾） */
size_t strlcpy(char *dst, const char *src, size_t siz);

/* BSD strlcat：返回 dst+src 长度，按 siz 截断追加到 dst */
size_t strlcat(char *dst, const char *src, size_t siz);

#ifdef __cplusplus
}
#endif

#endif /* SIM_STRING_H_WRAPPER */
