# ZettOS 本地 API 规格说明

> **文档用途**：作为在 ESP32 上用 C 实现 `zettlab_client`（接入 Zettlab AI-NAS）的权威参考依据。
>
> **数据来源**：反向工程自 Home Assistant 自定义集成仓库 `Xianleewu/zettlab-ainas-iot`（v0.1.1，MIT 许可）的 Python 源码 + 仓库根目录 `CLAUDE.md` API 契约文档。本规格已交叉验证：`api.py` 路径常量 ↔ `coordinator.py` 调用 ↔ `sensor.py` 字段访问 ↔ `CLAUDE.md` 契约表。
>
> **验证机型**：Zettlab D4（ZettOS 1.9.0-beta）、Zettlab D6 Ultra（ZettOS 1.9.1-beta）。集成与型号无关，读取 model/serial/firmware 后使用通用 ZettOS API。
>
> **未在源码中发现的信息**已明确标注为"未发现/未解码"，无臆测。

---

## 1. 仓库源码结构概览

仓库是一个 Home Assistant 自定义集成（Python），目录 `custom_components/zettlab_ainas/`。`manifest.json` 标注 `version: 0.1.1`、`iot_class: local_polling`、`requirements: []`（无外部运行时依赖，`cryptography` 由 HA 提供）。

| 文件 | 职责 |
|------|------|
| `manifest.json` | 集成元数据，domain=`zettlab_ainas`，version=0.1.1，无外部依赖 |
| `const.py` | 常量：CONF_HOST/USERNAME/PASSWORD/VERIFY_SSL/SCAN_INTERVAL；`DEFAULT_SCAN_INTERVAL=30`、`MIN_SCAN_INTERVAL=10`、`DEFAULT_VERIFY_SSL=False`；`DISCOVERY_UDP_PORT=9527`、`DISCOVERY_PROBE=b""`（未捕获） |
| `__init__.py` | 集成入口：创建 `ZettOSClient` + `ZettlabAinasCoordinator`，转发 BINARY_SENSOR/LIGHT/SELECT/SENSOR/SWITCH 平台 |
| `api.py` | **核心**：ZettOS HTTP 客户端（transport-only，不导入 homeassistant）。定义全部端点路径、RSA 登录、请求封装、自动重登录 |
| `coordinator.py` | `DataUpdateCoordinator`：单次轮询调用核心 3 端点 + best-effort 3 端点，组装 `ZettlabData` 快照 |
| `config_flow.py` | 配置流程：discover/manual → login（RSA 登录验证）+ reauth + options（扫描间隔） |
| `entity.py` | 实体基类：`DeviceInfo` 从 `/device` 构建（sn/model_name/device_name/system_version/mac_address1/2） |
| `sensor.py` | 传感器：CPU 使用率/温度、内存使用率/已用、NPU/GPU、uptime；每池 usage/used/total；每盘温度 |
| `binary_sensor.py` | 存储池 problem 状态（`status != 0`） |
| `light.py` | RGB 状态灯：`start_color` 解析为 RGB，写时保留 mode/speed |
| `select.py` | 风扇模式：选项 "0"–"3"（原始整数，枚举未解码） |
| `switch.py` | 屏幕开关：`lcd.status==1` 读；`POST /lcd {enable}` 写 |
| `discovery.py` | UDP 9527 广播发现（因 `DISCOVERY_PROBE` 为空，**当前不工作**，回退手动 IP） |
| `strings.json` | UI 翻译（实体名称） |
| `CLAUDE.md`（根目录） | **API 契约设计文档**，反向工程自真机，含完整端点表与已知限制 |

---

## 2. 认证流程

源自 `api.py` 的 `ZettOSClient` 类 + `CLAUDE.md` "ZettOS HTTP API contract"。ZettOS 是 RK3588 上的微服务 NAS，单一 Go 网关 `zettos-gateway` 绑定 `:80`/`:443`（自签名 TLS），反向代理后端服务。集成只走网关 HTTPS API。

### 2.1 总体特征

- **Base URL**：`https://<host>`（自签名证书，客户端必须跳过 TLS 校验或钉选设备证书；`DEFAULT_VERIFY_SSL=False`）
- **统一响应信封**：`{"code": 200, "data": ...}`，`code != 200` 即错误。已知错误码：`10020`（登录失败）、`14502`（SMART 未就绪）
- **认证方式**：JWT（ES256）放在 `Authorization` 头，**值为裸 token，不带 `Bearer ` 前缀**（加前缀会 401）。`Token: <token>` 也有效
- **token 有效期**：约 3 小时（`data.token.expires_at`）
- **无登出端点**：源码中未发现
- **自动重登录**：`api.py` 的 `_async_request` 在收到 401/403（`ZettosAuthError`）时，加锁重新 `async_login()` 后重试一次

### 2.2 登录三步走

**步骤 1 — 获取 RSA 公钥**

- 端点：`GET /zettos/main/user/v1/public_key`（无需认证）
- 响应：`data.public_key`（RSA PEM 字符串）、`data.version`
- 代码：`api.py` → `_async_get_public_key()`；路径常量 `_P_PUBLIC_KEY`

**步骤 2 — RSA-PKCS1v15 加密密码**

- 算法：RSA-PKCS1 v1.5 公钥加密密码明文，输出 base64
- 代码：`api.py` → `_encrypt_password(public_key_pem, password)`
  - `load_pem_public_key(pem.encode())` 加载 PEM
  - `public_key.encrypt(password.encode(), padding.PKCS1v15())`
  - `base64.b64encode(...).decode()`
- 注释说明这是 RSA-2048 公钥操作，很快，无需 offload 到 executor
- `CLAUDE.md` 第 39 行确认："RSA-**PKCS1 v1.5**-encrypt the password with that key, base64 it"

**步骤 3 — 登录换取 JWT**

- 端点：`POST /zettos/main/user/v1/login`
- 请求体：`{"username": "admin", "password": "<base64密文>"}`
- 响应：`data.token.access_token`（JWT ES256）、`data.token.expires_at`（约 3h）
- 代码：`api.py` → `async_login()`；路径常量 `_P_LOGIN`
- 校验：若无 `access_token` 抛 `ZettosAuthError("login response did not contain an access token")`

### 2.3 后续请求携带认证

- `api.py` → `_async_raw_request(method, path, *, json, authed=True)`
- 当 `authed=True` 且 `self._token` 为 None 时抛 `ZettosAuthError("not logged in")`
- 设置 `headers["Authorization"] = self._token`（**裸 token，无 Bearer 前缀**，代码注释明确："ZettOS expects the bare token (NO \"Bearer \" prefix)")
- 401/403 → `ZettosAuthError`，触发上层重登录

### 2.4 配置流程中的登录验证

- `config_flow.py` → `async_step_login`：构造临时 `ZettOSClient`，先 `async_login()` 再 `async_get_device()`，成功后以 `device.sn` 为 unique_id 创建配置项
- `async_step_reauth_confirm`：密码变更后重新登录并更新凭据

---

## 3. API 端点完整清单

> **前缀说明**：`/zettos/main/...` 表示在 `/zettos/main` 下；`/zettos/monitor/...` 注意**不在** `/main` 下。所有响应均为 `{"code":200,"data":...}` 信封，下表"响应字段"列仅描述 `data` 部分。

### 3.1 认证与发现

| 用途 | 方法 | 路径 | 请求参数 | 响应（data）字段 |
|------|------|------|----------|------------------|
| 获取 RSA 公钥 | GET | `/zettos/main/user/v1/public_key` | 无（无需认证） | `{public_key: str(PEM), version}` |
| 登录 | POST | `/zettos/main/user/v1/login` | `{username, password(b64 RSA密文)}` | `{token: {access_token: str(JWT ES256), expires_at}}` |
| 设备识别/发现探测 | GET | `/zettos/main/system-settings/v1/device/beScan` | 无（无需认证） | `{model_name, device_name, sn, remote_access_id, ip_address2, system_version, is_init}` |

代码引用：`api.py` 路径常量 `_P_PUBLIC_KEY`/`_P_LOGIN`/`_P_BESCAN`；`async_probe_device()` 用 `beScan` 验证手动输入的 host。

### 3.2 设备与监控（读）

| 用途 | 方法 | 路径 | 请求参数 | 响应（data）字段 |
|------|------|------|----------|------------------|
| 设备信息 | GET | `/zettos/main/system-settings/v1/device` | 无 | `{model_name, device_name, sn, system_version, cpu, gpu, npu, memory, mac_address1, mac_address2, ip_address2, power_time, last_start_time}` |
| 实时监控 | GET | `/zettos/monitor/v1/view` | 无 | `{cpu:{usage_rate, thermal}, npu:[...], gpu:[...], mem:{total, free, used, cache, ...}, disks:{"DISK A":{read_rate, write_rate, read_bytes, write_bytes}, ...}}` |
| 存储池（含每盘） | GET | `/zettos/main/system-settings/v1/storage-pool` | 无 | `[{name, status, total_size, used_size, type(raid…), total_devices_number, progress, disks:[{model, serial_number, slot, size, type, status, temperature, is_support_smart, real_path}], hot_spare_disks, ssd_cache_*}]` |
| 用户配额池 | GET | `/zettos/main/system-settings/v1/storage-pool/user_pools` | 无 | `[{pool_name, quota}]` |
| 风扇模式 | GET | `/zettos/main/system-settings/v1/fan` | 无 | `data` 直接是 int（如 `2`）——**注意不是对象** |
| LCD 屏幕 | GET | `/zettos/main/system-settings/v1/lcd` | 无 | `{status: 1}` |
| RGB 状态灯 | GET | `/zettos/main/system-settings/v1/light` | 无 | `{mode, last_mode, start_color:"#RRGGBB", end_color:"#RRGGBB", speed}` |
| UPS 状态 | GET | `/zettos/main/system-settings/v1/ups/status/` | 无（无需认证） | UPS 状态对象（仅接 UPS 时） |
| 系统配置 | GET | `/zettos/main/system-settings/v1/system/configs` | 无 | `{ai_threshold, default_personal_pool, firmware_id, ...}` |
| 共享文件夹列表 | GET | `/zettos/main/local-storage/v1/storage/list` | 无 | team/share folders per pool |
| 磁盘清单 | GET | `/zettos/main/local-storage/v1/disks`、`/disks/size`、`/disks/usb` | 无 | 磁盘库存 |

### 3.3 控制（写）

| 用途 | 方法 | 路径 | 请求参数 | 响应 |
|------|------|------|----------|------|
| 设置风扇模式 | POST | `/zettos/main/system-settings/v1/fan/{mode}` | `mode` 整数在 URL 路径中，**无 body** | `{code:200,data:...}` |
| 屏幕开关 | POST | `/zettos/main/system-settings/v1/lcd` | `{"enable": <bool>}` | `{code:200,data:...}` |
| RGB 灯设置 | POST | `/zettos/main/system-settings/v1/light` | `{"mode":int, "start_color":"#RRGGBB", "end_color":"#RRGGBB", "speed":int}` | `{code:200,data:...}` |
| 启动 SMART 扫描 | POST | `/zettos/main/system-settings/v1/device/smart/start` | 无 | 异步任务 |

代码引用：`api.py` → `async_set_fan_mode(mode)`（`f"{_P_FAN}/{int(mode)}"`）、`async_set_lcd(enable)`、`async_set_light(payload)`。

### 3.4 SMART（异步，源码未在客户端实现，仅 CLAUDE.md 记录）

- `POST .../device/smart/start`（启动）、`GET .../smart/status`、`.../smart/statusAsync`、`.../smart/info`（`14502` 直到扫描完成）、`.../smart/stop`
- 集成 v0.1.1 **未实现 SMART button**（CLAUDE.md "Open items"），仅记录契约

---

## 4. 关键数据结构（JSON 示例）

> 以下为基于源码字段映射与 CLAUDE.md 契约重建的 JSON 形态。`sensor.py`/`entity.py`/`binary_sensor.py` 的字段访问路径印证了这些结构。

### 4.1 实时监控 `GET /zettos/monitor/v1/view` → `data`

```json
{
  "cpu": {"usage_rate": 12.3, "thermal": 52},
  "npu": [0, 0],
  "gpu": [5],
  "mem": {"total": 17179869184, "free": 8589934592, "used": 8589934592, "cache": 1234567},
  "disks": {
    "DISK A": {"read_rate": 0, "write_rate": 0, "read_bytes": 123456, "write_bytes": 789012},
    "DISK B": {"read_rate": 0, "write_rate": 0, "read_bytes": 0, "write_bytes": 0}
  }
}
```

字段访问印证（`sensor.py`）：
- CPU 使用率：`monitor.cpu.usage_rate`
- CPU 温度：`monitor.cpu.thermal`
- 内存：`monitor.mem.used` / `monitor.mem.total`（使用率 = used/total*100）
- NPU：`monitor.npu` 为数组，取均值（`[x for x in npu if isinstance(x,(int,float))]`）
- GPU：`monitor.gpu` 为数组，取 `gpu[0]`

### 4.2 存储池 `GET /zettos/main/system-settings/v1/storage-pool` → `data`（数组）

```json
[
  {
    "name": "pool1",
    "status": 0,
    "total_size": 8000000000000,
    "used_size": 2000000000000,
    "type": "raid5",
    "total_devices_number": 4,
    "progress": 100,
    "disks": [
      {
        "model": "WD40EFRX",
        "serial_number": "WD-WCC4XXXXXX",
        "slot": 1,
        "size": 4000000000000,
        "type": "sata",
        "status": 0,
        "temperature": 35,
        "is_support_smart": true,
        "real_path": "/dev/sda"
      }
    ],
    "hot_spare_disks": [],
    "ssd_cache_disks": []
  }
]
```

字段访问印证：
- 池名 `pool.name`、用量 `pool.used_size`/`pool.total_size`（`sensor.py` `ZettlabPoolSensor`）
- 池健康 `pool.status != 0` → problem（`binary_sensor.py` `ZettlabPoolProblem`）
- 盘温度 `disk.temperature`、序列号 `disk.serial_number`、属性 `model/slot/type/real_path`（`sensor.py` `ZettlabDiskTempSensor`）

### 4.3 设备信息 `GET /zettos/main/system-settings/v1/device` → `data`

```json
{
  "model_name": "Zettlab D4",
  "device_name": "MyNAS",
  "sn": "ZT2024XXXXXXXX",
  "system_version": "1.9.0-beta",
  "cpu": "RK3588",
  "gpu": "Mali-G610",
  "npu": "6TOPS",
  "memory": 17179869184,
  "mac_address1": "aa:bb:cc:dd:ee:ff",
  "mac_address2": "aa:bb:cc:dd:ee:00",
  "ip_address2": "192.168.1.100",
  "power_time": 1234567,
  "last_start_time": 1785800000
}
```

字段访问印证（`entity.py` `device_info`、`sensor.py` `_uptime`）：
- `sn`（唯一 ID）、`model_name`、`device_name`、`system_version`、`mac_address1`/`mac_address2`（连接）
- `last_start_time`（unix 秒，转 datetime 作 uptime 诊断传感器）

### 4.4 控制状态

```json
// GET /fan → data 直接是 int
2

// GET /lcd → {status: 1}
{"status": 1}

// GET /light
{"mode": 0, "last_mode": 1, "start_color": "#FFFFFF", "end_color": "#FFFFFF", "speed": 1}
```

---

## 5. 轮询策略

源自 `coordinator.py` 的 `ZettlabAinasCoordinator`。

- **update_interval**：`timedelta(seconds=scan)`，`scan` 取 `entry.options["scan_interval"]`，默认 `DEFAULT_SCAN_INTERVAL=30`，最小 `MIN_SCAN_INTERVAL=10`（options flow 校验）
- **单一 coordinator**（无快/慢刷新分层）；所有平台实体共享同一 `ZettlabData` 快照
- **每次轮询调用**（`_async_update_data`）：
  - **核心读取**（失败 → `UpdateFailed`，auth 失败 → `ConfigEntryAuthFailed` 触发 reauth）：
    1. `async_get_device()` → `GET /device`
    2. `async_get_storage_pools()` → `GET /storage-pool`
    3. `async_get_monitor()` → `GET /monitor/v1/view`
  - **best-effort 读取**（经 `_safe()` 包装，失败返回 `None`/`{}`，不 blank 整个快照）：
    4. `async_get_fan_mode()` → `GET /fan`
    5. `async_get_lcd()` → `GET /lcd`
    6. `async_get_light()` → `GET /light`
- **快照结构** `ZettlabData`（@dataclass）：`device:dict`、`pools:list`、`monitor:dict`、`fan_mode:int|None`、`lcd:dict`、`light:dict`
- **token 过期**：客户端层透明处理（`_async_request` 捕获 `ZettosAuthError` → 重登录 → 重试一次），coordinator 无感
- **请求超时**：`REQUEST_TIMEOUT=15` 秒（`api.py`）

---

## 6. 已知限制与未映射项

源自 CLAUDE.md "Open items" + 各平台源码注释：

| 项 | 状态 | 说明 |
|----|------|------|
| 风扇模式整数枚举 | **未解码** | 观察值 `2`，`select.py` 选项显示为原始 "0"–"3"，语义标签待抓包/读固件 |
| RGB 灯 mode/speed 整数 | **未解码** | `light.py` 仅暴露 RGB 颜色，写时保留设备当前 mode/speed |
| 屏幕亮度 | **无 HTTP setter** | `lcd_brightness=255` 仅设备本地（CLAUDE.md 第 64 行），web API 只支持 on/off |
| 重启/关机/scrub | **端点未映射** | 不在 web bundle 中，需 Playwright 抓包桌面快捷面板 |
| UDP 9527 发现 | **未实现** | `DISCOVERY_PROBE=b""`，探测包未捕获，`discovery.py` 返回空，回退手动 IP + `beScan` HTTP 探测 |
| SMART 扫描 button | **未实现** | 契约存在（`smart/start`/`info`），v0.1.1 未做实体；`info` 扫描前返回 `14502` |
| UPS / 固件更新 / 共享文件夹 | **未实现** | 契约存在（`ups/status/`、`zettos-ota`、`local-storage/v1/storage/list`），未做实体 |
| 远程访问 | **不支持** | 云账号 + P2P 隧道（`pgTunnelStatic`），无可复用本地 API，集成仅本地 |
| 登出 | **未在源码中发现** | 无 logout 端点 |

---

## 7. 移植到 ESP32 C 的要点与挑战

### 7.1 模块映射

| Python 依赖 | ESP32 C 替代 | 注意点 |
|-------------|-------------|--------|
| `aiohttp`（HTTPS） | `esp_http_client` | 自签名证书：配置 `cert_pem=NULL` + `skip_cert_common_name=true`，或用 `use_global_ca_store` 钉选设备证书；ESP32-S3 RAM 有限，响应缓冲需限长 |
| `cryptography` RSA PKCS1v15 | `mbedTLS`（`mbedtls/pk.h`、`mbedtls/rsa.h`） | 见下文代码思路 |
| `base64` | `mbedtls/base64.h` 或 `esp_base64` | — |
| JSON 解析 | `cJSON` | 响应是嵌套对象，用 `cJSON_GetObjectItem` 链式访问；注意 `fan` 的 `data` 直接是 int |
| asyncio | FreeRTOS 任务 + 状态机 | 单任务轮询即可，无需并发；token 过期重登录需状态标志 |

### 7.2 关键挑战

1. **RSA-PKCS1v15 加密**：ZettOS 公钥是 RSA-2048 PEM。mbedTLS 流程：`mbedtls_pk_parse_public_key(&pk, pem, pem_len+1)` → 校验 `mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA)` → `mbedtls_rsa_pkcs1_encrypt(rsa, rng_fn, NULL, MBEDTLS_RSA_PUBLIC, pwd_len, pwd, out)`（PKCS1 v1.5 是默认填充）→ `mbedtls_base64_encode`。**坑点**：ESP-IDF 默认 mbedTLS 配置需启用 `MBEDTLS_PKCS1_V15` 与 RSA；PEM 公钥解析需 `MBEDTLS_PEM_PARSE_C`；RSA-2048 密文 256 字节，base64 后约 344 字节，需预留缓冲。
2. **JWT 存储与刷新**：token 是字符串，存静态 buffer；每次请求在 header 加 `Authorization: <token>`（**务必不带 `Bearer `**）。需实现"401/403 → 重新登录 → 重试一次"逻辑。token 约 3h 有效，可惰性刷新。
3. **自签名 HTTPS**：必须关闭校验或钉选设备证书。`esp_http_client_config_t` 设 `cert_pem=NULL`、`skip_cert_common_name=true`、`use_global_ca_store=false`；或抓取设备证书内置。
4. **响应信封**：所有端点先判 `code==200` 再取 `data`。`fan` 的 `data` 是裸 int，与其他端点不同，需特判。
5. **风扇模式写**：mode 在 URL 路径（`/fan/{mode}`）而非 body，POST 无 body。
6. **内存**：D4 监控数据不大，但 `storage-pool` 含多盘多池，cJSON 解析后应即时提取字段释放，避免同时持有完整解析树。

### 7.3 可简化的部分

- ESP32 上若只做监控显示，可实现核心 3 端点（device/pools/monitor）+ 按需控制（fan/lcd/light）；best-effort 逻辑可省略（单任务顺序请求即可）。
- 无需实现 UDP 发现、SMART、UPS、远程访问。
- 轮询间隔建议 ≥10s（与源码 MIN 一致），避免压垮设备。

---

## 8. 参考代码片段（Python 原文 + C 移植思路）

### 8.1 RSA 密码加密（`api.py` → `_encrypt_password`）

**Python 原文**：

```python
def _encrypt_password(public_key_pem: str, password: str) -> str:
    public_key = load_pem_public_key(public_key_pem.encode())
    encrypted = public_key.encrypt(password.encode(), padding.PKCS1v15())
    return base64.b64encode(encrypted).decode()
```

**C 移植思路（mbedTLS）**：

```c
// pem 为设备返回的 public_key 字符串（含 \n）
mbedtls_pk_context pk; mbedtls_pk_init(&pk);
mbedtls_pk_parse_public_key(&pk, (const unsigned char*)pem, strlen(pem)+1);
mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
// PKCS1 v1.5 是 RSA 默认填充
unsigned char cipher[256]; // RSA-2048 密文
mbedtls_rsa_pkcs1_encrypt(rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
                          MBEDTLS_RSA_PUBLIC, pwd_len, (const unsigned char*)pwd, cipher);
// cipher[256] -> base64 -> 放入 login body
size_t olen = 0;
mbedtls_base64_encode(NULL, 0, &olen, cipher, 256); // 先取长度
mbedtls_base64_encode(b64_buf, b64_cap, &olen, cipher, 256);
mbedtls_pk_free(&pk);
```

注：`mbedtls_ctr_drbg` 需先 `mbedtls_entropy_init` + `mbedtls_ctr_drbg_seed` 初始化。

### 8.2 登录与 token 提取（`api.py` → `async_login`）

**Python 原文**：

```python
async def async_login(self) -> None:
    pem = await self._async_get_public_key()
    encrypted = _encrypt_password(pem, self._password)
    payload = await self._async_raw_request(
        "POST", _P_LOGIN,
        json={"username": self._username, "password": encrypted},
        authed=False,
    )
    token = (payload.get("data") or {}).get("token", {}).get("access_token")
    if not token:
        raise ZettosAuthError("login response did not contain an access token")
    self._token = token
```

**C 移植思路**：三次 HTTP 串行——

1. `GET /zettos/main/user/v1/public_key` → cJSON 解析 `data.public_key`（PEM 字符串）
2. mbedTLS 加密密码 → base64
3. `POST /zettos/main/user/v1/login`，body=`{"username":"admin","password":"<b64>"}`，解析 `data.token.access_token` 存入 `static char s_token[...]`

### 8.3 认证请求头（`api.py` → `_async_raw_request`）

**Python 原文**（关键行）：

```python
headers: dict[str, str] = {}
if authed:
    if self._token is None:
        raise ZettosAuthError("not logged in")
    headers["Authorization"] = self._token  # 裸 token，无 Bearer
```

**C 移植思路**（`esp_http_client`）：

```c
esp_http_client_config_t cfg = {
    .url = "https://<host>/zettos/main/system-settings/v1/device",
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .cert_pem = NULL,
    .skip_cert_common_name = true,  // 跳过自签名校验
    .timeout_ms = 15000,
};
esp_http_client_handle_t cli = esp_http_client_init(&cfg);
char auth_hdr[512];
snprintf(auth_hdr, sizeof(auth_hdr), "%s", s_token);  // 裸 token，无 "Bearer "
esp_http_client_set_header(cli, "Authorization", auth_hdr);
esp_http_client_perform(cli);
// 响应 {code:200, data:{...}} -> cJSON 解析
```

### 8.4 风扇模式写（`api.py` → `async_set_fan_mode`）

**Python 原文**：

```python
async def async_set_fan_mode(self, mode: int) -> None:
    await self._async_request("POST", f"{_P_FAN}/{int(mode)}")
```

**C 移植**：URL 拼成 `https://<host>/zettos/main/system-settings/v1/fan/2`，POST 空 body，带 `Authorization` 头。

### 8.5 轮询核心（`coordinator.py` → `_async_update_data`）

**Python 原文**（核心读）：

```python
device = await self.client.async_get_device()
pools = await self.client.async_get_storage_pools()
monitor = await self.client.async_get_monitor()
```

**C 移植**：顺序 3 次 GET（`/device` → `/storage-pool` → `/monitor/v1/view`），各自解析后填入本地结构体。失败按 `code != 200` 或 HTTP 状态判定；若 401/403 则重登录后重试一次（与 `_async_request` 一致）。

---

## 9. 端点路径常量速查（`api.py` 顶部）

```python
_P_PUBLIC_KEY  = "/zettos/main/user/v1/public_key"
_P_LOGIN       = "/zettos/main/user/v1/login"
_P_BESCAN      = "/zettos/main/system-settings/v1/device/beScan"
_P_DEVICE      = "/zettos/main/system-settings/v1/device"
_P_STORAGE_POOL= "/zettos/main/system-settings/v1/storage-pool"
_P_MONITOR     = "/zettos/monitor/v1/view"          # 注意：不在 /main 下
_P_FAN         = "/zettos/main/system-settings/v1/fan"
_P_LCD         = "/zettos/main/system-settings/v1/lcd"
_P_LIGHT       = "/zettos/main/system-settings/v1/light"
```

**端口**：网关同时服务 `:80` 与 `:443`（自签名），客户端默认走 443 并跳过 TLS 校验。

---

## 10. 实现优先级建议（针对本项目 `zettlab_client`）

1. **第一里程碑（最小可用）**：实现认证三步（public_key → RSA 加密 → login 取 token）+ 核心 3 读端点（device / storage-pool / monitor）。即可驱动 Overview 屏的 CPU/温度/内存/磁盘/HDD 全部指标。
2. **第二里程碑（告警补强）**：解析 `storage-pool` 的 `pool.status != 0` → 触发池问题告警（对应 D8U 案例 27 万校验错误场景）；解析 `disks[].temperature` → DiskDetail 每盘温度。
3. **第三里程碑（可选控制）**：fan 模式读写、lcd 开关（仅当需要本地控制时）。
4. **暂不实现**：UDP 发现、SMART、UPS、远程访问、RGB 灯、重启/关机。

---

## 附：上游仓库参考

- 上游仓库：`Xianleewu/zettlab-ainas-iot`（v0.1.1，MIT 许可）
- 上游契约文档：仓库根目录 `CLAUDE.md`
- 本规格提取时间：2026-08-04
- 免责声明：本规格为反向工程结果，非 Zettlab 官方 API 文档。"Zettlab" 和 "ZettOS" 是其各自所有者的商标。
