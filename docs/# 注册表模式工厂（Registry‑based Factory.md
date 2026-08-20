# 注册表模式工厂（Registry‑based Factory）
> 前面是传统 `switch‑case` 工厂；**注册表模式（注册工厂）**：各个NAS适配器在程序启动时把自己的「类型ID、名字、构造函数」注册到一张全局注册表数组；工厂不需要写庞大switch‑case，新增适配器**不需要修改工厂代码**，真正做到开闭原则。

适用场景：NAS适配器：群晖/QNAP/Unraid/串口模拟；后续还可能加更多NAS类型。

对比：
- switch‑case工厂：新增适配器 → 修改 `factory.c` 的switch，违反开闭原则。
- **注册表工厂**：每个适配器自注册，工厂只遍历注册表，新增适配器不用改工厂。

> 两种实现方式：
1. **静态编译注册表**：编译期把全部适配器注册进表（ESP‑IDF常用，简单稳定）
2. **动态自动注册（constructor属性）**：模块在main执行前自动注册，零注册调用，适合大项目。

## 整体目录不变
```
main/nas_adapter/
├── nas_adapter.h               # 抽象接口、数据结构
├── nas_adapter_registry.h      # 注册表定义
├── nas_adapter_registry.c      # 注册表工厂实现
├── nas_adapter_synology.c
├── nas_adapter_qnap.c
├── nas_adapter_unraid.c
├── nas_adapter_local_uart.c
└── nas_adapter_private.h
```

## 1、nas_adapter.h 不变（沿用之前抽象接口）
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NAS_ADAPTER_TYPE_NONE = 0,
    NAS_ADAPTER_TYPE_SYNOLOGY,
    NAS_ADAPTER_TYPE_QNAP,
    NAS_ADAPTER_TYPE_UNRAID,
    NAS_ADAPTER_TYPE_LOCAL_UART,
    NAS_ADAPTER_TYPE_MAX
} nas_adapter_type_t;

// 硬盘、nas_state_t 结构体完全复用前面代码

typedef struct nas_adapter_s nas_adapter_t;

struct nas_adapter_s {
    nas_adapter_type_t type;
    int (*init)(nas_adapter_t *self);
    void (*deinit)(nas_adapter_t *self);
    int (*poll)(nas_adapter_t *self, nas_state_t *out_state);
    const char* (*get_name)(nas_adapter_t *self);
    void *priv;
};

// 适配器构造函数类型：返回适配器实例
typedef nas_adapter_t* (*nas_adapter_create_fn)(void);
```

## 2、nas_adapter_registry.h 注册表头
注册表每一条记录：`type`、可读名字、构造函数。
```c
#pragma once
#include "nas_adapter.h"

// 注册表单条记录
typedef struct {
    nas_adapter_type_t      type;
    const char             *name;         // 字符串id，用于配置解析 "synology"
    const char             *display_name; // UI显示名字 "群晖 Synology"
    nas_adapter_create_fn   create;      // 构造函数指针
} nas_adapter_reg_entry_t;

/**
 * @brief 注册适配器到注册表
 */
int nas_adapter_reg_register(const nas_adapter_reg_entry_t *entry);

/**
 * @brief 根据type创建适配器
 */
nas_adapter_t* nas_adapter_factory_create(nas_adapter_type_t type);

/**
 * @brief 根据字符串名字查找type（NVS配置）
 */
nas_adapter_type_t nas_adapter_factory_lookup_type(const char *name);

/**
 * @brief 根据type拿到注册表条目
 */
const nas_adapter_reg_entry_t* nas_adapter_reg_get_entry(nas_adapter_type_t type);

/**
 * @brief 遍历注册表，用于UI生成下拉选项
 * @param idx 迭代索引，0开始
 * @return NULL表示遍历结束
 */
const nas_adapter_reg_entry_t* nas_adapter_reg_iterate(int idx);

/**
 * @brief 销毁适配器实例
 */
void nas_adapter_factory_destroy(nas_adapter_t *adapter);
```

## 3、nas_adapter_registry.c 注册表核心实现
内部维护一张全局注册表数组。
```c
#include "nas_adapter_registry.h"
#include <string.h>
#include <stdlib.h>

// 最大支持适配器数量，可以预留余量
#define NAS_ADAPTER_REG_MAX_ENTRY 16
static nas_adapter_reg_entry_t g_registry[NAS_ADAPTER_REG_MAX_ENTRY];
static int g_reg_count = 0;

int nas_adapter_reg_register(const nas_adapter_reg_entry_t *entry)
{
    if (!entry || g_reg_count >= NAS_ADAPTER_REG_MAX_ENTRY) {
        return -1;
    }
    g_registry[g_reg_count++] = *entry;
    return 0;
}

nas_adapter_t* nas_adapter_factory_create(nas_adapter_type_t type)
{
    for(int i = 0; i < g_reg_count; i++)
    {
        const nas_adapter_reg_entry_t *e = &g_registry[i];
        if(e->type == type && e->create != NULL)
        {
            return e->create();
        }
    }
    return NULL;
}

const nas_adapter_reg_entry_t* nas_adapter_reg_get_entry(nas_adapter_type_t type)
{
    for(int i = 0; i < g_reg_count; i++)
    {
        const nas_adapter_reg_entry_t *e = &g_registry[i];
        if(e->type == type)
        {
            return e;
        }
    }
    return NULL;
}

nas_adapter_type_t nas_adapter_factory_lookup_type(const char *name)
{
    if(!name) return NAS_ADAPTER_TYPE_NONE;
    for(int i = 0; i < g_reg_count; i++)
    {
        const nas_adapter_reg_entry_t *e = &g_registry[i];
        if(strcmp(e->name, name) == 0)
        {
            return e->type;
        }
    }
    return NAS_ADAPTER_TYPE_NONE;
}

const nas_adapter_reg_entry_t* nas_adapter_reg_iterate(int idx)
{
    if(idx <0 || idx >= g_reg_count) return NULL;
    return &g_registry[idx];
}

void nas_adapter_factory_destroy(nas_adapter_t *adapter)
{
    if(!adapter) return;
    if(adapter->deinit) adapter->deinit(adapter);
    if(adapter->priv) free(adapter->priv);
    free(adapter);
}
```

## 4、适配器实现：以 local_uart 举例
> 每个适配器提供构造函数，**向外暴露一条注册表注册项**。

`nas_adapter_local_uart.c`
```c
#include "nas_adapter_registry.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t tick;
} local_uart_priv_t;

static int local_uart_init(nas_adapter_t *self)
{
    local_uart_priv_t *priv = self->priv;
    priv->tick = 0;
    return 0;
}

static void local_uart_deinit(nas_adapter_t *self) { (void)self; }

static int local_uart_poll(nas_adapter_t *self, nas_state_t *out_state)
{
    local_uart_priv_t *priv = self->priv;
    priv->tick++;
    memset(out_state,0,sizeof(nas_state_t));
    out_state->cpu_load = 20 + (priv->tick%30);
    out_state->cpu_temp = 42;
    out_state->fan_speed_rpm = 1200;
    out_state->hdd_count = 4;
    for(int i=0;i<4;i++){
        out_state->hdd_list[i].slot = i;
        out_state->hdd_list[i].present = true;
        out_state->hdd_list[i].temp_c = 35+i;
        out_state->hdd_list[i].fault = false;
    }
    out_state->alert = false;
    return 0;
}

static const char* local_uart_get_name(nas_adapter_t *self)
{
    (void)self;
    return "Local UART Sim";
}

// 构造函数
static nas_adapter_t* nas_adapter_local_uart_create(void)
{
    nas_adapter_t *adapter = malloc(sizeof(nas_adapter_t));
    local_uart_priv_t *priv = malloc(sizeof(local_uart_priv_t));
    if(!adapter || !priv) {
        free(adapter); free(priv);
        return NULL;
    }
    memset(adapter,0,sizeof(*adapter));
    memset(priv,0,sizeof(*priv));

    adapter->type = NAS_ADAPTER_TYPE_LOCAL_UART;
    adapter->priv = priv;
    adapter->init = local_uart_init;
    adapter->deinit = local_uart_deinit;
    adapter->poll = local_uart_poll;
    adapter->get_name = local_uart_get_name;
    return adapter;
}

// ========== 注册表注册条目 ==========
static const nas_adapter_reg_entry_t s_local_uart_entry = {
    .type = NAS_ADAPTER_TYPE_LOCAL_UART,
    .name = "local_uart",
    .display_name = "串口本地模拟",
    .create = nas_adapter_local_uart_create
};

// 模块注册，在应用初始化阶段调用一次
void nas_adapter_local_uart_register(void)
{
    nas_adapter_reg_register(&s_local_uart_entry);
}
```

> `synology / qnap / unraid` 和上面完全一样，每个适配器都提供：
> 1. 自己的`xxx_create()`构造函数
> 2. 静态的`nas_adapter_reg_entry_t`注册项
> 3. `nas_adapter_xxx_register()`注册函数

## 5、应用层统一注册全部适配器
在系统初始化时，把所有适配器注册进注册表。
`nas_adapter_private.h`
```c
#pragma once
void nas_adapter_synology_register(void);
void nas_adapter_qnap_register(void);
void nas_adapter_unraid_register(void);
void nas_adapter_local_uart_register(void);

// 一次性注册全部适配器
static inline void nas_adapter_register_all(void)
{
    nas_adapter_synology_register();
    nas_adapter_qnap_register();
    nas_adapter_unraid_register();
    nas_adapter_local_uart_register();
}
```

main.c 启动时调用一次
```c
#include "nas_adapter/nas_adapter_private.h"

void app_main(void)
{
    // 1.注册表注册所有适配器
    nas_adapter_register_all();

    // 2.读取NVS拿到nas类型
    nas_adapter_type_t cfg_type = nvs_read_nas_type();

    // 3.工厂创建实例，工厂内部遍历注册表查找
    g_nas_adapter = nas_adapter_factory_create(cfg_type);
    if(g_nas_adapter) {
        g_nas_adapter->init(g_nas_adapter);
    }
}
```

## 6、UI下拉列表利用注册表迭代接口
不需要写死UI下拉选项，直接遍历注册表生成列表。
```c
// UI生成NAS类型下拉框
void ui_build_nas_type_dropdown(lv_obj_t *dd)
{
    lv_dropdown_clear_options(dd);
    int idx = 0;
    while(true)
    {
        const nas_adapter_reg_entry_t *e = nas_adapter_reg_iterate(idx);
        if(e == NULL) break;
        lv_dropdown_add_option(dd, e->display_name, e->type);
        idx++;
    }
}
```
> 新增NAS适配器，**UI下拉列表自动多出选项，UI代码完全不用改**。

## 7、进阶：GCC constructor自动注册（无需手动调用register）
ESP‑IDF GCC支持`__attribute__((constructor))`，模块在`app_main`之前自动执行注册，**不需要手动调用`nas_adapter_register_all()`**。

在每个适配器文件末尾替换注册函数：
```c
static void __attribute__((constructor)) nas_adapter_local_uart_auto_register(void)
{
    nas_adapter_reg_register(&s_local_uart_entry);
}
```
> ⚠️注意：constructor执行顺序不确定；**不能在constructor里做硬件、NVS、wifi操作，只做简单注册表填表**。
> 优点：新增适配器，只要编译进固件，自动注册，main完全不需要修改。
> 缺点：ESP‑IDF组件模式下，有些编译配置constructor会失效；量产项目优先用手动注册，稳定性更高。

## 8、注册表模式 vs switch‑case传统工厂对比

|项目|switch‑case工厂|注册表模式工厂|
|---|---|---|
|新增适配器|修改factory.c switch case，改动工厂代码|只新增适配器`.c`，工厂代码不动|
|UI下拉列表|UI硬编码全部选项|遍历注册表动态生成下拉|
|配置字符串解析|写大量if‑else字符串匹配|查表自动完成|
|代码耦合|工厂依赖全部适配器头文件|工厂只依赖注册表头；适配器自注册|
|裁剪固件|删除case，删除源文件|直接不编译该适配器`.c`，自动不在注册表出现|
|适合场景|适配器少（2‑3个）|适配器多、未来持续扩展（NAS场景非常合适）|

## 9、新增一个NAS适配器完整流程（注册表模式）
1. 枚举`nas_adapter_type_t`增加新类型ID；
2. 新建`nas_adapter_xxx.c`，实现`init/deinit/poll`；
3. 定义静态`nas_adapter_reg_entry_t`注册项；
4. 实现`nas_adapter_xxx_register()`（或constructor自动注册）；
5. 把`.c`加入编译；
> ✅**工厂代码、UI下拉列表、字符串解析全部不需要修改**。

## 10、关键避坑
1. 注册表只保存**函数指针、常量字符串**，不要存实例；实例是`factory_create`动态malloc出来。
2. `constructor`只做注册，**禁止访问硬件、NVS、wifi**，此时系统还没初始化。
3. 不要在注册表条目中存malloc动态内存，全部使用静态常量。
4. poll函数依旧保持非阻塞，适配器内部状态全部放在`priv`私有上下文，禁止全局变量。
5. 注册表数组大小预留余量，方便后续扩展更多NAS类型。

## 和整套架构数据流
```
app_main
 └─所有适配器注册进注册表（手动/constructor）
    └─读取NVS nas名字字符串
       └─nas_adapter_factory_lookup_type()查表得到type
          └─nas_adapter_factory_create(type)遍历注册表拿到构造函数，生成适配器实例
             └─adapter->init()
                └─任务轮询 poll → nas_state → event_bus → UI / PID风扇业务
```

如果你需要，我可以把这套注册表模式和前面boards硬件抽象、LVGL UI适配、事件总线串起来，给出整体架构总图。