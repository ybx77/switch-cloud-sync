# Switch Cloud Sync 服务端协议 v1

这份文档是给**服务端**看的。客户端（Switch 上的 `.nro`）只依赖下面这些约定，不依赖任何框架、数据库或文件布局。你可以用 Python / Node / Go / Rust / PHP 任意实现。

- 客户端实现：`source/save_sync.cpp`、`source/album_sync.cpp`、`source/playtime_sync.cpp`、`source/http.cpp`
- 客户端配置里的 `base_url` 就是本文的 `{base}`（不含 `/api/v1`，也不要带结尾斜杠）

---

## 0. 设计原则

1. **请求体是原始字节**：文件用 PUT 原始字节，游玩记录用 JSON/TSV，不使用 multipart。
2. **列表用 TSV**，一行一条记录，客户端按 `\t` 切分；解析失败的行直接跳过。
3. **客户端不做重试**。失败就是失败，由用户再点一次，所以服务端应当原子写入，避免留下半截文件。
4. **去重是客户端行为**：存档按 `commit_id` + SHA-256，相册按 `size + mtime`，没变化时客户端根本不会发请求。服务端仍然应当对重复 PUT 保持幂等。
5. **客户端不校验服务端返回的 JSON**。除列表接口外，响应体基本被忽略，状态码才是关键。
6. 时间统一用 **Unix 秒（UTC）**。

---

## 1. 通用约定

| 项 | 值 |
| --- | --- |
| API 前缀 | `{base}/api/v1` |
| 认证 | `Authorization: Bearer <token>`（配置里 token 为空时不发送） |
| User-Agent | `SwitchCloudSync/1.0.0 (libnx)` |
| Accept | `text/plain, application/octet-stream` |
| `Expect` | 客户端主动删除了 `Expect: 100-continue`，请求一定是 `Content-Length` + 直接发 body |
| 连接超时 | 20 秒 |
| 空闲超时 | 默认 60 秒内平均速率低于 512 B/s 视为失败 |
| 最大响应体 | 列表类响应客户端最多读 8 MB |

**必须返回 `200`（不是 304/206）的接口**：`GET /saves`、`GET /saves/latest`。客户端只认 `== 200`。

`PUT` 接口接受任意 `2xx`。

### 错误响应建议

状态码表达语义，body 用纯文本，客户端会把 body 打印到屏幕/日志（建议短一些）：

```
HTTP/1.1 401 Unauthorized
Content-Type: text/plain; charset=utf-8

error=unauthorized
message=invalid device token
```

| 状态码 | 含义 |
| --- | --- |
| 200 | 成功 |
| 400 | 参数不合法（缺字段、device 为空、path 非法） |
| 401 / 403 | token 无效，或 token 与 device 不匹配 |
| 404 | 端点不存在 / 该存档不存在 |
| 409 | 冲突（可选，例如同一存档并发上传） |
| 413 | 超过服务端体积限制 |
| 500 | 服务端内部错误 |

---

## 2. 身份与标识

### device

- 客户端首次运行会生成一个稳定 ID，形如 `switch-3f2a91c4a10b77de`，保存在 SD 卡 `state/device_id.txt`
- 也可以在 `config.ini` 的 `[device] id` 里覆盖
- 所有请求都带 `device` 查询参数（URL 编码）
- 强烈建议：**一台 Switch 一个 token**，服务端校验 `token -> device`，防止 A 机覆盖 B 机

### title_id

16 位小写十六进制，例如 `0100152000022000`。就是 Switch 的 application id。

### user_id

- 账号存档：`AccountUid` 的两个 `u64` 依次拼接，各 16 位小写十六进制，共 32 字符
- 设备存档（无账号）：固定字符串 `0`

同一台机器上同一用户的 `user_id` 是稳定的，可以直接当目录名。

### title_name

游戏名（UTF-8），由客户端从 NACP 读取，**放在查询参数里并 URL 编码**。服务端不要信任它（可能为空、可能很长、可能含控制字符），落库/展示前做长度限制和转义。

---

## 3. 端点

### 3.1 `GET /api/v1/ping`

连通性检查。

**响应** `200`，`text/plain`，内容随意，例如：

```
ok switch-cloud-sync v1
```

客户端在「Status / connection test」里显示状态码和 body 的前若干字符。

---

### 3.2 `PUT /api/v1/saves` — 上传一份存档

**查询参数**

| 参数 | 必填 | 说明 |
| --- | --- | --- |
| `device` | 是 | 设备 ID |
| `title_id` | 是 | 16 位小写 hex |
| `user_id` | 是 | 32 位小写 hex，或 `0` |
| `timestamp` | 是 | 客户端生成的时间（Unix 秒） |
| `title_name` | 否 | URL 编码的 UTF-8 游戏名 |

**请求头**

| 头 | 说明 |
| --- | --- |
| `Content-Type` | `application/octet-stream` |
| `Content-Length` | 必有（客户端总是先算出文件大小） |
| `X-Content-SHA256` | 归档文件的 SHA-256，64 位小写 hex |
| `X-Device-Name` | URL 编码的设备名（可能为空字符串） |

**请求体**：ZIP 归档的原始字节，格式见第 4 节。

**服务端应该做什么**

1. 把 body **流式**写入临时文件（不要整块读进内存，存档可能有几百 MB）
2. 边写边算 SHA-256，和 `X-Content-SHA256` 比对，不一致返回 `400`
3. 原子地 `rename` 到正式位置，同时复制/硬链接一份到 `history/<timestamp>.zip`
4. 更新该 `(device,title_id,user_id)` 的 `latest` 记录（时间戳、大小、sha256、title_name）
5. 返回 `200`

**响应**：`200`，body 客户端不解析，可以返回：

```
stored=ok
sha256=...
bytes=...
```

---

### 3.3 `GET /api/v1/saves?device=<device>` — 列出该设备的最新存档

**响应** `200`，`Content-Type: text/tab-separated-values; charset=utf-8`

第一行是注释头，客户端会跳过所有以 `#` 开头的行：

```
#title_id	user_id	timestamp	size	sha256	title_name
0100152000022000	00000000000000000000000000000000	1700000000	1048576	3f2a...c4	The Legend of Zelda
```

规则：

- **每个 `(title_id, user_id)` 只返回一行**（最新版本）
- 字段顺序和数量必须是 6 个，否则客户端整行丢弃
- `size` 是归档字节数，`sha256` 是归档的 64 位小写 hex（恢复时客户端用它校验）
- `title_name` 不能含 `\t` 和换行（服务端应替换成空格）
- 响应体小（客户端上限 8 MB），几百个存档也没问题
- 没有数据时返回 200 + 只有表头，或空 body 都可以（客户端显示"服务器没有存档"）

---

### 3.4 `GET /api/v1/saves/latest?device=&title_id=&user_id=` — 下载存档

**响应** `200`，body 就是 ZIP 归档的原始字节。

要求：

- 必须返回 `200`，不要用 `304 Not Modified`（客户端不做条件请求）
- `Content-Length` 尽量给，客户端会显示进度
- 服务端返回的字节必须和列表里的 `sha256` 一致，否则客户端拒绝恢复并报 `checksum mismatch`

**响应头**（可选，客户端不解析）

```
Content-Type: application/octet-stream
Content-Length: 1048576
X-Content-SHA256: 3f2a...c4
```

不存在时返回 `404`（客户端显示 "HTTP 404 (endpoint missing, or no data on the server)"）。

---

### 3.5 `PUT /api/v1/album?device=<device>&path=<urlencoded path>` — 上传一张截图/视频

**查询参数**

| 参数 | 说明 |
| --- | --- |
| `device` | 设备 ID |
| `path` | 相册内的相对路径，URL 编码，例如 `2024%2F05%2F01%2F2024050112304567.mp4` |

**请求头**

| 头 | 说明 |
| --- | --- |
| `Content-Type` | `image/jpeg` 或 `video/mp4` |
| `Content-Length` | 必有 |
| `X-File-Size` | 文件大小（十进制） |
| `X-File-Mtime` | 文件修改时间（Unix 秒，`fsFsGetFileTimeStampRaw` 的值） |
| `X-Device-Name` | URL 编码的设备名 |

**请求体**：文件的原始字节。

**服务端应该做什么**

1. 校验 `path`：不允许以 `/` 开头、不允许 `..`、不允许 `\`、不允许空段（详见第 7 节）
2. 流式写入 `album/<path>`，先写临时文件再原子 rename
3. 同一路径重复上传视为覆盖（客户端只在 `size/mtime` 变化时才重传）
4. 返回 `200`

**响应**：

```
stored=ok
bytes=12345678
```

> 客户端默认**不删除**本地文件，所以服务端就是纯粹的增量备份。大视频请确保磁盘和上传体积限制足够。

---

### 3.6 `PUT /api/v1/playtime?device=<device>` — 上传游玩时长快照

**请求头**

```
Content-Type: text/tab-separated-values; charset=utf-8
X-Device-Name: <urlencoded>
```

**请求体**（TSV，两行注释头 + 数据行）

```
#switchcloud-playtime v1
#title_id	user_id	playtime_ns	launches	last_played	title_name
0100152000022000	0000...0000	73452000000000	42	1700000000	The Legend of Zelda
```

| 字段 | 说明 |
| --- | --- |
| `playtime_ns` | 系统 Play Log 记录的累计时长，**纳秒**（`u64`） |
| `launches` | 启动次数 |
| `last_played` | 最近一次游玩时间（Unix 秒），可能为 0 |
| `title_name` | 游戏名（已替换掉 tab/换行） |

**语义：快照，不是增量。**

客户端每次上传的是「当前系统里记录的累计值」，所以服务端应当按 `(device, title_id, user_id)` 做 **upsert + 取最大值**：

- `playtime_ns = max(旧值, 新值)`
- `launches = max(旧值, 新值)`
- `last_played = max(旧值, 新值)`（注意：系统重置过 Play Log 时新值可能更小，取 max 更安全）
- `title_name` 用最新一次的非空值

这样即使客户端重复上传旧快照、或者用户删档重来，也不会把已有数据改小。

**响应**：

```
accepted=37
```

> 这是「汇总」接口。客户端还会把更细的会话/事件上传到 `/playlog`（见 3.7），
> 两者独立，服务端可以只实现其中一个。

---

### 3.7 `PUT /api/v1/playlog?device=<device>` — 详细游玩记录（JSON）

**请求头**

```
Content-Type: application/json; charset=utf-8
X-Device-Name: <urlencoded>
```

**请求体**：与 NX-Activity-Log 的导出结构兼容。

```json
{
  "exportString": "November 03 2026, 15:20:13 UTC",
  "exportTimestamp": 1793687213,
  "exportVersion": "1.1.0",
  "source": "SwitchCloudSync",
  "device": "switch-6ae98aaef75ac5a5",
  "users": [
    {
      "id": "10001A47FB6F4C93016CE67883BBE5B1",
      "name": "ybx",
      "titles": [
        {
          "id": "010097F018538000",
          "name": "DAVE THE DIVER",
          "summary": {
            "firstPlayed": 1781022359,
            "lastPlayed": 1792939924,
            "launches": 17,
            "playtime": 83963
          },
          "events": [
            {"clockTimestamp": 1781022320, "steadyTimestamp": 2889651, "type": "Launched"},
            {"clockTimestamp": 1781022325, "steadyTimestamp": 2889656, "type": "Gained Focus"},
            {"clockTimestamp": 1781022359, "steadyTimestamp": 2889690, "type": "Account Login"},
            {"clockTimestamp": 1781023311, "steadyTimestamp": 2890642, "type": "Lost Focus"},
            {"clockTimestamp": 1781023312, "steadyTimestamp": 2890643, "type": "Account Logout"},
            {"clockTimestamp": 1781023312, "steadyTimestamp": 2890643, "type": "Closed"}
          ]
        }
      ]
    }
  ]
}
```

| 字段 | 说明 |
| --- | --- |
| `id`（user） | 32 位大写十六进制 `AccountUid` |
| `id`（title） | 16 位大写十六进制 application id |
| `summary.firstPlayed` / `lastPlayed` | Unix 秒（PosixTime），可能为 0 |
| `summary.launches` | 累计启动次数 |
| `summary.playtime` | **秒**（和 NX-Activity-Log 导出一致） |
| `events[].clockTimestamp` | 用户时钟的 Unix 秒 |
| `events[].steadyTimestamp` | 系统 steady clock 的秒数（用于计算会话时长） |
| `events[].type` | `Launched` / `Gained Focus` / `Lost Focus` / `Account Login` / `Account Logout` / `Closed` |

**语义：完整快照，覆盖写。**

- 建议原样保存到 `devices/<device>/playlog.json`（事件可能几千条，几百 KB）
- 如果想在数据库里出统计，可同时遍历 `summary`，按 `(device, title_id, user_id)` 取 max 更新，规则同 3.6
- 事件只来自系统 Play Log 的环形缓冲，旧记录会被覆盖，所以每次上传不一定包含全部历史
- 客户端默认开启，可在「设置 / 详细游玩事件」里关闭

**响应**：

```
accepted=46
```

**可选调试接口**：`GET /api/v1/playlog?device=<device>` 返回上次保存的 JSON（客户端不使用）。

---

## 4. 存档归档格式（ZIP + deflate）

客户端把存档打包成标准 **ZIP**（deflate 压缩）再上传，和 JKSV 的思路一致：上传更小，PC 上也能直接解压查看。

ZIP 里包含：

| 条目 | 说明 |
| --- | --- |
| `.__scs__/meta.txt` | 元数据，key=value（见下） |
| `.__scs__/index.txt` | 每个文件一行：`相对路径<TAB>1或0`，`1` 表示这是**拼接文件**（concatenation file，目录+archive bit） |
| 其它条目 | 存档文件本体，路径就是存档内的相对路径 |
| `以 / 结尾` 的条目 | 空目录（没有文件但游戏需要它存在） |

`meta.txt` 示例：

```
format=switchcloud-zip1
title_id=0100152000022000
user_id=00000000000000000000000000000000
title_name=The Legend of Zelda
save_data_type=1
file_count=12
```

补充：

- 条目按路径字典序写入，`meta.txt` 不含时间戳，所以**内容相同 → ZIP 字节相同 → SHA-256 相同**，客户端据此跳过重复上传
- `save_data_type`：`1` = 账号存档，`3` = 设备存档
- 压缩后的大小就是列表里的 `size`，也是服务端落盘的字节数

### 拼接文件是什么？

部分游戏（以及超过 4GB 的单文件）使用“拼接文件”：在文件系统里是个带 archive bit 的目录，但只能当文件打开。JKSV 也会识别并特殊处理。客户端：

- 备份时用 `fsFsOpenFile` 探测，成功就当文件读取
- 在 `index.txt` 里标记 `1`
- 恢复时用 `FsCreateOption_BigFile` 重新创建

### 服务端要不要解析？

- **只想备份/恢复**：不用解析，原样存储和回放即可（`timestamp/size/title_name` 用查询参数和 `Content-Length` 就能填）
- **想展示内容**：ZIP 可以直接解开，看 `.__scs__/meta.txt` 即可
- 客户端仍然能恢复**旧版 SCSA1** 归档，所以升级服务端不需要迁移历史文件

### 恢复时客户端做什么

1. 下载到 `state/tmp/restore.zip`
2. 校验 SHA-256（和列表里的值比）
3. 完整校验 ZIP（路径安全性、条目大小上限）
4. 清空目标存档目录
5. 逐文件解压写回（自动创建子目录，拼接文件用 BigFile 创建）
6. `fsFsCommit`

任何一步失败都会中止并打印原因，所以服务端**保留历史版本**很重要。

---

## 5. 建议的存储布局

```
data/
└── devices/
    └── switch-3f2a91c4a10b77de/
        ├── meta.json
        ├── saves/
        │   └── 0100152000022000/
        │       └── 00000000000000000000000000000000/
        │           ├── latest.zip
        │           └── history/
        │               ├── 1700000000.zip
        │               └── 1700003600.zip
        ├── album/
        │   └── 2024/05/01/2024050112304567.mp4
        ├── playtime.tsv
        └── playlog.json
```

如果要上数据库，推荐两张表 + 文件存储：

```sql
-- 存档索引
CREATE TABLE saves (
  device      TEXT NOT NULL,
  title_id    TEXT NOT NULL,
  user_id     TEXT NOT NULL,
  timestamp   INTEGER NOT NULL,
  size        INTEGER NOT NULL,
  sha256      TEXT NOT NULL,
  title_name  TEXT,
  path        TEXT NOT NULL,   -- 文件系统里的相对路径
  PRIMARY KEY (device, title_id, user_id)
);

-- 时长
CREATE TABLE playtime (
  device       TEXT NOT NULL,
  title_id     TEXT NOT NULL,
  user_id      TEXT NOT NULL,
  playtime_ns  INTEGER NOT NULL,
  launches     INTEGER NOT NULL,
  last_played  INTEGER NOT NULL,
  title_name   TEXT,
  PRIMARY KEY (device, title_id, user_id)
);
```

相册直接用文件系统 + 路径即可，不需要入库。

---

## 6. 实现步骤（照这个顺序写）

1. 起一个 HTTP 服务，挂 6 个路由：`ping / saves PUT / saves GET / saves/latest / album PUT / playtime PUT`
2. 写认证中间件：读 `Authorization: Bearer`，校验 token，并且把 token 绑定到 `device`
3. 写一个"流式落盘"的辅助函数：`request_body -> tmp 文件 + 算 sha256 + 原子 rename`（saves 和 album 共用）
4. `PUT /saves`：落盘 → 校验 `X-Content-SHA256` → 更新索引与 history
5. `GET /saves`：读索引，输出 6 列 TSV（每个 title/user 一行）
6. `GET /saves/latest`：按索引路径回放文件，`200` + `application/octet-stream`
7. `PUT /album`：校验 path → 落盘到 `album/<path>`
8. `PUT /playtime`：解析 TSV → 按 max 语义 upsert
9. `PUT /playlog`：接收 JSON 快照，原样落盘；建议同时按 `summary` 更新汇总表
10. 用第 8 节的 curl 命令逐个验证
11. 最后在 Switch 上按「设置 → 测试服务器连接 → 一键上传全部 → 恢复存档」顺序联调

---

## 7. 安全与健壮性清单

**认证**

- 每台设备一个 token，服务端建立 `token -> device` 映射；请求里的 `device` 和 token 不匹配返回 `403`
- token 比较用常量时间比较，别用 `==` 直接比（至少避免明显的时序侧信道）
- 不要把 token 写进日志

**路径安全（album 的 `path` 参数）**

- 先做 URL 解码，再校验
- 拒绝：以 `/` 开头、包含 `..` 段、包含 `\`、包含 NUL、空段、超过 1024 字节
- 最终路径必须落在 `data/devices/<device>/album/` 之内（用 `realpath` / `canonicalize` 再前缀比较）
- 不要允许覆盖 `.zip`、索引文件等内部文件（把相册放在独立子目录即可）

**体积与资源**

- 流式处理，禁止把请求体整体读进内存
- 限制单文件大小与单设备总配额（存档默认上限客户端配置为 256 MB，视频可能上 GB）
- 限制并发上传数，防止被打满磁盘
- 磁盘写满时返回 `507` 或 `500`，并保证不破坏已有文件（先写 tmp 再 rename）

**一致性**

- 所有写入都走 `tmp -> fsync -> rename`
- 存档按 `(device, title_id, user_id)` 加锁，避免两个并发上传互相覆盖
- 保留历史版本（推荐最近 10 个或最近 30 天），恢复前用户还有退路
- 定期校验 `latest.zip` 的 sha256，和索引比对

**传输**

- 公网必须 HTTPS（客户端支持自定义 CA PEM，自签证书也能用）
- 反向代理注意放开请求体大小（nginx 的 `client_max_body_size`）
- 客户端删除了 `Expect: 100-continue`，不需要额外配置，但代理也不能强制 chunked

**展示**

- `title_name`、`path` 都是客户端输入，落库/展示前做长度限制与转义
- 列表接口避免返回超大结果（客户端上限 8 MB）

---

## 8. curl 联调命令

```bash
BASE=https://cloud.example.com
TOKEN=your-token
DEV=switch-3f2a91c4a10b77de

# 1) 连通性
curl -i -H "Authorization: Bearer $TOKEN" "$BASE/api/v1/ping"

# 2) 上传存档（--data-binary 保持原始字节）
SHA=$(sha256sum sample.zip | cut -d' ' -f1)
curl -i -X PUT \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  -H "X-Content-SHA256: $SHA" \
  --data-binary @sample.zip \
  "$BASE/api/v1/saves?device=$DEV&title_id=0100152000022000&user_id=00000000000000000000000000000000&timestamp=1700000000&title_name=Test%20Game"

# 3) 列表（必须是 200 + TSV）
curl -i -H "Authorization: Bearer $TOKEN" "$BASE/api/v1/saves?device=$DEV"

# 4) 下载最新存档
curl -i -H "Authorization: Bearer $TOKEN" -o restore.zip \
  "$BASE/api/v1/saves/latest?device=$DEV&title_id=0100152000022000&user_id=00000000000000000000000000000000"

# 5) 上传相册文件（path 要 URL 编码）
curl -i -X PUT \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: video/mp4" \
  -H "X-File-Size: $(stat -c%s clip.mp4)" \
  -H "X-File-Mtime: 1700000000" \
  --data-binary @clip.mp4 \
  "$BASE/api/v1/album?device=$DEV&path=2024%2F05%2F01%2F2024050112304567.mp4"

# 6) 上传时长汇总
printf '#switchcloud-playtime v1\n#title_id\tuser_id\tplaytime_ns\tlaunches\tlast_played\ttitle_name\n0100152000022000\t00000000000000000000000000000000\t73452000000000\t42\t1700000000\tTest Game\n' \
  | curl -i -X PUT -H "Authorization: Bearer $TOKEN" \
      -H "Content-Type: text/tab-separated-values; charset=utf-8" \
      --data-binary @- "$BASE/api/v1/playtime?device=$DEV"

# 7) 上传详细游玩记录（JSON）
curl -i -X PUT \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json; charset=utf-8" \
  --data-binary @playlog.json \
  "$BASE/api/v1/playlog?device=$DEV"
```

自测通过后，在 Switch 上依次点：

1. **设置 → 测试服务器连接** → 右上角变绿、toast 显示 `HTTP 200`
2. **上传游玩记录** → 纯文本 + JSON，最容易验证
3. **上传存档** → 会打印每个存档的结果；第二次运行应大量显示「跳过（未变化）」
4. **恢复存档** → 先确认服务端保留了历史版本

---

## 9. 版本演进

- 端点前缀固定为 `/api/v1`；破坏性变更请开 `/api/v2`，客户端会在 `base_url` 里带上前缀
- 归档是标准 ZIP；客户端仍能读取旧的 `SCSA1` 归档，新上传一律用 ZIP
- 列表 TSV 增加列时，**追加到末尾**；客户端要求恰好 6 列，所以真正加列需要新端点或新版本
- playtime 的 TSV 头行带 `v1`，服务端可以据此兼容
