#include "nas_event_loop.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "event_bus.h"
#include "data_source.h"

/* =====================================================================
 * NAS 事件循环（nas_event_loop）
 * =====================================================================
 * 职责：
 *   1) 周期性触发数据源抓数（esp_timer 定时器 → 事件总线 → 抓数任务）
 *   2) 抓到的 NasData 通过事件总线发布，供 UI 订阅刷新
 *
 * 抓取开关策略：
 *   - 本模块不维护任何"是否可抓数"状态，"能否抓数"完全由数据源自维护
 *     （data_source_is_connected() → client 的 is_online）：
 *       · 真实 NAS：依赖 WiFi，连接失败/离线时 is_online=false，
 *         内部带失败退避与自动重连，WiFi 恢复后即可重新抓数；
 *       · mock：is_connected() 恒 true，始终可抓数。
 *   - 因此任务内无需监听 WiFi 事件，每 2s 的抓数指令被
 *     data_source_fetch_and_publish() 内的连接门卫自然过滤。
 *
 * 架构（单驱动 + 单消费）：
 *   - 生产者：esp_timer 周期回调只发 EVENT_TRIGGER_HTTP_FETCH，不做网络操作，
 *     避免网络阻塞 esp_timer 系统任务；
 *   - 消费者：task_nas_data_loop 任务阻塞在 event_bus_receive，串行处理所有事件。
 *
 * 并发模型：
 *   - 数据源访问互斥由 data_source 模块内部的递归锁（data_source_lock/unlock）
 *     统一保护，本模块任务在"抓数+发布"序列外持锁，与 UI 设置页的
 *     data_source_switch 互斥，保证返回指针不被并发销毁；
 *   - 任务采用超时接收 + 退出信号量的优雅退出协议，stop 不直接强删任务，
 *     避免任务持锁或网络抓数进行中被强制终止。
 * ===================================================================== */

static const char *TAG = "nas_event_loop";

/* ---------------- 模块级状态（全部仅由本模块访问） ---------------- */
static TaskHandle_t s_nas_task_hdl = NULL;   /* 抓数任务句柄，stop 时用于等待其退出 */
static bool s_running = false;               /* 事件循环总开关：start 置位 / stop 清零 */
static SemaphoreHandle_t s_task_exit_sem = NULL;    /* 任务退出信号量：任务自删前 Give，stop 等待 */

/* NAS 拉取周期定时器（原独立 http_timer 模块，已并入本模块）。
 * 回调仅发布 EVENT_TRIGGER_HTTP_FETCH，实际抓数由 task_nas_data_loop 执行，
 * 避免网络阻塞 esp_timer 任务。随 nas_event_loop_start() 一并创建并启动。 */
#define FETCH_INTERVAL_MS 2000u

/* 任务事件接收超时：stop 置 s_running=false 后，任务最迟一个超时周期内自行退出 */
#define EVENT_LOOP_POLL_MS 500u
/* 等待任务退出的上限（须覆盖最坏网络 poll 耗时，http 超时 3s + 余量） */
#define TASK_EXIT_WAIT_MS 4000u

static esp_timer_handle_t s_fetch_timer = NULL;   /* 2s 抓数周期定时器句柄 */

/* 上一次抓数时的数据源连接状态：仅在翻转时打日志（连接建立/断开），
 * 避免每 2s 的抓数周期反复刷屏。初值 false：若首次抓数即已连接，
 * 会打一条"established"日志，语义正确且不误报。 */
static bool s_prev_connected = false;

/* ---------------- 定时器回调 ---------------- */

/* 周期定时器回调：只负责"发指令"，不执行任何抓数逻辑。
 * 网络操作可能阻塞数秒，若直接放在 esp_timer 回调中会卡死整个
 * esp_timer 系统任务（其它定时器全部停摆），因此仅入队事件，
 * 由独立任务 task_nas_data_loop 串行执行。 */
static void fetch_timer_cb(void *arg)
{
    (void)arg;
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

/* 抓数并发布：由任务在持锁状态下调用（保证与数据源切换互斥）。
 * 执行链：data_source_poll() 从 NAS 拉取 → 取回 NasData →
 *         若在线则 event_bus_publish_nas_data() 广播给 UI 各订阅者刷新。
 * 返回 true 表示本次抓数成功且数据已发布。
 *
 * 抓数门卫（连接判断下沉到数据源）：
 *   data_source_is_connected() 直接反映数据源自身的连接状态——
 *   真实 NAS 离线/未连 WiFi 时返回 false，此处直接跳过，不产生无效网络请求；
 *   mock 数据源恒返回 true，始终可抓数。无需事件循环维护独立的抓取开关。
 *
 * 日志设计（排查连接问题）：
 *   - 连接状态跳变（断开 WARN / 建立 INFO）：仅在翻转时输出，标记根因时机；
 *   - 未连接跳过 / poll 节流：DEBUG 级别，每 2s 周期不刷屏；
 *   - 抓数成功：INFO 附带 cpu/mem/盘数，便于核对数据是否在更新。 */
static bool data_source_fetch_and_publish(void)
{
    bool connected = data_source_is_connected();

    /* 连接状态跳变日志：只在翻转时输出，用于定位"何时断开/何时恢复" */
    if (connected != s_prev_connected) {
        if (connected) {
            ESP_LOGI(TAG, "Data source connection established (type=%s)",
                     data_source_get_type_name());
        } else {
            ESP_LOGW(TAG, "Data source connection lost (type=%s)",
                     data_source_get_type_name());
        }
        s_prev_connected = connected;
    }

    /* 不再在未连接时跳过 poll：未连接也允许调用 poll()，由各 client 的 poll()
     * 内部重连逻辑自行恢复连接（连接自愈内聚在 client，而非事件循环）。
     * 事件循环的"连接门卫"职责上移——仅在不在线时不发布数据。 */
    if (data_source_poll()) {
        const NasData *data = data_source_get_data();
        if (data && data->is_online) {
            ESP_LOGI(TAG, "Fetched data (type=%s, cpu=%.1f%%, mem=%.1f%%, disks=%u)",
                     data_source_get_type_name(),
                     data->system.cpu_pct, data->system.ram_pct, data->disk_count);
            event_bus_publish_nas_data(data);
            return true;
        }
        /* poll 成功但数据未标记在线：异常路径，值得注意 */
        ESP_LOGW(TAG, "Poll ok but data not marked online (type=%s)",
                 data_source_get_type_name());
        return false;
    }

    /* poll 返回 false 多为未到轮询间隔（节流）；真实失败由 client 内部日志说明 */
    ESP_LOGD(TAG, "Poll throttled or failed (type=%s)", data_source_get_type_name());
    return false;
}

/* 抓数任务主体：本模块唯一的消费者。
 *
 * 事件处理策略：
 *   仅响应抓数指令 EVENT_TRIGGER_HTTP_FETCH，直接调用抓数（是否真抓由
 *   数据源内部连接状态过滤）；不再监听 WiFi 事件——连接状态由数据源自维护，
 *   WiFi 恢复后数据源 poll 自动重连，无需事件循环干预。
 *
 * 退出协议（优雅退出）：
 *   - receive 带 500ms 超时，超时后回到 while 重新检查 s_running；
 *   - stop 置 s_running=false 后，任务最迟一个超时周期内感知并自行退出；
 *   - 退出前先 Give s_task_exit_sem，让 stop 的等待方确认"任务已退出"，
 *     从而避免 stop 在任务持锁/抓数中途强删（vTaskDelete 会掐断正在进行的网络请求）。 */
static void task_nas_data_loop(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "NAS data loop task started (event-driven)");

    while (s_running) {
        event_t evt;
        /* 带超时接收：stop 置 s_running=false 后，任务最迟一个超时周期内自行退出，
         * 避免被外部强删时正处于网络抓数中。 */
        if (!event_bus_receive(&evt, pdMS_TO_TICKS(EVENT_LOOP_POLL_MS))) {
            continue; /* 超时无事件：回到 while 条件，检查是否被请求退出 */
        }

        /* "抓数+发布"整体持数据源锁：与 UI 设置页的 data_source_switch 互斥，
         * 并保证 get_data 返回的指针在发布完成前不被并发销毁。
         * 锁为递归锁，内部 is_connected/poll/get_data 各自持锁可安全重入。 */
        data_source_lock();

        if (evt.id == EVENT_TRIGGER_HTTP_FETCH) {
            data_source_fetch_and_publish();
        }
        /* 其它事件与本模块无关，一律忽略 */

        data_source_unlock();
    }

    /* ---- 优雅退出路径：此处的必然前提是已 Give 锁，无锁残留 ---- */
    ESP_LOGI(TAG, "NAS data loop task exiting");
    /* 通知 stop 等待者任务已退出（无锁残留，可安全清理资源） */
    if (s_task_exit_sem != NULL) {
        xSemaphoreGive(s_task_exit_sem);
    }
    vTaskDelete(NULL); /* 自行回收自身 TCB/栈，由空闲任务完成 */
}

/* 启动事件循环：由 main 初始化流程调用一次。
 *
 * 初始化顺序（有严格依赖，不可随意调换）：
 *   1. 创建任务退出信号量（失败即回滚 s_running，允许重试）；
 *   2. 根据配置创建并初始化数据源（失败仅告警，任务照常启动；
 *      是否可抓数由数据源内部连接状态决定，connect 失败会在 poll 中自动重试）；
 *   3. 创建抓数任务（失败则回滚已建资源）；
 *   4. 创建并启动 2s 周期定时器（失败则抓数停摆，但不影响事件循环本体）。
 *   注：数据源访问锁由 data_source 模块内部懒创建，本模块不参与。 */
void nas_event_loop_start(const char* type_id)
{
    if (s_running) {
        ESP_LOGW(TAG, "NAS event loop already running");
        return;
    }

    s_running = true;

    /* 1. 任务退出信号量：优雅退出协议的核心同步原语 */
    s_task_exit_sem = xSemaphoreCreateBinary();
    if (s_task_exit_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create task exit semaphore");
        s_running = false; /* 回滚状态，允许重试 */
        return;
    }

    /* 2. 数据源：type_id 由调用方（main）从配置解析后传入，事件循环不耦合 app_cfg。
     * 连接自愈内聚在各 client 的 poll() 中，此处不显式 connect。 */
    ESP_LOGI(TAG, "Creating data source for type: %s", type_id ? type_id : "(null)");
    if (!data_source_set_type(type_id)) {
        ESP_LOGE(TAG, "Failed to create data source");
        /* 创建失败（协议不支持/内存不足）致命：数据源不可用，后续 poll 恒跳过 */
    }

    /* 3. 抓数任务：失败则清理已建资源并回滚，保证可再次 start */
    if (xTaskCreate(task_nas_data_loop, "nas_data_loop", 8192, NULL, 1, &s_nas_task_hdl) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create nas data loop task");
        vSemaphoreDelete(s_task_exit_sem);
        s_task_exit_sem = NULL;
        s_running = false;
        return;
    }

    /* 4. 周期定时器：回调仅发布 EVENT_TRIGGER_HTTP_FETCH，随事件循环一并创建并启动 */
    if (s_fetch_timer != NULL) {
        /* stop 正常会删除定时器，走到这里说明上次 stop 有遗漏，防御性处理 */
        ESP_LOGW(TAG, "Fetch timer already exists (unexpected), reusing");
    } else {
        esp_timer_create_args_t args = {
            .callback = fetch_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,   /* 回调在 esp_timer 专用任务中执行 */
            .name = "nas_fetch_timer",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_fetch_timer) == ESP_OK &&
            esp_timer_start_periodic(s_fetch_timer, (uint64_t)FETCH_INTERVAL_MS * 1000) == ESP_OK) {
            ESP_LOGI(TAG, "Fetch timer started (interval=%ums)", FETCH_INTERVAL_MS);
        } else {
            ESP_LOGE(TAG, "Failed to create/start fetch timer");
        }
    }

    ESP_LOGI(TAG, "NAS event loop started");
}

/* 停止事件循环：与 start 严格对称的逆序清理。
 *
 * 步骤（每步都前置清理依赖，避免资源泄漏 / 并发破坏）：
 *   1. 停并删除周期定时器 —— 先切断"抓数指令"事件源，防止 stop 后事件继续
 *      积压进 event_bus 队列（队列满会连带挤掉 NAS_DATA_UPDATE / WiFi 事件）；
 *   2. 置 s_running=false 并等待任务优雅退出 —— 任务自删前 Give 退出信号量，
 *      本函数阻塞等待至多 TASK_EXIT_WAIT_MS；超时才强删兜底（极端网络阻塞场景）；
 *   3. 断开数据源 —— 此时任务已退出，不会与 poll 并发访问数据源；
 *   4. 删除退出信号量。 */
void nas_event_loop_stop(void)
{
    if (!s_running) {
        return; /* 幂等：未启动或已停止则直接返回 */
    }

    /* 1. 先停拉取定时器，切断事件源（防无效事件积压队列 + 句柄泄漏） */
    if (s_fetch_timer != NULL) {
        esp_timer_stop(s_fetch_timer);
        esp_timer_delete(s_fetch_timer);
        s_fetch_timer = NULL;
    }

    /* 2. 请求任务优雅退出并等待其自行清理 */
    s_running = false;
    if (s_nas_task_hdl != NULL) {
        /* 任务退出时 Give 信号量；正常情况下 500ms 内即退出，无需等满 4s */
        bool exited = (s_task_exit_sem != NULL) &&
                      (xSemaphoreTake(s_task_exit_sem, pdMS_TO_TICKS(TASK_EXIT_WAIT_MS)) == pdTRUE);
        if (!exited) {
            /* 兜底：任务长时间未退出（极端网络阻塞），强删（此时可能持锁，删锁置于其后） */
            ESP_LOGW(TAG, "NAS data loop task did not exit in %ums, force deleting", TASK_EXIT_WAIT_MS);
            vTaskDelete(s_nas_task_hdl);
        }
        s_nas_task_hdl = NULL;
    }

    /* 3. 断开数据源（任务已退出，无并发抓数） */
    data_source_disconnect();

    /* 4. 清理同步原语 */
    if (s_task_exit_sem != NULL) {
        vSemaphoreDelete(s_task_exit_sem);
        s_task_exit_sem = NULL;
    }

    ESP_LOGI(TAG, "NAS event loop stopped");
}

/* 查询事件循环是否在运行（幂等查询，供外部模块判断可用性） */
bool nas_event_loop_is_running(void)
{
    return s_running;
}
