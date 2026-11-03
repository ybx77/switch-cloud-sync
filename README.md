# Switch 云存档同步

> **Switch Cloud Sync** — 一个运行在大气层（Atmosphère）上的 Switch 自制应用（`.nro`），把**游戏存档**、**相册截图/视频**和**游玩记录**备份到你自己的服务器，也可以从服务器恢复存档。
>
> 图形界面 · ZIP 压缩 · 内容去重 · 详细游玩事件 · 服务器完全自定义

![icon](icon.jpg)

> 这是一个早期但可用的版本。它不是任天堂官方软件，与任天堂无关；请仅用于备份自己的存档。

---

## 目录

- [功能](#功能)
- [界面](#界面)
- [它是怎么工作的](#它是怎么工作的)
- [构建](#构建)
- [安装](#安装)
- [使用](#使用)
- [设置项](#设置项)
- [服务端接口](#服务端接口)
- [项目结构](#项目结构)
- [常见问题](#常见问题)
- [免责声明](#免责声明)
- [致谢](#致谢)

---

## 功能

| 功能 | 说明 |
| --- | --- |
| **一键上传全部** | 依次执行 存档 → 相册 → 游玩记录，结束后给出每项统计 |
| **存档备份** | 枚举账号存档 + 设备存档（NAND / SD），打包成标准 **ZIP**（deflate 压缩）后上传 |
| **存档恢复** | 从服务器列表选择时间点，校验 SHA-256 和 ZIP 结构后清空本地存档并写回 |
| **只传变化的内容** | 用 save data `commit_id` 预判 + ZIP 内容 SHA-256 二次确认，没变化时连文件都不读 |
| **相册增量** | 扫描 Album（含视频），按 `路径 + 大小 + mtime` 只传新增/变化 |
| **游玩记录** | 汇总（每游戏×用户累计时长）+ 与 NX-Activity-Log 导出一致的**详细事件**（启动/关闭/焦点/账号登录登出） |
| **图形界面** | 卡片式首页、设置页、进度页、恢复选择页；中文使用系统共享字体（FreeType）渲染 |
| **应用内设置** | 服务器地址、令牌、超时、开关、上传上限等全部可在应用里编辑并保存 |
| **服务器自定义** | HTTP / HTTPS、任意端口/路径、Bearer Token、自签 CA |

### 不重复上传是怎么判断的

1. 读存档的 `FsSaveDataExtraData.commit_id`，和上次上传记录一致 → **直接跳过，连存档都不挂载**
2. 否则打包成 ZIP 并计算 SHA-256，和上次一致 → 跳过（不上传）
3. 只有内容真的变化才上传

所以一个很久没玩的游戏，存档没有新提交，点上传时几乎瞬间跳过。

### 压缩

- 使用 `minizip`（zlib deflate，level 1，优先速度）
- ZIP 内包含 `.__scs__/meta.txt`、`.__scs__/index.txt`，其余就是存档文件本体
- 产物是标准 ZIP，PC 上可以直接解压查看
- 兼容识别**拼接文件**（concatenation file：带 archive bit 的目录，只能当文件打开），恢复时用 `FsCreateOption_BigFile` 重建
- 客户端仍能恢复旧版（SCSA1）归档，历史备份不用迁移

---

## 界面

```
┌──────────────────────────────────────────────────────────────┐
│  Switch 云存档 1.1.0                        ● 已配置          │
│  http://192.168.1.10:8420                                     │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ ⟳  一键上传全部      存档 · 相册 · 游玩记录             │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────┐  ┌───────────────────────┐        │
│  │ 💾 上传存档            │  │ ⤓ 恢复存档             │        │
│  │    只传有变化的存档     │  │    从服务器选择并写回   │        │
│  └───────────────────────┘  └───────────────────────┘        │
│  ┌───────────────────────┐  ┌───────────────────────┐        │
│  │ 🖼 上传相册            │  │ 🕒 上传游玩记录         │        │
│  └───────────────────────┘  └───────────────────────┘        │
│  ┌───────────────────────┐  ┌───────────────────────┐        │
│  │ ⚙ 设置                 │  │ ⏻ 退出                │        │
│  └───────────────────────┘  └───────────────────────┘        │
│ Ⓐ 选择   方向键 移动   Ⓑ 退出                                │
└──────────────────────────────────────────────────────────────┘
```

- 上传过程中按 `B` 取消，进度页实时显示日志
- 恢复列表会显示：游戏名、**用户昵称 / 设备存档**、标题 ID、大小、时间
- 右上角状态：灰=未配置，黄=已配置未测试，绿=连接正常，红=连不上

---

## 构建

需要 [devkitPro](https://devkitpro.org/wiki/Getting_Started)（devkitA64 + libnx）、`switch-curl`、`switch-freetype`、`switch-harfbuzz`、`switch-zlib`（minizip）。

```bash
pacman -S --needed switch-dev switch-curl switch-freetype switch-harfbuzz switch-zlib
make -j$(nproc)
```

产物：

```
switch-cloud-sync.nro
```

图标由根目录的 `icon.jpg`（256×256）自动打包。想重新生成图标：

```bash
python -m pip install pillow
python tools/make_icon.py
```

> 也可以在 GitHub Actions 里构建：仓库自带 `.github/workflows/build.yml`，push 后到 Actions 页面下载 artifact。

---

## 安装

1. 把 `switch-cloud-sync.nro` 复制到 SD 卡：`sd:/switch/switch-cloud-sync.nro`
2. 从 hbmenu 启动（推荐按住 `R` 启动任意游戏进入 hbmenu，全内存模式）
3. 首次运行会在 SD 卡生成：

```
sdmc:/switch/cloud-sync/
├── config.ini
├── logs/sync.log
├── state/
│   ├── device_id.txt
│   ├── saves.tsv      # 存档 SHA-256 + commit_id 索引
│   ├── album.tsv      # 相册 size + mtime 索引
│   └── meta.ini
└── tmp/
```

4. 打开「设置」，填写服务器地址和令牌，点「测试服务器连接」
5. 回首页点「一键上传全部」

首次启动会加载系统共享字体，界面出现可能需要 1-2 秒。

---

## 使用

| 按键 | 作用 |
| --- | --- |
| `↑ ↓ ← →` | 移动选择 |
| `A` | 确认 / 编辑 |
| `B` | 返回 / 取消当前传输 |
| `X`（设置页） | 保存设置 |
| `Y`（设置页） | 重新读取配置文件 |
| `+` | 退出 |

建议的第一次联调顺序：

1. 设置 → 测试服务器连接 → 右上角变绿
2. 上传游玩记录（纯文本 + JSON，最容易验证）
3. 上传存档（第二次运行应大量显示「跳过（未变化）」）
4. 在有历史版本的前提下再测试恢复

**恢复存档会覆盖本地进度**，应用会二次确认；请确认游戏已经完全关闭。

---

## 设置项

| 设置 | 对应 `config.ini` | 说明 |
| --- | --- | --- |
| 服务器地址 | `[server] base_url` | 如 `http://192.168.1.10:8420`，不要带结尾 `/` |
| 访问令牌 | `[server] token` | `Authorization: Bearer <token>` |
| CA 证书路径 | `[server] ca_bundle` | 留空时自动使用 `sd:/switch/cloud-sync/ca-bundle.pem` |
| 跳过 TLS 校验 | `[server] insecure_tls` | 仅内网调试 |
| 网络超时 | `[server] timeout_sec` | 空闲超时秒数 |
| 设备名称 / 设备 ID | `[device]` | ID 留空会自动生成 |
| 上传存档 / 仅游戏存档 / 单存档上限 | `[saves]` | 上限按**压缩后**大小计算 |
| **只上传有变化的内容** | `[sync] only_changed` | 默认开启 |
| 上传相册 / 上传后删除本地 | `[album]` | 默认保留本地 |
| 上传游玩记录 / 详细游玩事件 | `[playtime]` | 详细事件会上传 JSON |

所有设置都可以在应用内修改并写回 `config.ini`。

---

## 服务端接口

仓库**不包含正式后端**，只提供协议文档：

- [docs/API.md](docs/API.md) — 端点、ZIP 归档格式、TSV 列表、详细游玩记录 JSON、存储布局、安全清单、curl 联调
- [docs/INSTALL.md](docs/INSTALL.md) — 构建 / 安装 / 配置
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — 排错

核心端点：

```
GET  /api/v1/ping
PUT  /api/v1/saves?device=&title_id=&user_id=&timestamp=&title_name=
GET  /api/v1/saves?device=
GET  /api/v1/saves/latest?device=&title_id=&user_id=
PUT  /api/v1/album?device=&path=
PUT  /api/v1/playtime?device=
PUT  /api/v1/playlog?device=
```

### 本地临时后端

`tools/` 里有一个只依赖 Python 标准库的开发用后端，方便局域网联调：

```bash
python tools/dev_server.py --host 0.0.0.0 --port 8420 --token test-token-123
python tools/dev_smoke_test.py --base http://127.0.0.1:8420 --token test-token-123
```

> 仅供开发和测试，**不要直接暴露到公网**。正式部署请按 `docs/API.md` 实现 HTTPS、鉴权、原子写入和版本保留。

---

## 项目结构

```
Makefile                  # devkitPro/libnx 构建
icon.jpg                  # 256x256 应用图标
include/
  common.hpp              # 常量、全局状态、工具函数
  accounts.hpp            # 账号昵称 / 用户标签
  config.hpp              # INI 配置读写
  http.hpp                # libcurl 封装（上传源 / 下载 sink）
  saves.hpp               # 存档枚举、挂载、ZIP 打包 / 恢复
  save_sync.hpp           # 存档同步流程
  album_sync.hpp          # 相册同步流程
  playtime_sync.hpp       # 游玩记录（汇总 + 详细事件 + JSON）
  state.hpp               # 本地索引（存档 commit / 相册 mtime）
  titles.hpp              # 已安装标题 + NACP 名称缓存
  sha256.hpp              # 自带 SHA-256
  log.hpp / gfx.hpp / font.hpp / ui.hpp
source/                   # 对应实现
tools/
  dev_server.py           # 开发用临时后端
  dev_smoke_test.py       # 接口自测
  make_icon.py            # 生成图标
docs/
  API.md
  INSTALL.md
  TROUBLESHOOTING.md
```

---

## 常见问题

**Q：上传很慢？**
存档会先压缩再上传。日志里会显示 `压缩 A → B`；越大的存档收益越明显，已经压缩/加密的数据可能收益有限。相册视频一般是原始大小。

**Q：为什么列表里同一个游戏出现两次？**
可能是两个 Switch 用户各有一份存档，或者是“账号存档 + 设备存档”。恢复列表现在会显示用户昵称或「设备存档」来区分。

**Q：某个游戏备份失败，提示打不开存档文件？**
请把 `sd:/switch/cloud-sync/logs/sync.log` 里对应的那一行发到 Issues，日志包含路径、条目类型和两个 `Result` 码。拼接文件（concatenation file）已经自动处理。

**Q：会不会自动后台上传？**
当前是手动前台应用。存档挂载要求游戏未运行，做成前台最稳。想做全自动可以参考 README 历史讨论，用 sysmodule + `pm:shell` 在游戏退出后触发。

**Q：游戏时长是哪来的？**
来自系统 Play Log（`pdm:qry`），和系统“用户页”同源，不是本应用计时。系统日志是环形缓冲，旧记录会被覆盖。

**Q：界面没有文字？**
系统共享字体加载失败，看日志里的 `Fonts:` / `plInitialize`。没有字体时界面无法显示文字。

更多见 [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)。

---

## 免责声明

- 本项目与任天堂无关，未获得任天堂认可或授权。
- 仅用于备份**自己的**游戏存档和个人媒体。请遵守当地法律和任天堂服务条款，使用风险自负。
- 恢复存档会覆盖本地进度。作者不对任何数据丢失负责，请让服务端保留历史版本。
- 在大气层自制系统上运行第三方程序存在风险，请自行判断。

## 致谢

- [devkitPro](https://devkitpro.org/) / [libnx](https://github.com/switchbrew/libnx)
- [zlib](https://zlib.net/) / [minizip](https://github.com/zlib-ng/minizip-ng)
- [FreeType](https://freetype.org/)
- [NX-Activity-Log](https://github.com/tallbl0nde/NX-Activity-Log)（游玩记录导出格式参考）
- [JKSV](https://github.com/J-D-K/JKSV)（存档备份与拼接文件处理思路参考）

## License

[MIT](LICENSE)
