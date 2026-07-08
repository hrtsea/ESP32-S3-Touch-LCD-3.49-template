# WebUI 与 esp_wifi_config 整合方案计划

## 一、现状分析

### 1.1 当前集成方式

项目已实现**共享 HTTP 服务器**的基础集成：

```
main.cpp:67-96 network_init()
  ├── wifi_cfg_init(&cfg)           # esp_wifi_config 创建 httpd
  ├── wifi_cfg_get_httpd()          # 获取 httpd 句柄
  ├── webui_start_with_httpd(srv)   # webui 注册路由到共享服务器
  └──  fallback: webui_start()      # 独立启动备用
```

- **位置**：[main.cpp:67-96](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/main.cpp#L67-L96)
- **模式**：WiFi 配网完成后保留 API（`WIFI_HTTP_API_ONLY`），WebUI 由项目 webui 组件提供

### 1.2 两个组件的职责边界

| 组件 | 职责 | 端点/功能 |
|------|------|-----------|
| **esp_wifi_config** | WiFi 配置、配网、网络管理 | `/api/wifi/*` (17个端点) + SoftAP + Captive Portal |
| **webui** | 设备控制面板（状态/配置/录音/截图） | 17个自定义端点 + 内嵌 HTML 单页应用 |

### 1.3 esp_wifi_config 的自定义页面能力

esp_wifi_config 提供三种 Web UI 模式（通过 Kconfig 配置）：

| 模式 | 配置 | 说明 |
|------|------|------|
| 内置 Preact UI | `WIFI_CFG_ENABLE_WEBUI=y`, `WEBUI_CUSTOM_PATH=""` | 自带配网前端，~10KB gzipped |
| 自定义文件系统 UI | `WIFI_CFG_ENABLE_WEBUI=y`, `WEBUI_CUSTOM_PATH="/littlefs"` | 从 LittleFS/SPIFFS 加载自定义页面 |
| 极简回退页 | `WIFI_CFG_ENABLE_WEBUI=n` | 内置最小 HTML，仅添加网络 |

**自定义页面限制**：仅支持 3 个固定 URL 路径
- `/` → `/index.html`
- `/assets/app.js`（或 `.js.gz`）
- `/assets/index.css`（或 `.css.gz`）

---

## 二、整合方案对比

### 方案 A：保持现状 + 优化（推荐）

**核心思路**：继续使用共享 HTTP 服务器模式，webui 提供主控制面板，esp_wifi_config 提供 WiFi API。

```
                ┌─────────────────────────┐
                │    Shared HTTPD :80     │
                └───────────┬─────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
  /api/wifi/*          /api/*                / (webui)
  (esp_wifi_config)   (webui业务API)       (内嵌HTML)
```

**需要做的优化**：
1. 配网期间将 webui 首页整合为统一入口
2. 利用 `pre_request_hook` 实现统一认证
3. Captive Portal 落地页指向 webui 首页而非默认 WiFi 配置页

**优点**：
- 改动最小，风险最低
- webui 功能完整保留
- 内存占用最小（单 httpd 实例）
- 前后端完全可控

**缺点**：
- 配网页和控制面板是两套前端
- 未利用 esp_wifi_config 的自定义页面机制

---

### 方案 B：统一前端（自定义页面 + API 扩展）

**核心思路**：使用 esp_wifi_config 的自定义文件系统 UI 机制，将 webui 前端打包为单页应用放到 LittleFS 中，后端 API 全部注册到共享 httpd。

```
                ┌─────────────────────────┐
                │    Shared HTTPD :80     │
                └───────────┬─────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
  /api/wifi/*          /api/*             / (统一前端)
  WiFi配置API       webui业务API       LittleFS中的SPA
                                     (含WiFi配置+设备控制)
```

**实施步骤**：
1. 引入 LittleFS 组件和分区
2. 将 webui 前端重构为独立的 SPA 项目（React/Vue/Preact）
3. 前端打包输出到 `www/` 目录
4. CMake 配置 `littlefs_create_partition_image`
5. sdkconfig 配置 `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH="/littlefs"`
6. webui 组件仅保留后端 API，移除内嵌 HTML

**优点**：
- 统一的前端体验（WiFi 配网 + 设备控制在一个 SPA 中）
- 前端独立迭代，无需重新编译固件
- 利用 esp_wifi_config 的 gzip 支持
- 更专业的前端开发体验

**缺点**：
- 改动大，需要前端重构
- 需要 LittleFS 分区（占用 Flash 空间 ~200-500KB）
- 开发环境复杂化（需要 Node.js/npm）
- 嵌入式资源加载方式改变，调试链路变长

---

### 方案 C：混合模式（配网用内置，运行用 webui）

**核心思路**：配网阶段使用 esp_wifi_config 内置的 Preact UI，配网完成后切换到 webui 控制面板。

- 配网阶段（SoftAP + Captive Portal）：用户看到 esp_wifi_config 内置的 WiFi 配置页
- 配网完成后：`http_post_prov_mode = WIFI_HTTP_API_ONLY`，内置 UI 卸载，webui 首页成为主入口

**实施要点**：
1. 配置 `http_post_prov_mode = WIFI_HTTP_API_ONLY`
2. webui 的 `GET /` 路由在配网结束后仍然可用
3. Captive Portal 重定向仍然指向 `/`，但配网期间由 esp_wifi_config 的页面响应
4. 需要处理路由优先级问题

**注意**：此方案与现状接近，区别在于配网期间用户看到的是 esp_wifi_config 的内置 UI 而非 webui。

---

## 三、推荐方案：方案 A（保持现状 + 优化）

### 3.1 理由

1. **项目现状良好**：共享 HTTP 服务器模式已经工作正常
2. **webui 功能丰富**：webui 有 17 个端点 + 完整的设备控制功能，与业务深度绑定
3. **改动成本可控**：只需做少量优化即可达到良好体验
4. **不引入新依赖**：不需要 LittleFS、不需要前端构建工具链

### 3.2 优化项

#### 优化 1：统一认证机制

利用 esp_wifi_config 的 `pre_request_hook` + webui 各自实现的方式，统一用 Basic Auth 保护敏感端点。

```c
// network_init 中配置
wifi_cfg_config_t cfg = {
    .http = {
        .api_base_path = "/api/wifi",
        .enable_auth = true,           // WiFi API 启用认证
        .auth_username = "admin",
        .auth_password = "admin",
        .pre_request_hook = webui_auth_hook,  // 复用同一hook
    },
};
```

**修改文件**：
- `components/webui/webui.c` — 添加 Basic Auth 中间件函数
- `main/main.cpp` — 配置认证参数

#### 优化 2：Captive Portal 落地页优化

当前 esp_wifi_config 的 Captive Portal 重定向到 `/`，正好由 webui 的首页响应。

**验证项**：确认配网期间 webui 的首页是否能正常访问。如果 `http_post_prov_mode = WIFI_HTTP_API_ONLY` 在配网期间也生效，则需要调整。

**方案**：将 `http_post_prov_mode` 改为 `WIFI_HTTP_FULL`，但用自定义页面路径覆盖？不，这会引入复杂度。

**实际情况**：配网期间 esp_wifi_config 会注册完整的 Web UI（包括 `/`），这可能与 webui 的 `/` 路由冲突。需要确认路由注册顺序和优先级。

#### 优化 3：webui 增加 WiFi 配置入口

在 webui 的内嵌 HTML 中，添加一个 WiFi 设置 Tab/页面，调用 esp_wifi_config 的 `/api/wifi/*` 端点。

**修改文件**：
- `components/webui/webui.c` — 在 HTML 字符串中添加 WiFi 配置界面的 JS/CSS/HTML

---

## 四、详细实施步骤（方案 A 优化版）

### Step 1：确认路由冲突和优先级

**目标**：确认 esp_wifi_config 的内置 UI 与 webui 的 `/` 路由是否冲突

**操作**：
1. 检查 `http_post_prov_mode` 当前值（`WIFI_HTTP_API_ONLY`）
2. 确认配网期间 esp_wifi_config 是否注册了 `/` 路由
3. 如果冲突，决定配网期间哪个页面优先

**修改文件**：无（调研阶段）

### Step 2：添加 Basic Auth 认证

**目标**：保护 webui 的配置端点，与 esp_wifi_config 认证统一

**操作**：
1. 在 webui.c 中添加 `check_basic_auth()` 辅助函数
2. 修改敏感端点 handler（`POST /api/cfg`, `POST /api/clock` 等）增加认证检查
3. 在 main.cpp 中配置 esp_wifi_config 使用相同的用户名密码

**修改文件**：
- `components/webui/webui.c` — 添加认证检查
- `components/webui/webui.h` — 可选：暴露 auth hook
- `main/main.cpp` — 配置 esp_wifi_config auth 参数

### Step 3：webui 前端增加 WiFi 设置

**目标**：在 webui 的控制面板中增加 WiFi 管理界面，统一用户体验

**操作**：
1. 在 webui.c 的内嵌 HTML 中添加 WiFi Tab
2. 实现网络列表、添加网络、删除网络、连接状态等 UI
3. 调用 esp_wifi_config 的 `/api/wifi/*` REST API
4. 复用 webui 现有的 JS 工具函数

**修改文件**：
- `components/webui/webui.c` — 添加 WiFi 配置前端代码（HTML/JS/CSS）

### Step 4：测试验证

**测试项**：
1. 配网模式下 Captive Portal 是否正常工作
2. WiFi 配置 API 是否正常
3. webui 所有原有功能是否正常
4. 认证是否生效
5. 内存占用是否可接受

---

## 五、潜在风险与应对

| 风险 | 影响 | 应对措施 |
|------|------|---------|
| 路由注册顺序导致 `/` 被 esp_wifi_config 覆盖 | 配网期间 webui 首页不可访问 | 调整注册顺序，或使用不同路径 |
| 配网期间内存紧张（两个前端+httpd） | OOM 崩溃 | 确认 `WIFI_HTTP_API_ONLY` 在配网期的行为 |
| Basic Auth 增加复杂性 | 开发调试不便 | 提供 Kconfig 开关，开发版可关闭 |
| 内嵌 HTML 代码过长，维护困难 | 可读性差 | 考虑拆分为多个静态变量 + 注释分段 |

---

## 六、Flash/RAM 影响评估

### 方案 A 优化版
- **Flash**：几乎无变化（增加少量认证代码）
- **RAM**：几乎无变化（共享 httpd 已实现）

### 方案 B 统一前端
- **Flash**：增加 LittleFS 分区（约 200-500KB）
- **RAM**：减少（内嵌 HTML 释放，但 LittleFS 缓存增加）
- **开发成本**：显著增加

---

## 七、总结建议

**短期（当前迭代）**：采用方案 A 优化版
- 保持现有架构稳定
- 增加统一认证
- 在 webui 中添加 WiFi 设置界面
- 风险低，收益明确

**长期（未来迭代）**：评估方案 B 的必要性
- 如果前端交互越来越复杂
- 如果需要频繁迭代前端而不更新固件
- 如果团队有前端开发资源
- 再考虑迁移到 LittleFS + 独立前端项目的架构
