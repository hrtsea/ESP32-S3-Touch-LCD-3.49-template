# NAS适配器：工厂模式实现多NAS后端（群晖 / QNAP / Unraid / 串口本地模拟）
## 需求梳理
- 后端类型：`Synology(群晖)`、`QNAP`、`Unraid`、`LocalUart(串口本地调试)`
- 统一输出：硬盘状态、温度、风扇转速、CPU负载、告警状态，**上层业务完全不感知具体NAS类型**
- 可运行时切换NAS类型（NVS保存配置）
- 新增NAS协议，只需要新增适配器文件，不修改上层业务代码，符合开闭原则
- 和前面整套架构配合：事件总线、UI、监控屏，硬件层(`boards`)与NAS业务完全解耦

> 核心：**工厂模式 + 面向接口抽象**
1. 定义抽象接口基类（函数指针结构体），所有NAS适配器实现这套接口；
2. 每个NAS后端独立实现一套`.c`，不互相污染；
3. 工厂函数根据配置类型，返回对应适配器实例；
4. 上层业务只调用抽象接口，不直接调用具体实现函数。

## 目录组织（放到项目`main/nas_adapter/`）
```
main/nas_adapter/
├── nas_adapter.h               # ✅抽象接口定义，适配器对象结构体，枚举
├── nas_adapter_factory.c       # ✅工厂：根据类型创建对应适配器实例
├── nas_adapter_synology.c      # 群晖实现
├── nas_adapter_qnap.c          # QNAP实现
├── nas_adapter_unraid.c        # Unraid实现
├── nas_adapter_local_uart.c    # 串口本地模拟调试（无真实NAS，串口模拟上报数据）
└── nas_adapter_private.h       # 各个适配器内部私有声明（不对外暴露）
```

## 1、nas_adapter.h 公共抽象头（上层唯一include）
> 定义统一数据模型 + 抽象接口，上层业务只依赖这个头，不include各个具体适配器文件。
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// NAS设备类型枚举
typedef enum {
    NAS_ADAPTER_TYPE_NONE = 0,
    NAS_ADAPTER_TYPE_SYNOLOGY,
    NAS_ADAPTER_TYPE_QNAP,
    NAS_ADAPTER_TYPE_UNRAID,
    NAS_ADAPTER_TYPE_LOCAL_UART,   // 串口本地模拟调试
} nas_adapter_type_t;

// 单块硬盘信息
typedef struct {
    uint8_t slot;           // 盘位号
    bool present;           // 是否在位
    bool fault;             // 故障告警
    int16_t temp_c;         // 温度 ℃
    uint64_t capacity_gb;   // 容量GB
} nas_hdd_info_t;

// NAS全局状态
typedef struct {
    float cpu_load;         // CPU负载 0~100
    int16_t cpu_temp;
    uint16_t fan_speed_rpm;
    uint8_t hdd_count;
    nas_hdd_info_t hdd_list[8]; // 最多8盘位
    bool alert;              // 全局告警
} nas_state_t;

// ========== 抽象适配器接口（函数指针表，工厂模式核心） ==========
typedef struct nas_adapter_s nas_adapter_t;

struct nas_adapter_s {
    // 适配器类型
    nas_adapter_type_t type;

    // 初始化：建立连接 http/smbus/uart
    int (*init)(nas_adapter_t *self);

    // 反初始化，断开连接
    void (*deinit)(nas_adapter_t *self);

    // 轮询更新数据，非阻塞；返回0成功
    int (*poll)(nas_adapter_t *self, nas_state_t *out_state);

    // 获取适配器可读名字
    const char* (*get_name)(nas_adapter_t *self);

    // 私有上下文，每个适配器自己存私有参数（http句柄、uart句柄、会话token等）
    void *priv;
};

// ========== 工厂对外API ==========
/**
 * @brief 根据类型创建NAS适配器实例
 * @param type 适配器类型
 * @return 适配器指针，NULL失败
 */
nas_adapter_t* nas_adapter_factory_create(nas_adapter_type_t type);

/**
 * @brief 销毁适配器
 */
void nas_adapter_factory_destroy(nas_adapter_t *adapter);

/**
 * @brief 将字符串转为枚举类型（用于NVS配置读取）
 */
nas_adapter_type_t nas_adapter_str_to_type(const char *str);

/**
 * @brief 枚举转字符串，用于UI显示
 */
const char* nas_adapter_type_to_str(nas_adapter_type_t t);
```

> 上层业务使用示例，完全看不到synology/qnap实现细节
```c
// business/hdd_monitor.c
#include "nas_adapter/nas_adapter.h"

static nas_adapter_t *g_nas_adapter = NULL;
static nas_state_t g_nas_state;

// 从NVS读出配置类型
void nas_adapter_init(void)
{
    nas_adapter_type_t cfg_type = nvs_get_nas_type();
    g_nas_adapter = nas_adapter_factory_create(cfg_type);
    if(g_nas_adapter == NULL) {
        // 降级到LOCAL_UART调试
        g_nas_adapter = nas_adapter_factory_create(NAS_ADAPTER_TYPE_LOCAL_UART);
    }
    g_nas_adapter->init(g_nas_adapter);
}

// 任务轮询，1s调用一次
void nas_adapter_poll_task(void)
{
    if(g_nas_adapter == NULL) return;
    int ret = g_nas_adapter->poll(g_nas_adapter, &g_nas_state);
    if(ret == 0) {
        // 拿到数据，投递事件总线，UI、PID业务接收事件
        event_bus_post(EVENT_NAS_DATA_UPDATE, &g_nas_state, sizeof(nas_state_t));
    }
}
```

## 2、nas_adapter_private.h（适配器内部私有，上层禁止include）
每个具体适配器实现的构造函数声明，只给factory使用。
```c
#pragma once
#include "nas_adapter.h"

// 各个适配器构造函数，工厂内部调用
nas_adapter_t *nas_adapter_synology_create(void);
nas_adapter_t *nas_adapter_qnap_create(void);
nas_adapter_t *nas_adapter_unraid_create(void);
nas_adapter_t *nas_adapter_local_uart_create(void);
```

## 3、nas_adapter_factory.c 工厂实现
> 根据type调用对应create函数，返回适配器对象；销毁统一释放priv内存。
```c
#include "nas_adapter.h"
#include "nas_adapter_private.h"
#include <string.h>
#include <stdlib.h>

nas_adapter_t* nas_adapter_factory_create(nas_adapter_type_t type)
{
    nas_adapter_t *adapter = NULL;
    switch(type)
    {
        case NAS_ADAPTER_TYPE_SYNOLOGY:
            adapter = nas_adapter_synology_create();
            break;
        case NAS_ADAPTER_TYPE_QNAP:
            adapter = nas_adapter_qnap_create();
            break;
        case NAS_ADAPTER_TYPE_UNRAID:
            adapter = nas_adapter_unraid_create();
            break;
        case NAS_ADAPTER_TYPE_LOCAL_UART:
            adapter = nas_adapter_local_uart_create();
            break;
        default:
            return NULL;
    }
    return adapter;
}

void nas_adapter_factory_destroy(nas_adapter_t *adapter)
{
    if(!adapter) return;
    if(adapter->deinit) {
        adapter->deinit(adapter);
    }
    // 释放私有上下文
    if(adapter->priv) {
        free(adapter->priv);
    }
    free(adapter);
}

const char* nas_adapter_type_to_str(nas_adapter_type_t t)
{
    switch(t)
    {
        case NAS_ADAPTER_TYPE_SYNOLOGY: return "synology";
        case NAS_ADAPTER_TYPE_QNAP:     return "qnap";
        case NAS_ADAPTER_TYPE_UNRAID:   return "unraid";
        case NAS_ADAPTER_TYPE_LOCAL_UART: return "local_uart";
        default: return "none";
    }
}

nas_adapter_type_t nas_adapter_str_to_type(const char *str)
{
    if(str == NULL) return NAS_ADAPTER_TYPE_NONE;
    if(strcmp(str,"synology")==0) return NAS_ADAPTER_TYPE_SYNOLOGY;
    if(strcmp(str,"qnap")==0)     return NAS_ADAPTER_TYPE_QNAP;
    if(strcmp(str,"unraid")==0)   return NAS_ADAPTER_TYPE_UNRAID;
    if(strcmp(str,"local_uart")==0) return NAS_ADAPTER_TYPE_LOCAL_UART;
    return NAS_ADAPTER_TYPE_NONE;
}
```

## 4、其中一个适配器实现示例：nas_adapter_local_uart.c（串口模拟）
> 每个适配器独立`.c`，内部维护`priv`私有结构体，实现全部接口。
```c
#include "nas_adapter.h"
#include "nas_adapter_private.h"
#include <stdlib.h>
#include <string.h>

// 私有上下文，此适配器自己的状态，对外完全隐藏
typedef struct {
    uint32_t tick;
} local_uart_priv_t;

static int local_uart_init(nas_adapter_t *self)
{
    local_uart_priv_t *priv = self->priv;
    priv->tick = 0;
    return 0;
}

static void local_uart_deinit(nas_adapter_t *self)
{
    (void)self;
}

static int local_uart_poll(nas_adapter_t *self, nas_state_t *out_state)
{
    local_uart_priv_t *priv = self->priv;
    priv->tick++;

    memset(out_state,0,sizeof(nas_state_t));
    // 模拟测试数据
    out_state->cpu_load = 20.0f + (priv->tick % 30);
    out_state->cpu_temp = 42;
    out_state->fan_speed_rpm = 1200;
    out_state->hdd_count = 4;
    for(int i=0;i<4;i++){
        out_state->hdd_list[i].slot = i;
        out_state->hdd_list[i].present = true;
        out_state->hdd_list[i].temp_c = 35 + i;
        out_state->hdd_list[i].fault = false;
    }
    out_state->alert = false;
    return 0;
}

static const char* local_uart_get_name(nas_adapter_t *self)
{
    (void)self;
    return "Local Uart Sim";
}

// 构造函数：分配适配器对象+私有priv，填充函数指针表
nas_adapter_t *nas_adapter_local_uart_create(void)
{
    nas_adapter_t *adapter = (nas_adapter_t*)malloc(sizeof(nas_adapter_t));
    local_uart_priv_t *priv = (local_uart_priv_t*)malloc(sizeof(local_uart_priv_t));
    if(!adapter || !priv) {
        free(adapter);
        free(priv);
        return NULL;
    }
    memset(adapter,0,sizeof(nas_adapter_t));
    memset(priv,0,sizeof(local_uart_priv_t));

    adapter->type = NAS_ADAPTER_TYPE_LOCAL_UART;
    adapter->priv = priv;

    // 绑定接口实现
    adapter->init = local_uart_init;
    adapter->deinit = local_uart_deinit;
    adapter->poll = local_uart_poll;
    adapter->get_name = local_uart_get_name;
    return adapter;
}
```

> `nas_adapter_synology.c` / `qnap` / `unraid` 模板完全一样：
> 1. 定义自己的`xxx_priv_t`保存会话、http句柄、token、IP地址；
> 2. 实现`init`做登录、http会话初始化；
> 3. `poll`非阻塞http请求，解析返回json，填充`nas_state_t`；
> 4. 实现构造函数，在factory注册。

## 5、运行时切换NAS类型（UI设置页面切换）
> 用户UI选择NAS类型，保存NVS，销毁旧适配器，新建对应适配器。
```c
// 用户UI修改配置回调
void ui_on_nas_type_change(nas_adapter_type_t new_type)
{
    // 销毁旧实例
    nas_adapter_factory_destroy(g_nas_adapter);
    // 创建新实例
    g_nas_adapter = nas_adapter_factory_create(new_type);
    if(g_nas_adapter) {
        g_nas_adapter->init(g_nas_adapter);
    }
    // 保存到NVS持久化
    nvs_set_nas_type(new_type);
}
```

## 6、与整套系统数据流打通
```
UI设置(NAS类型选择)
    ↓ 保存NVS
main启动读取NVS nas_type
    ↓ nas_adapter_factory_create() 创建对应适配器实例
    ↓ adapter->init() 初始化连接
FreeRTOS任务循环调用 adapter->poll()
    ↓ 填充nas_state_t
    ↓ event_bus_post(EVENT_NAS_DATA_UPDATE)
        ├─ ui/lvgl_ui：接收事件，刷新屏幕硬盘、温度、转速
        └─ business/fan_pid：接收事件，做风扇PID温控
```

## 7、扩展新增NAS设备步骤（开闭原则）
1. 头文件`nas_adapter.h`枚举增加新类型；
2. 新建`nas_adapter_xxx.c`，复制模板，实现全部接口；
3. 在`nas_adapter_private.h`增加构造函数声明；
4. 在`nas_adapter_factory.c`switch增加case；
5. 实现`init/deinit/poll/get_name`；
6. 修改`nas_adapter_type_to_str / nas_adapter_str_to_type`字符串转换；
> **上层业务代码完全不需要改动。**

## 8、关键约束与避坑
1. **所有适配器输出统一`nas_state_t`结构体，字段对齐；不同NAS差异数据做兼容填充（没有的填0/false）。**
2. `poll`函数**必须非阻塞**，不能阻塞等待http；http请求要异步，ESP-IDF用`esp_http_client`非阻塞模式，防止卡死整个监控任务。
3. 私有数据全部放在`adapter->priv`，**不要使用全局变量**，支持运行时销毁重建。
4. 上层业务永远不直接调用`nas_adapter_synology_xxx()`这类具体实现函数，全部通过虚函数指针表调用。
5. 串口本地模拟适配器非常重要，**没有真实NAS硬件也可以完整调试UI、风扇PID逻辑**。
6. NVS存储字符串类型，方便UI下拉选择、持久化配置。

## 9、CMake编译
所有适配器`.c`全部编译进固件，运行时工厂选择实例，不需要条件编译；
> 如果你希望固件裁剪（不需要qnap就不编译qnap代码），可以加编译开关，但是推荐全部编译，方便UI切换调试。

如果你需要，我可以给出：
1. Unraid http适配器极简骨架（esp_http_client非阻塞poll示例）
2. 事件总线接收NAS数据，UI刷新的完整片段
3. NVS读写NAS类型配置的示例代码。