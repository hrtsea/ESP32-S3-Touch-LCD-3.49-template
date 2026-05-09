"""Source-of-truth translations table.

Keys map to a 4-tuple (en, zh-CN, ja, ko). The generator (gen_i18n.py)
turns this into main/i18n_strings.h plus a glyph-subset font that covers
every code point used across all four locales.

Add a new language by adding a new entry to LANGS and a new column here.
"""

LANGS = [
    ("en", "English"),
    ("zh", "中文"),    # 中文
    ("ja", "日本語"),  # 日本語
    ("ko", "한국어"),  # 한국어
]

# (key, en, zh, ja, ko)
TRANSLATIONS = [
    # Settings root
    ("menu_title",        "Menu",              "菜单",               "メニュー",           "메뉴"),
    ("set_wifi",          "Wi-Fi",             "Wi-Fi",              "Wi-Fi",              "Wi-Fi"),
    ("set_tz",            "Time zone",         "时区",               "タイムゾーン",       "시간대"),
    ("set_display",       "Display",           "显示",               "表示",               "디스플레이"),
    ("set_sound",         "Sound",             "声音",               "音声",               "사운드"),
    ("set_brightness",    "Brightness",        "亮度",               "明るさ",             "밝기"),
    ("set_autodim",       "Auto-dim",          "自动调暗",           "自動減光",           "자동 어두움"),
    ("set_reset",         "Reset",             "重置",               "リセット",           "초기화"),
    ("set_language",      "Language",          "语言",               "言語",               "언어"),

    # Common
    ("back",              "Back",              "返回",               "戻る",               "뒤로"),
    ("idle",              "Idle",              "空闲",               "アイドル",           "대기 중"),
    ("on",                "On",                "开",                 "オン",               "켜짐"),
    ("off",               "Off",               "关",                 "オフ",               "꺼짐"),
    ("never",             "Never",             "永不",               "なし",               "안 함"),

    # Wi-Fi page
    ("wifi_not_conn",     "Not connected",     "未连接",             "未接続",             "연결 안 됨"),
    ("wifi_scanning",     "Scanning...",       "扫描中...",          "スキャン中...",      "검색 중..."),
    ("wifi_scan_btn",     "Scan networks",     "扫描网络",           "スキャン",           "검색"),
    ("wifi_found_n",      "Found %u networks", "找到 %u 个网络",     "%u 件見つかりました", "%u개 검색됨"),
    ("wifi_no_aps",       "(no networks yet -- tap Scan)", "(无网络 — 请点扫描)",
                          "(まだネットワークなし — スキャンを押す)", "(없음 — 검색 누르기)"),
    ("wifi_connecting",   "Connecting to %s...", "连接 %s 中...",    "%s に接続中...",     "%s 에 연결 중..."),
    ("wifi_pass_for",     "Pass for %s",       "%s 的密码",          "%s のパスワード",    "%s 비밀번호"),
    ("wifi_autoconnect",  "Auto-connect on boot", "开机自动连接",   "起動時に自動接続",   "부팅 시 자동 연결"),
    ("wifi_connecting_n", "Connecting to %s... (%us)", "连接 %s 中... (%us)",
                          "%s に接続中... (%us)", "%s 에 연결 중... (%us)"),
    ("wifi_connect_btn",  "Connect",           "连接",               "接続",               "연결"),
    ("wifi_forget_btn",   "Forget",            "忘记",               "削除",               "삭제"),

    # Time-zone (continent labels mirror tz_cities; generated table keeps the
    # English names there. Only the page title gets translated.)

    # Display sub-page
    ("hour_24",           "24-hour clock",     "24小时制",           "24時間制",           "24시간 형식"),
    ("show_seconds",      "Show seconds",      "显示秒",             "秒を表示",           "초 표시"),
    ("show_ms",           "Show ms",           "显示毫秒",           "ミリ秒を表示",       "ms 표시"),
    ("show_fps",          "Show FPS",          "显示 FPS",           "FPS を表示",         "FPS 표시"),
    ("date_format",       "Date format",       "日期格式",           "日付形式",           "날짜 형식"),
    ("theme",             "Theme (menu reloads on next boot)", "主题(重启后生效)",
                          "テーマ(再起動で反映)", "테마(재부팅 후 적용)"),
    ("theme_dark",        "Dark",              "暗色",               "ダーク",             "다크"),
    ("theme_light",       "Light",             "亮色",               "ライト",             "라이트"),
    ("theme_hicontrast",  "High contrast",     "高对比度",           "ハイコントラスト",   "고대비"),
    ("lang_en",           "English",           "英语",               "英語",               "영어"),
    ("lang_zh",           "Chinese",           "中文",               "中国語",             "중국어"),
    ("lang_ja",           "Japanese",          "日语",               "日本語",             "일본어"),
    ("lang_ko",           "Korean",            "韩语",               "韓国語",             "한국어"),

    # Sound sub-page
    ("sound_enabled",     "Sound enabled",     "启用声音",           "音声を有効化",       "사운드 사용"),
    ("volume_pct",        "Volume %u%%",       "音量 %u%%",          "音量 %u%%",          "볼륨 %u%%"),

    # Auto-dim
    ("dim_after",         "Dim after %s",      "%s 后变暗",          "%s 後に減光",        "%s 후 어둡게"),
    ("dim_never",         "Dim: Never",        "变暗: 永不",         "減光: なし",         "어둡게: 안 함"),
    ("sleep_after",       "Sleep after %s",    "%s 后睡眠",          "%s 後にスリープ",    "%s 후 절전"),
    ("sleep_never",       "Sleep: Never",      "睡眠: 永不",         "スリープ: なし",     "절전: 안 함"),
    ("backlight_level",   "Backlight level",   "背光亮度",           "バックライト",       "백라이트"),

    # Reset
    ("reset_warn",        "Erase all settings + Wi-Fi creds, then reboot.",
                          "清除所有设置和 Wi-Fi 凭据，然后重启。",
                          "全ての設定と Wi-Fi 認証情報を削除して再起動します。",
                          "모든 설정과 Wi-Fi 정보를 지우고 재부팅합니다."),
    ("reset_btn",         "Erase + reboot",    "清除并重启",         "削除して再起動",     "지우고 재부팅"),

    # Radio tile
    ("radio_now_playing", "Now Playing",       "正在播放",           "再生中",             "재생 중"),
    ("radio_no_station",  "No station yet",    "尚未选择电台",       "未選択",             "선택 안 됨"),
    ("radio_engine_init", "Engine init...",    "引擎初始化...",      "エンジン起動中...",  "엔진 초기화..."),
    ("radio_engine_fail", "Engine init FAILED","引擎初始化失败",     "起動失敗",           "엔진 초기화 실패"),
    ("radio_connecting",  "Connecting %s...",  "连接 %s 中...",      "%s 接続中...",       "%s 에 연결 중..."),
    ("radio_playing",     "Playing %s",        "正在播放 %s",        "%s を再生中",        "%s 재생 중"),
    ("radio_play_fail",   "Play FAILED",       "播放失败",           "再生失敗",           "재생 실패"),
    ("radio_stopped",     "Stopped",           "已停止",             "停止",               "정지됨"),
    ("vol_n",             "Vol %d",            "音量 %d",            "音量 %d",            "볼륨 %d"),

    # Clock tile
    # (date/time digits aren't language-specific; only the bottom-right TZ
    # label uses k_tz_cities[].name which stays English.)
]
