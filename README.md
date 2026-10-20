# adjustable-power
基于 ESP-IDF 开发的数控可调电源项目，支持电压、电流精准调节，集成采样、保护、人机交互、OTA升级等功能。

| Supported Targets | ESP32 | ESP32-C3 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- |

## ✨ 项目简介
本项目是一款由ESP32主控的数控直流可调电源，采用分层架构开发，代码解耦，方便二次开发与功能扩展。
硬件配合功率变换电路实现输出电压、电流数控调节；片内ADC完成输出采样，软件实现闭环控制，内置多重安全保护机制。
项目使用 ESP-IDF 原生框架 + IDF Component Manager 管理第三方组件，便于版本维护、多人协作、跨设备同步开发。

## 📁 工程目录结构
```
adjustable-power/
├── main/                 # 应用入口、主任务逻辑
├── components/           # 自定义底层驱动组件（DAC、ADC、屏幕、外设驱动等）
├── middleware/           # 中间层：滤波算法、PID 控制、数据解析、协议封装
├── service/              # 业务服务层：电源控制、保护逻辑、参数管理
├── tools/                # 辅助工具、上位机交互脚本
├── ota_tool.py           # OTA 固件升级脚本
├── CMakeLists.txt        # 工程顶层 CMake 配置
├── dependencies.lock     # 依赖锁定文件，保证编译环境一致性
├── .gitignore            # Git 忽略配置

## 🛠️ 开发环境
- 主控框架：ESP-IDF
- 推荐版本：ESP-IDF v5.x
- 开发编辑器：VSCode + ESP-IDF Extension
- 依赖管理：ESP Component Manager
- 调试工具：idf.py、串口监视器

## 🚀 编译、烧录与调试
> 先加载ESP-IDF环境，在工程根目录终端执行
```bash
# 设置目标芯片（按需修改，例：esp32 / esp32c3 / esp32s3）
idf.py set-target esp32

# 图形化配置（引脚、串口、参数配置）
idf.py menuconfig

# 编译工程
idf.py build

# 编译 + 烧录（Windows端口为COMx）
idf.py -p COMx flash

# 烧录并打开串口监视器
idf.py -p COMx flash monitor
- 串口监视器退出快捷键：`Ctrl + ]`
- 波特率默认：115200，可在 menuconfig 修改

## 📋 功能规划 & 开发进度

- 工程框架搭建，分层架构（main /components/middleware /service）
- DAC 数控调压输出
- ADC 电压、电流采样
- PID 闭环稳压稳流
- 过压、过流、过热保护
- OLED 屏幕参数显示
- 参数掉电保存
- 串口指令交互
- Web 网页控制
- OTA 远程固件升级

## 📌 Git 版本管理规范（本仓库使用）

> 
> 提交信息规范，方便回溯、维护

- `feat:` 新增功能，例：`feat: 增加OLED实时参数显示`
- `fix:` 修复 bug，例：`fix: 修复过流保护误触发`
- `opt:` 代码优化、性能调整，例：`opt: 优化ADC采样滤波`
- `docs:` 修改文档、注释
- `refactor:` 重构代码，不改动功能
- `chore:` 构建脚本、依赖、工程配置调整

> 
> 开发建议：
> 稳定版本在 `main` 分支；新功能在 `dev` 分支开发，测试完成后合并至 main。

## ⚠️ 重要注意事项

1. **禁止提交私密配置**
WiFi 账号、密钥、校准参数、秘钥等不要直接提交仓库；
单独放到 `secret.h`，并加入 `.gitignore`，仓库内可放 `secret_example.h` 作为模板。
2. 编译产物、组件缓存不上传
`build/`、`managed_components/`、`.cache/` 由 gitignore 自动忽略，无需手动添加。
3. 硬件安全提醒
可调电源属于功率电路，上电前务必检查硬件接线，防止短路烧毁主控与功率器件。

## 🤝 参与贡献

欢迎 Star、Fork，如果你有优化思路、bug 反馈、新功能想法：

1. 提交 Issue 描述问题或需求
2. Fork 仓库，新建分支开发，完成后提交 Pull Request

## 📚 参考文档

- ESP-IDF 官方文档：[https://docs.espressif.com/projects/esp-idf/](https://docs.espressif.com/projects/esp-idf/)
- 乐鑫社区论坛：[https://esp32.com/](https://esp32.com/)