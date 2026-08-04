# ZotLab NAS Monitor - 项目 Wiki 实现计划

## [x] Task 1: 项目整体架构分析

- **Priority**: high
- **Depends On**: None
- **Description**: 
  - 分析项目目录结构和核心模块
  - 识别关键文件和依赖关系
  - 提取项目常量和配置信息
- **Acceptance Criteria Addressed**: [完整的目录结构文档, 核心架构描述]
- **Test Requirements**:
  - `programmatic` TR-1.1: 所有目录和文件均在文档中有对应描述
  - `human-judgement` TR-1.2: 架构图清晰展示模块间关系

## [x] Task 2: 核心模块详细文档

- **Priority**: high
- **Depends On**: Task 1
- **Description**: 
  - 文档化配置管理模块 (app_cfg)
  - 文档化事件总线模块 (event_bus)
  - 文档化显示驱动模块 (disp_driver)
  - 文档化NAS数据层 (nas_data)
  - 文档化UI系统
  - 文档化网络模块
  - 文档化数据源抽象层 (data_source)
- **Acceptance Criteria Addressed**: [每个核心模块的职责、API、数据结构]
- **Test Requirements**:
  - `programmatic` TR-2.1: 所有核心API函数均有文档说明
  - `human-judgement` TR-2.2: 数据结构描述完整准确

## [x] Task 3: 关键技术概念文档

- **Priority**: medium
- **Depends On**: Task 1, Task 2
- **Description**: 
  - 文档化LVGL TileView机制
  - 文档化事件驱动架构
  - 文档化WiFi配网流程
  - 文档化配置脏字段机制
- **Acceptance Criteria Addressed**: [关键技术概念的详细解释]
- **Test Requirements**:
  - `human-judgement` TR-3.1: 技术流程描述清晰易懂
  - `human-judgement` TR-3.2: 流程图准确反映实际代码逻辑

## [x] Task 4: 构建系统和调试工具文档

- **Priority**: medium
- **Depends On**: Task 1
- **Description**: 
  - 文档化CMake构建配置
  - 文档化编译定义
  - 文档化CLI命令行工具
  - 文档化日志级别设置
- **Acceptance Criteria Addressed**: [构建和调试相关文档]
- **Test Requirements**:
  - `programmatic` TR-4.1: CMakeLists.txt中的源文件列表与文档一致
  - `human-judgement` TR-4.2: 调试命令和方法实用

## [x] Task 5: 代码规范和扩展指南

- **Priority**: medium
- **Depends On**: Task 1, Task 2
- **Description**: 
  - 文档化文件命名规范
  - 文档化变量和函数命名规范
  - 文档化UI文件五段式写法
  - 提供添加新NAS客户端的指南
  - 提供添加新设置标签页的指南
  - 提供添加新UI屏幕的指南
- **Acceptance Criteria Addressed**: [代码规范和扩展指南]
- **Test Requirements**:
  - `human-judgement` TR-5.1: 规范描述与实际代码一致
  - `human-judgement` TR-5.2: 扩展指南步骤清晰可操作

## [x] Task 6: 常见问题和版本历史

- **Priority**: low
- **Depends On**: Task 2, Task 3
- **Description**: 
  - 整理WiFi配网失败的排查方法
  - 整理屏幕显示异常的排查方法
  - 整理NAS数据不更新的排查方法
  - 添加版本历史记录
- **Acceptance Criteria Addressed**: [故障排查指南和版本历史]
- **Test Requirements**:
  - `human-judgement` TR-6.1: 问题描述准确
  - `human-judgement` TR-6.2: 解决方案实用有效

## [x] Task 7: Wiki文档更新和验证

- **Priority**: medium
- **Depends On**: Task 1-6
- **Description**: 
  - 更新NAS类型列表（添加FNOS、UNRAID、Linux HTTP/Serial、Windows）
  - 更新事件总线完整事件列表
  - 更新配置管理完整字段列表
  - 添加WiFi适配层详细说明
  - 添加数据源抽象层详细说明
  - 更新启动流程与实际代码一致
- **Acceptance Criteria Addressed**: [Wiki文档与实际代码一致]
- **Test Requirements**:
  - `programmatic` TR-7.1: 所有枚举类型与头文件一致
  - `human-judgement` TR-7.2: 所有API文档与头文件声明一致
