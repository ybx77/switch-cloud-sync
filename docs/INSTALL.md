# 构建与安装

## 1. 环境准备

需要 devkitPro 的 Switch 工具链（devkitA64 + libnx）、libcurl、FreeType：

### Windows

1. 安装 [devkitPro pacman](https://devkitpro.org/wiki/Getting_Started)
2. 打开 **devkitPro MSYS2**（`C:\devkitPro\msys2.exe`）
3. 安装包：

```bash
pacman -Syu
pacman -S --needed switch-dev switch-curl switch-freetype switch-harfbuzz switch-zlib
```

### Linux / WSL / macOS

```bash
sudo pacman -S --needed switch-dev switch-curl switch-freetype switch-harfbuzz switch-zlib
```

### 确认工具链

```bash
echo $DEVKITPRO                    # /opt/devkitpro 或 C:/devkitPro
which aarch64-none-elf-g++
curl-config --libs
ls $DEVKITPRO/portlibs/switch/lib/libfreetype.a
ls $DEVKITPRO/portlibs/switch/lib/libminizip.a
ls $DEVKITPRO/portlibs/switch/include/freetype2/ft2build.h
```

## 2. 编译

```bash
make -j$(nproc)
```

产物：

```
switch-cloud-sync.nro
```

清理：

```bash
make clean
```

### 图标

根目录的 `icon.jpg` 会被 `elf2nro` 自动打进 NRO。想换图标：

```bash
python tools/make_icon.py     # 需要 Pillow：python -m pip install pillow
make -j$(nproc)
```

### 在 WSL 里构建

```bash
wsl -d Ubuntu
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PORTLIBS=/opt/devkitpro/portlibs/switch
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PORTLIBS/bin:$PATH
cd "/mnt/c/Users/<你的用户名>/Desktop/02_project/switch云存档"
make -j$(nproc)
```

## 3. 部署到 SD 卡

```
sd:/switch/switch-cloud-sync.nro
```

第一次运行后自动创建：

```
sd:/switch/cloud-sync/
├── config.ini
├── logs/sync.log
├── state/
└── tmp/
```

## 4. 启动方式

| 方式 | 说明 |
| --- | --- |
| 相册 → hbmenu → 运行 | applet 模式，内存较小 |
| **按住 R 启动任意游戏 → hbmenu → 运行** | **全内存模式，推荐** |

第一次启动会加载系统共享字体（约 1-2 秒），之后界面即可显示中文。

## 5. 在应用内设置

首页 → **设置**，用 `A` 编辑、`X` 保存、`Y` 重新读取、`B` 返回：

| 设置 | 说明 |
| --- | --- |
| 服务器地址 | 如 `http://192.168.1.10:8420` 或 `https://cloud.example.com`，结尾不要带 `/` |
| 访问令牌 | 发送为 `Authorization: Bearer <token>` |
| 设备名称 / 设备 ID | 设备 ID 留空会自动生成 |
| CA 证书路径 | 留空时自动使用 `sdmc:/switch/cloud-sync/ca-bundle.pem` |
| 网络超时 | 空闲超时秒数（5-3600） |
| 单个存档上限 | MB，超过就跳过 |
| 只上传有变化的内容 | **默认开启**，见 README 的去重说明 |
| 上传后删除本地相册 | 默认关闭 |
| 详细游玩事件 | 默认开启，会上传 `/playlog` |
| 测试服务器连接 | 立即 ping 一次，右上角状态会同步更新 |

设置会直接写回 `config.ini`，也会从文件重新读取。

### 关于配置文件

`config.ini` 仍然可以手动编辑（例如批量改），改完在应用里点「重新读取配置文件」。

```ini
[server]
base_url = http://192.168.1.10:8420
token = test-token-123
ca_bundle =
insecure_tls = 0
timeout_sec = 60

[device]
id =
name = my-switch

[saves]
enabled = 1
games_only = 1
max_upload_mb = 256

[sync]
only_changed = 1

[album]
enabled = 1
delete_after_upload = 0

[playtime]
enabled = 1
detailed = 1
```

## 6. HTTPS 与 CA

### 公网证书

一般不用配置。

### 自签证书 / 私有 CA

```bash
openssl s_client -connect cloud.example.com:443 -showcerts </dev/null \
  | sed -n '/BEGIN CERTIFICATE/,/END CERTIFICATE/p' > ca-bundle.pem
```

复制到 `sd:/switch/cloud-sync/ca-bundle.pem` 即可自动生效（或在设置里填路径）。

### 内网调试

设置里临时打开「跳过 TLS 校验」。仅限局域网。

## 7. 首次联调

1. 设置里填好地址/令牌 → **测试服务器连接**
2. 回首页 → **一键上传全部**
3. 看进度页的统计与日志；有失败项会列出来
4. 想验证恢复：先上传一次，再进 **恢复存档** 选择时间点

## 8. 升级

替换 `sd:/switch/switch-cloud-sync.nro` 即可，`sd:/switch/cloud-sync/` 下的配置和索引会保留。

强制重新上传全部：

```
删除 sd:/switch/cloud-sync/state/saves.tsv   # 存档
删除 sd:/switch/cloud-sync/state/album.tsv   # 相册
```
