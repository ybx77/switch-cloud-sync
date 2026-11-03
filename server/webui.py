"""Server-rendered admin UI (stdlib only)."""

from __future__ import annotations

import html
import time
from urllib.parse import quote

APP_NAME = "Switch 云存档"
APP_VERSION = "1.1.0"
PROJECT_URL = "https://github.com/ybx77/switch-cloud-sync"

CSS = """
:root{--bg:#0f1115;--panel:#171c24;--panel2:#1d2430;--line:#2a3648;--text:#e8edf6;
--muted:#93a1b8;--accent:#4c7dff;--green:#35c48a;--red:#ff6b6b;--amber:#ffbe5a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
font:15px/1.55 -apple-system,"Segoe UI",Roboto,"Noto Sans SC","Microsoft YaHei",sans-serif}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
.shell{display:flex;min-height:100vh}
.sidebar{width:232px;background:#12161d;border-right:1px solid var(--line);padding:20px 14px;
position:sticky;top:0;height:100vh}
.brand{display:flex;gap:10px;align-items:center;font-size:16px;margin:0 8px 22px}
.brand b{font-weight:600}
nav a{display:flex;gap:10px;padding:10px 12px;border-radius:10px;color:var(--muted);
margin-bottom:4px;font-size:14px}
nav a.on{background:#1e2635;color:var(--text)}
nav a:hover{background:#1a212c;text-decoration:none;color:var(--text)}
.side-foot{position:absolute;bottom:18px;left:22px;font-size:12px;color:var(--muted)}
.main{flex:1;min-width:0;padding:26px 30px 70px}
.topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:22px;gap:16px}
h1{font-size:24px;margin:0}h2{font-size:17px;margin:26px 0 12px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(165px,1fr));gap:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px 18px}
.card .k{color:var(--muted);font-size:13px}
.card .v{font-size:24px;font-weight:600;margin-top:6px}
.card .s{color:var(--muted);font-size:12px;margin-top:4px}
table{width:100%;border-collapse:collapse;background:var(--panel);border:1px solid var(--line);
border-radius:12px;overflow:hidden}
th,td{padding:10px 12px;text-align:left;border-bottom:1px solid var(--line);font-size:14px;
vertical-align:middle}
th{color:var(--muted);font-weight:500;font-size:13px;background:#141922}
tr:last-child td{border-bottom:none}
tr:hover td{background:#1a212c}
.badge{display:inline-block;padding:2px 8px;border-radius:20px;font-size:12px;
background:#243049;color:#b9c8e8}
.badge.green{background:#153b2e;color:#6ee7b7}
.badge.red{background:#46242a;color:#ffb4b4}
.mono{font-family:ui-monospace,Consolas,monospace}
.muted{color:var(--muted)}
.small{font-size:12px}
.empty{padding:40px;text-align:center;color:var(--muted);background:var(--panel);
border:1px dashed var(--line);border-radius:12px}
.btn{display:inline-block;background:var(--accent);color:#fff;padding:9px 16px;border-radius:10px;
border:none;cursor:pointer;font-size:14px}
.btn:hover{filter:brightness(1.08);text-decoration:none}
.btn:disabled{opacity:.45;cursor:not-allowed;filter:none}
input:disabled{opacity:.5}
.btn.ghost{background:transparent;border:1px solid var(--line);color:var(--text)}
.btn.small{padding:5px 10px;font-size:13px;border-radius:8px}
.btn.danger{background:#8f2b33}
.field{display:flex;flex-direction:column;gap:6px;margin-bottom:14px}
input,select{background:#12161d;border:1px solid var(--line);color:var(--text);
padding:10px 12px;border-radius:10px;font-size:14px;outline:none}
input:focus,select:focus{border-color:var(--accent)}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.copybox{background:#12161d;border:1px solid var(--line);border-radius:10px;padding:12px 14px;
display:flex;justify-content:space-between;gap:10px;align-items:center;margin:6px 0}
code{font-family:ui-monospace,Consolas,monospace;font-size:13px;word-break:break-all}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:14px}
.thumb{background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden}
.thumb img{width:100%;height:132px;object-fit:cover;display:block;background:#000}
.thumb .ph{height:132px;display:flex;align-items:center;justify-content:center;font-size:36px;
background:#11151c;color:#41506b}
.thumb .meta{padding:9px 11px;font-size:12px;color:var(--muted)}
.thumb .name{font-size:13px;color:var(--text);margin-bottom:4px;word-break:break-all}
.pager{display:flex;gap:8px;margin-top:16px;align-items:center}
.login-wrap{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px}
.login-card{width:100%;max-width:430px;background:var(--panel);border:1px solid var(--line);
border-radius:18px;padding:30px}
.steps{display:flex;flex-direction:column;gap:18px;max-width:900px;margin:0 auto}
.step{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:20px 22px}
.step h3{margin:0 0 12px;font-size:16px}
.step .n{display:inline-flex;width:24px;height:24px;border-radius:50%;background:var(--accent);
color:#fff;align-items:center;justify-content:center;font-size:13px;margin-right:8px}
.dot{width:9px;height:9px;border-radius:50%;display:inline-block;margin-right:8px;
background:var(--amber);vertical-align:middle}
.dot.ok{background:var(--green)}.dot.bad{background:var(--red)}
.pulse{animation:pulse 1.4s infinite}
@keyframes pulse{0%{opacity:.35}50%{opacity:1}100%{opacity:.35}}
.notice{padding:10px 14px;border-radius:10px;background:#20304d;color:#cfe0ff;
border:1px solid #2f4570;margin:12px 0;font-size:14px}
.notice.bad{background:#3c2126;border-color:#5c2b33;color:#ffc9c9}
.notice.ok{background:#16382c;border-color:#225a45;color:#9be9c6}
.bar{height:8px;border-radius:6px;background:#232c3a;overflow:hidden;min-width:80px}
.bar i{display:block;height:100%;background:var(--accent)}
.scroll{max-height:520px;overflow:auto;border-radius:12px}
.media{max-width:100%;border-radius:14px;background:#000;display:block;margin:0 auto}
.chart{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px 20px;
margin-bottom:16px}
.chart h3{margin:0 0 14px;font-size:15px;color:var(--muted);font-weight:500}
.chart-row{display:grid;grid-template-columns:190px 1fr 96px;gap:12px;align-items:center;
margin:9px 0}
.chart-label{font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chart-track{height:14px;background:#12161d;border-radius:7px;overflow:hidden}
.chart-fill{height:100%;border-radius:7px;
background:linear-gradient(90deg,#4c7dff,#59c7a5)}
.chart-value{font-size:13px;color:var(--muted);text-align:right;
font-variant-numeric:tabular-nums}
.cols{display:flex;gap:8px;align-items:flex-end;height:170px;padding:8px 2px 0}
.col{flex:1;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;
gap:6px;min-width:0}
.col .bar2{width:100%;max-width:34px;background:linear-gradient(180deg,#4c7dff,#59c7a5);
border-radius:6px 6px 0 0;min-height:2px}
.col .val{font-size:10px;color:var(--text)}
.col .lbl{font-size:10px;color:var(--muted);white-space:nowrap}
.foot-link{margin-top:18px;font-size:12px;color:var(--muted);text-align:center}
.foot-link a{color:var(--muted);text-decoration:underline}
@media(max-width:900px){.shell{display:block}.sidebar{width:auto;height:auto;position:static}
.main{padding:18px}.side-foot{display:none}}
"""

NAV = [
    ("dashboard", "概览", "◫"),
    ("devices", "设备", "▣"),
    ("saves", "存档", "⛁"),
    ("album", "相册", "▤"),
    ("playtime", "游玩时长", "⏱"),
    ("playlog", "游玩事件", "☷"),
    ("settings", "对接设置", "⚙"),
]

FAVICON = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
    '<rect width="64" height="64" rx="14" fill="#20345f"/>'
    '<path d="M18 40a10 10 0 0 1 2-19.8A13 13 0 0 1 45 22a9 9 0 0 1 1 18H18z" fill="#eef4ff"/>'
    '<path d="M27 33l5-5 5 5M32 28v9" stroke="#1c7460" stroke-width="3.2" '
    'fill="none" stroke-linecap="round" stroke-linejoin="round"/>'
    "</svg>"
)


def h(value) -> str:
    return html.escape("" if value is None else str(value), quote=True)


def q(value: str) -> str:
    return quote(str(value), safe="")


def fmt_size(n) -> str:
    n = float(n or 0)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return ("%d %s" % (n, unit)) if unit == "B" else ("%.1f %s" % (n, unit))
        n /= 1024.0
    return "0 B"


def fmt_time(ts) -> str:
    try:
        ts = int(ts)
    except Exception:
        return "-"
    if ts <= 0:
        return "-"
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(ts))


def fmt_hours(ns) -> str:
    try:
        ns = int(ns)
    except Exception:
        return "-"
    minutes = ns // 60_000_000_000
    hours, mins = divmod(minutes, 60)
    if hours >= 24:
        days, hours = divmod(hours, 24)
        return "%d 天 %d 小时" % (days, hours)
    if hours:
        return "%d 小时 %02d 分" % (hours, mins)
    return "%d 分钟" % mins


def user_label(uid: str, name: str = "") -> str:
    if not uid or uid == "0":
        return "设备存档"
    if name:
        return name
    return "用户 " + (uid[-6:] if len(uid) > 6 else uid)


def short_sha(sha: str) -> str:
    return (sha or "")[:10]


def csrf_field(csrf: str) -> str:
    return '<input type="hidden" name="csrf" value="%s">' % h(csrf)


def admin_layout(active: str, title: str, body: str, user: str, csrf: str) -> str:
    nav = []
    for key, label, icon in NAV:
        cls = " class=\"on\"" if key == active else ""
        nav.append('<a href="/%s"%s><span>%s</span>%s</a>' % (key, cls, icon, h(label)))
    return """<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s · %s</title><link rel="icon" href="/favicon.svg"><style>%s</style></head>
<body><div class="shell">
<aside class="sidebar">
  <div class="brand"><img src="/favicon.svg" width="26" height="26" alt=""><b>%s</b></div>
  <nav>%s</nav>
  <div class="side-foot">v%s · 服务端<br>
    <a href="%s" target="_blank" rel="noopener">GitHub 开源项目</a></div>
</aside>
<main class="main">
  <div class="topbar"><h1>%s</h1>
    <div class="row">
      <span class="muted small">%s</span>
      <form method="post" action="/logout" style="margin:0">%s
        <button class="btn ghost small" type="submit">退出</button>
      </form>
    </div>
  </div>
  %s
</main></div></body></html>""" % (
        h(APP_NAME), h(title), CSS, h(APP_NAME), "".join(nav), APP_VERSION,
        h(PROJECT_URL), h(title), h(user), csrf_field(csrf), body,
    )


def simple_layout(title: str, body: str) -> str:
    return """<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s · %s</title><link rel="icon" href="/favicon.svg"><style>%s</style></head>
<body>%s
<div class="foot-link">%s v%s · <a href="%s" target="_blank" rel="noopener">GitHub 开源项目</a></div>
</body></html>""" % (h(APP_NAME), h(title), CSS, body, h(APP_NAME), APP_VERSION, h(PROJECT_URL))


def flash(message: str, kind: str = "ok") -> str:
    if not message:
        return ""
    cls = "notice ok" if kind == "ok" else ("notice bad" if kind == "bad" else "notice")
    return '<div class="%s">%s</div>' % (cls, h(message))


# ---------------------------------------------------------------------------
# setup / login
# ---------------------------------------------------------------------------

def setup_page(base_urls, token: str, connected: bool, device, admin_done: bool,
               origin_warning: str = "") -> str:
    primary = base_urls[0] if base_urls else "http://<本机IP>:8420"
    others = base_urls[1:]
    dev_html = ""
    if connected and device:
        dev_html = (
            '<div class="notice ok"><span class="dot ok"></span>已检测到客户端：'
            "<b>%s</b> %s · %s</div>"
            % (h(device["id"]), ("(" + h(device["name"]) + ")") if device["name"] else "",
               h(fmt_time(device["last_seen"])))
        )
    elif connected:
        dev_html = '<div class="notice ok"><span class="dot ok"></span>已检测到客户端连接</div>'
    else:
        dev_html = ('<div class="notice"><span class="dot pulse"></span>'
                    "等待客户端连接…（在 Switch 应用里点「测试服务器连接」或上传任意数据）</div>")

    disabled = "" if connected else " disabled"
    origin = ('<div class="notice bad">%s</div>' % h(origin_warning)) if origin_warning else ""

    alt = ""
    if others:
        alt = ('<div class="muted small" style="margin-top:8px">其他可用地址：%s</div>'
               % "、".join("<code>%s</code>" % h(u) for u in others))

    return simple_layout("初次设置", """
<div class="login-wrap"><div style="width:100%%;max-width:920px">
  <div style="text-align:center;margin-bottom:22px">
    <img src="/favicon.svg" width="52" height="52" alt="">
    <h1 style="margin:12px 0 4px">%s 服务端</h1>
    <div class="muted">首次使用：先让 Switch 客户端连上，再设置管理员账号</div>
  </div>
  %s
  <div class="steps">
    <div class="step">
      <h3><span class="n">1</span>在 Switch 应用里填写对接信息</h3>
      <div class="muted small">打开应用 → 设置，填入下面两项，然后保存。</div>
      <div class="copybox"><div><div class="muted small">服务器地址</div>
        <code id="base">%s</code></div>
        <button class="btn ghost small" onclick="copyText('base')">复制</button></div>
      %s
      <div class="copybox"><div><div class="muted small">访问令牌</div>
        <code id="token">%s</code></div>
        <button class="btn ghost small" onclick="copyText('token')">复制</button></div>
      <div class="muted small">输入时不区分大小写，横线可以省略。访问令牌等同于密码，请勿公开；管理页里可以随时重置。</div>
    </div>
    <div class="step">
      <h3><span class="n">2</span>等待客户端连接</h3>
      <div id="status">%s</div>
      <div class="muted small" style="margin-top:8px">本页面每 2 秒自动检查一次。</div>
    </div>
    <div class="step">
      <h3><span class="n">3</span>设置管理员账号</h3>
      <div id="adminHint" class="muted small" style="margin-bottom:10px">%s</div>
      <form method="post" action="/setup/admin" id="adminForm">
        <div class="row" style="align-items:flex-end">
          <div class="field" style="flex:1;min-width:180px"><label>用户名</label>
            <input name="username" minlength="3" maxlength="32" required%s></div>
          <div class="field" style="flex:1;min-width:180px"><label>密码（至少 8 位）</label>
            <input name="password" type="password" minlength="8" required%s></div>
          <div class="field" style="flex:1;min-width:180px"><label>确认密码</label>
            <input name="password2" type="password" minlength="8" required%s></div>
          <div class="field"><button class="btn" type="submit"%s>创建并进入管理</button></div>
        </div>
      </form>
    </div>
  </div>
</div></div>
<script>
function copyText(id){const el=document.getElementById(id);const t=el.textContent;
  navigator.clipboard.writeText(t).then(()=>{el.style.color='#35c48a';
  setTimeout(()=>el.style.color='',900);}).catch(()=>{});}
let connected=%s;
async function poll(){
  try{const r=await fetch('/setup/status',{cache:'no-store'});const j=await r.json();
    if(j.admin){location.href='/login';return;}
    const st=document.getElementById('status');
    if(j.connected){
      st.innerHTML='<div class="notice ok"><span class="dot ok"></span>已检测到客户端连接：<b>'
        +(j.device_id||'')+'</b> '+(j.device_name?('('+j.device_name+')'):'')+'</div>';
      document.querySelectorAll('#adminForm input,#adminForm button').forEach(e=>e.disabled=false);
      document.getElementById('adminHint').textContent='客户端已连接，可以创建管理员账号了。';
    } else {
      st.innerHTML='<div class="notice"><span class="dot pulse"></span>等待客户端连接…</div>';
    }
  }catch(e){}
  setTimeout(poll,2000);
}
poll();
</script>
""" % (h(APP_NAME), origin, h(primary), alt, h(token), dev_html,
       "客户端连接后即可设置" if not connected else "客户端已连接，可以创建管理员账号了。",
       disabled, disabled, disabled, disabled, "true" if connected else "false"))


def login_page(error: str = "") -> str:
    return simple_layout("登录", """
<div class="login-wrap"><div class="login-card">
  <div style="text-align:center;margin-bottom:20px">
    <img src="/favicon.svg" width="46" height="46" alt="">
    <h1 style="margin:12px 0 4px;font-size:22px">管理登录</h1>
    <div class="muted small">%s 服务端</div>
  </div>
  %s
  <form method="post" action="/login">
    <div class="field"><label>用户名</label><input name="username" autofocus required></div>
    <div class="field"><label>密码</label><input name="password" type="password" required></div>
    <button class="btn" style="width:100%%" type="submit">登录</button>
  </form>
</div></div>""" % (h(APP_NAME), ('<div class="notice bad">%s</div>' % h(error)) if error else ""))


# ---------------------------------------------------------------------------
# dashboard / devices / saves
# ---------------------------------------------------------------------------

def stat_card(key: str, value: str, sub: str = "") -> str:
    return ('<div class="card"><div class="k">%s</div><div class="v">%s</div>'
            '%s</div>') % (h(key), h(value), ('<div class="s">%s</div>' % h(sub)) if sub else "")


def chart_bars(title: str, items, value_fn, empty_text="暂无数据") -> str:
    """items: iterable of (label, numeric value)."""
    items = [(str(label), float(value or 0)) for label, value in items]
    items = [it for it in items if it[1] > 0]
    if not items:
        return '<div class="chart"><h3>%s</h3><div class="muted small">%s</div></div>' % (
            h(title), h(empty_text))
    maxv = max(v for _, v in items) or 1.0
    rows = []
    for label, value in items:
        pct = max(1, int(round(value * 100.0 / maxv)))
        rows.append(
            '<div class="chart-row"><div class="chart-label" title="%s">%s</div>'
            '<div class="chart-track"><div class="chart-fill" style="width:%d%%"></div></div>'
            '<div class="chart-value">%s</div></div>'
            % (h(label), h(label), pct, h(value_fn(value))))
    return '<div class="chart"><h3>%s</h3>%s</div>' % (h(title), "".join(rows))


def chart_columns(title: str, points, value_fn, empty_text="暂无数据") -> str:
    """points: iterable of (label, numeric value); vertical bars."""
    points = [(str(label), float(value or 0)) for label, value in points]
    if not points or max((v for _, v in points), default=0) <= 0:
        return '<div class="chart"><h3>%s</h3><div class="muted small">%s</div></div>' % (
            h(title), h(empty_text))
    maxv = max(v for _, v in points) or 1.0
    cols = []
    for label, value in points:
        height = max(2, int(round(value * 120.0 / maxv)))
        cols.append(
            '<div class="col"><div class="val">%s</div>'
            '<div class="bar2" style="height:%dpx"></div>'
            '<div class="lbl">%s</div></div>'
            % (h(value_fn(value)), height, h(label)))
    return '<div class="chart"><h3>%s</h3><div class="cols">%s</div></div>' % (
        h(title), "".join(cols))


def dashboard_page(stats, devices, activity, top_play, csrf) -> str:
    cards = "".join([
        stat_card("设备", str(stats["devices"])),
        stat_card("存档", str(stats["saves"]), fmt_size(stats["saves_bytes"])),
        stat_card("存档历史版本", str(stats["save_versions"])),
        stat_card("相册", str(stats["album"]), fmt_size(stats["album_bytes"])),
        stat_card("游玩总时长", fmt_hours(stats["playtime_ns"])),
        stat_card("最近活动", fmt_time(stats["last_activity"])),
    ])

    act_rows = []
    for item in activity:
        kind = "存档" if item["kind"] == "save" else "相册"
        target = ""
        if item["kind"] == "save":
            target = '<a href="/saves/history?device=%s&title_id=%s&user_id=%s">%s</a>' % (
                q(item["device"]), q(item.get("title_id", "")), q(item["user_id"]),
                h(item["name"]))
        else:
            target = '<a href="/album/view?device=%s&path=%s">%s</a>' % (
                q(item["device"]), q(item["name"]), h(item["name"]))
        act_rows.append(
            "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
            % (kind, h(item["device"]), target, h(fmt_time(item["ts"]))))
    if not act_rows:
        act_rows.append('<tr><td colspan="4" class="muted">还没有上传记录</td></tr>')

    dev_rows = []
    for d in devices[:8]:
        dev_rows.append(
            "<tr><td class=\"mono\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
            % (h(d["id"]), h(d["name"] or "-"), h(d["ip"] or "-"),
               h(d["version"] or "-"), h(fmt_time(d["last_seen"]))))
    if not dev_rows:
        dev_rows.append('<tr><td colspan="5" class="muted">还没有设备连接</td></tr>')

    max_ns = max([int(r["playtime_ns"]) for r in top_play], default=1) or 1
    play_rows = []
    for r in top_play:
        pct = int(int(r["playtime_ns"]) * 100 / max_ns)
        play_rows.append(
            "<tr><td>%s</td><td class=\"muted\">%s</td><td>%s</td>"
            '<td style="width:30%%"><div class="bar"><i style="width:%d%%"></i></div></td></tr>'
            % (h(r["title_name"] or r["title_id"]),
               h(user_label(r["user_id"], r["user_name"] if "user_name" in r.keys() else "")),
               h(fmt_hours(r["playtime_ns"])), pct))
    if not play_rows:
        play_rows.append('<tr><td colspan="4" class="muted">还没有游玩记录</td></tr>')

    body = """
<div class="cards">%s</div>
<h2>最近上传</h2>
<table><thead><tr><th>类型</th><th>设备</th><th>内容</th><th>时间</th></tr></thead>
<tbody>%s</tbody></table>
<h2>设备</h2>
<table><thead><tr><th>设备 ID</th><th>名称</th><th>IP</th><th>客户端</th><th>最近连接</th></tr></thead>
<tbody>%s</tbody></table>
<h2>游玩时长 Top</h2>
<table><thead><tr><th>游戏</th><th>用户</th><th>时长</th><th></th></tr></thead>
<tbody>%s</tbody></table>
<a class="btn ghost" href="/devices">查看全部设备</a>
""" % (cards, "".join(act_rows), "".join(dev_rows), "".join(play_rows))
    return admin_layout("dashboard", "概览", body, "管理员", csrf)


def devices_page(devices, csrf) -> str:
    rows = []
    for d in devices:
        rows.append(
            "<tr><td class=\"mono\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
            % (h(d["id"]), h(d["name"] or "-"), h(d["ip"] or "-"),
               h(d["version"] or "-"), h(fmt_time(d["last_seen"]))))
    if not rows:
        rows.append('<tr><td colspan="5" class="muted">还没有设备连接</td></tr>')
    body = ('<table><thead><tr><th>设备 ID</th><th>名称</th><th>IP</th><th>客户端版本</th>'
            '<th>最近连接</th></tr></thead><tbody>%s</tbody></table>' % "".join(rows))
    return admin_layout("devices", "设备", body, "管理员", csrf)


def device_filter(devices, current, action: str) -> str:
    opts = ['<option value="">全部设备</option>']
    for d in devices:
        sel = " selected" if d["id"] == current else ""
        label = d["id"] + ((" · " + d["name"]) if d["name"] else "")
        opts.append('<option value="%s"%s>%s</option>' % (h(d["id"]), sel, h(label)))
    return ('<form method="get" action="%s" class="row" style="margin-bottom:14px">'
            '<select name="device" onchange="this.form.submit()">%s</select>'
            '<noscript><button class="btn small">筛选</button></noscript></form>'
            % (action, "".join(opts)))


def saves_page(saves, devices, current, csrf) -> str:
    rows = []
    for s in saves:
        rows.append(
            "<tr><td><a href=\"/saves/history?device=%s&title_id=%s&user_id=%s\">%s</a>"
            '<div class="muted small mono">%s</div></td>'
            "<td>%s</td><td>%s</td><td>%s</td><td class=\"mono small\">%s</td>"
            '<td><a class="btn ghost small" href="/saves/download?device=%s&title_id=%s&user_id=%s">'
            "下载</a></td></tr>"
            % (q(s["device"]), q(s["title_id"]), q(s["user_id"]),
               h(s["title_name"] or s["title_id"]), h(s["title_id"]),
               h(user_label(s["user_id"], s["user_name"] if "user_name" in s.keys() else "")),
               h(fmt_size(s["size"])), h(fmt_time(s["timestamp"])), h(short_sha(s["sha256"])),
               q(s["device"]), q(s["title_id"]), q(s["user_id"])))
    if not rows:
        rows.append('<tr><td colspan="6" class="muted">还没有存档</td></tr>')
    body = (device_filter(devices, current, "/saves") +
            '<table><thead><tr><th>游戏</th><th>用户</th><th>大小</th><th>上传时间</th>'
            '<th>SHA-256</th><th></th></tr></thead><tbody>%s</tbody></table>' % "".join(rows))
    return admin_layout("saves", "存档", body, "管理员", csrf)


def history_page(save, versions, csrf) -> str:
    rows = []
    for v in versions:
        rows.append(
            "<tr><td>%s</td><td>%s</td><td class=\"mono small\">%s</td>"
            '<td><a class="btn ghost small" '
            'href="/saves/download?device=%s&title_id=%s&user_id=%s&version=%d">下载</a></td></tr>'
            % (h(fmt_time(v["timestamp"])), h(fmt_size(v["size"])), h(v["sha256"][:16]),
               q(save["device"]), q(save["title_id"]), q(save["user_id"]), int(v["timestamp"])))
    if not rows:
        rows.append('<tr><td colspan="4" class="muted">没有历史版本</td></tr>')
    body = """
<div class="card" style="margin-bottom:16px">
  <div class="muted small">%s · %s</div>
  <div style="font-size:20px;margin:4px 0">%s</div>
  <div class="muted">最新：%s · %s</div>
  <div style="margin-top:10px"><a class="btn small" href="/saves/download?device=%s&title_id=%s&user_id=%s">下载最新</a>
  <a class="btn ghost small" href="/saves?device=%s">返回存档</a></div>
</div>
<h2>历史版本</h2>
<table><thead><tr><th>时间</th><th>大小</th><th>SHA-256</th><th></th></tr></thead>
<tbody>%s</tbody></table>
""" % (h(save["device"]), h(user_label(save["user_id"])),
       h(save["title_name"] or save["title_id"]), h(fmt_time(save["timestamp"])),
       h(fmt_size(save["size"])), q(save["device"]), q(save["title_id"]), q(save["user_id"]),
       q(save["device"]), "".join(rows))
    return admin_layout("saves", "存档详情", body, "管理员", csrf)


# ---------------------------------------------------------------------------
# album
# ---------------------------------------------------------------------------

def album_page(items, devices, current, kind, page_no, pages, total, csrf) -> str:
    cards = []
    for it in items:
        base = "/album/file?device=%s&path=%s" % (q(it["device"]), q(it["path"]))
        view = "/album/view?device=%s&path=%s" % (q(it["device"]), q(it["path"]))
        if it["kind"] == "image":
            media = '<a href="%s"><img loading="lazy" src="%s" alt=""></a>' % (h(view), h(base))
        else:
            media = ('<a href="%s"><div class="ph">🎬</div></a>' % h(view))
        cards.append(
            '<div class="thumb">%s<div class="meta"><div class="name">%s</div>'
            '<div class="mono small" style="color:#6f7f9a">%s</div>'
            '%s · %s</div></div>'
            % (media, h(it["path"].split("/")[-1]),
               h("/".join(it["path"].split("/")[:-1]) or "-"),
               h(fmt_size(it["size"])), h(fmt_time(it["mtime"]))))
    if not cards:
        cards.append('<div class="empty">还没有相册文件</div>')

    kind_opts = []
    for key, label in (("all", "全部"), ("image", "图片"), ("video", "视频")):
        sel = " selected" if kind == key else ""
        kind_opts.append('<option value="%s"%s>%s</option>' % (key, sel, label))

    pager = []
    def page_link(n, label):
        return ('<a class="btn ghost small" href="/album?device=%s&kind=%s&page=%d">%s</a>'
                % (q(current), q(kind), n, label))
    if page_no > 1:
        pager.append(page_link(page_no - 1, "上一页"))
    pager.append('<span class="muted small">第 %d / %d 页 · 共 %d 个</span>'
                 % (page_no, pages, total))
    if page_no < pages:
        pager.append(page_link(page_no + 1, "下一页"))

    body = """
<div class="row" style="margin-bottom:14px">
  <form method="get" action="/album" class="row">
    <select name="device" onchange="this.form.submit()">%s</select>
    <select name="kind" onchange="this.form.submit()">%s</select>
    <noscript><button class="btn small">筛选</button></noscript>
  </form>
</div>
<div class="grid">%s</div>
<div class="pager">%s</div>
""" % (device_options(devices, current), "".join(kind_opts), "".join(cards), "".join(pager))
    return admin_layout("album", "相册", body, "管理员", csrf)


def device_options(devices, current) -> str:
    opts = ['<option value="">全部设备</option>']
    for d in devices:
        sel = " selected" if d["id"] == current else ""
        label = d["id"] + ((" · " + d["name"]) if d["name"] else "")
        opts.append('<option value="%s"%s>%s</option>' % (h(d["id"]), sel, h(label)))
    return "".join(opts)


def album_view_page(item, csrf) -> str:
    src = "/album/file?device=%s&path=%s" % (q(item["device"]), q(item["path"]))
    if item["kind"] == "image":
        media = '<img class="media" src="%s" alt="">' % h(src)
    elif item["kind"] == "video":
        media = '<video class="media" controls preload="metadata" src="%s"></video>' % h(src)
    else:
        media = '<div class="empty">无法预览该类型</div>'
    body = """
%s
<div class="card" style="margin-top:16px">
  <div class="muted small">%s</div>
  <div style="margin:4px 0 10px;word-break:break-all">%s</div>
  <div class="muted small">%s · 修改时间 %s · SHA-256 %s</div>
  <div class="row" style="margin-top:12px">
    <a class="btn" href="%s&download=1">下载</a>
    <a class="btn ghost" href="/album?device=%s">返回相册</a>
  </div>
</div>
""" % (media, h(item["device"]), h(item["path"]), h(fmt_size(item["size"])),
       h(fmt_time(item["mtime"])), h(item["sha256"][:16] if item["sha256"] else "-"),
       h(src), q(item["device"]))
    return admin_layout("album", "相册", body, "管理员", csrf)


# ---------------------------------------------------------------------------
# playtime / playlog
# ---------------------------------------------------------------------------

def fmt_secs(sec) -> str:
    try:
        sec = int(sec)
    except Exception:
        return "-"
    if sec >= 3600:
        return "%.1f h" % (sec / 3600.0)
    return "%d min" % max(1, sec // 60)


def daily_playtime(events, days=14):
    """Pair Launched/Closed events into sessions and bucket them by local day."""
    sessions = []
    start = None
    for ev in events or []:
        t = ev.get("type")
        ts = int(ev.get("clockTimestamp") or 0)
        if t == "Launched":
            if start is not None and ts > start:
                sessions.append((start, ts))
            start = ts
        elif t == "Closed" and start is not None:
            end = ts if ts > start else start
            if end - start > 24 * 3600:
                end = start + 24 * 3600
            sessions.append((start, end))
            start = None

    today = time.localtime()
    day0 = time.mktime((today.tm_year, today.tm_mon, today.tm_mday, 0, 0, 0, 0, 0, -1))
    buckets = {}
    for a, b in sessions:
        day = time.strftime("%Y-%m-%d", time.localtime(a))
        buckets[day] = buckets.get(day, 0) + max(0, b - a)

    points = []
    for i in range(days - 1, -1, -1):
        ts = day0 - i * 86400
        key = time.strftime("%Y-%m-%d", time.localtime(ts))
        points.append((time.strftime("%m-%d", time.localtime(ts)), buckets.get(key, 0)))
    return points


def playtime_page(rows, devices, current, total_ns, csrf) -> str:
    trs = []
    for r in rows:
        trs.append(
            "<tr><td>%s</td><td class=\"muted\">%s</td><td>%s</td><td>%s</td><td>%s</td>"
            "<td class=\"muted small\">%s</td></tr>"
            % (h(r["title_name"] or r["title_id"]),
               h(user_label(r["user_id"], r["user_name"] if "user_name" in r.keys() else "")),
               h(fmt_hours(r["playtime_ns"])), h(str(r["launches"])),
               h(fmt_time(r["last_played"])), h(r["device"])))
    if not trs:
        trs.append('<tr><td colspan="6" class="muted">还没有游玩记录</td></tr>')

    top = rows[:15]
    chart = chart_bars("游戏时长 Top 15", [
        (r["title_name"] or r["title_id"], int(r["playtime_ns"])) for r in top
    ], lambda v: fmt_hours(int(v)))

    users = {}
    for r in rows:
        key = user_label(r["user_id"], r["user_name"] if "user_name" in r.keys() else "")
        users[key] = users.get(key, 0) + int(r["playtime_ns"])
    user_chart = chart_bars("按用户", sorted(users.items(), key=lambda kv: kv[1], reverse=True),
                            lambda v: fmt_hours(int(v)))

    devs = {}
    for r in rows:
        devs[r["device"]] = devs.get(r["device"], 0) + int(r["playtime_ns"])
    dev_chart = ""
    if len(devs) > 1:
        dev_chart = chart_bars("按设备", sorted(devs.items(), key=lambda kv: kv[1], reverse=True),
                               lambda v: fmt_hours(int(v)))

    body = """
<div class="cards" style="margin-bottom:16px">%s%s</div>
%s
%s
%s
%s
<h2>全部记录</h2>
<table><thead><tr><th>游戏</th><th>用户</th><th>累计时长</th><th>启动次数</th>
<th>最近游玩</th><th>设备</th></tr></thead><tbody>%s</tbody></table>
""" % (stat_card("记录数", str(len(rows))), stat_card("总时长", fmt_hours(total_ns)),
       device_filter(devices, current, "/playtime"), chart, user_chart, dev_chart,
       "".join(trs))
    return admin_layout("playtime", "游玩时长", body, "管理员", csrf)


def playlog_page(data, devices, current, csrf) -> str:
    if not data:
        body = (device_filter(devices, current, "/playlog") +
                '<div class="empty">还没有上传详细游玩记录</div>')
        return admin_layout("playlog", "游玩事件", body, "管理员", csrf)

    user_totals = {}
    title_totals = {}
    event_counts = {}
    for user in data.get("users", []):
        uname = user.get("name") or user_label(user.get("id", ""))
        for t in user.get("titles", []):
            summary = t.get("summary") or {}
            secs = int(summary.get("playtime", 0) or 0)
            user_totals[uname] = user_totals.get(uname, 0) + secs
            tname = t.get("name") or t.get("id", "")
            title_totals[tname] = title_totals.get(tname, 0) + secs
            for ev in t.get("events") or []:
                kind = ev.get("type", "?")
                event_counts[kind] = event_counts.get(kind, 0) + 1

    charts = "".join([
        chart_bars("用户总时长", sorted(user_totals.items(), key=lambda kv: kv[1], reverse=True),
                   lambda v: fmt_hours(int(v * 1_000_000_000))),
        chart_bars("游戏时长 Top 15",
                   sorted(title_totals.items(), key=lambda kv: kv[1], reverse=True)[:15],
                   lambda v: fmt_hours(int(v * 1_000_000_000))),
        chart_bars("事件类型", sorted(event_counts.items(), key=lambda kv: kv[1], reverse=True),
                   lambda v: "%d 次" % int(v)),
    ])

    blocks = []
    for user in data.get("users", []):
        titles = user.get("titles", [])
        rows = []
        for t in titles:
            summary = t.get("summary") or {}
            events = t.get("events") or []
            rows.append(
                "<tr><td><a href=\"/playlog/title?device=%s&uid=%s&title=%s\">%s</a>"
                "<div class=\"muted small mono\">%s</div></td><td>%s</td><td>%s</td>"
                "<td>%s</td><td>%s</td></tr>"
                % (q(current), q(t.get("id", "")), q(t.get("id", "")),
                   h(t.get("name") or "(未知游戏)"), h(t.get("id", "")),
                   h(fmt_hours(int(summary.get("playtime", 0)) * 1_000_000_000)),
                   h(str(summary.get("launches", 0))),
                   h(fmt_time(summary.get("lastPlayed", 0))), h(str(len(events)))))
        if not rows:
            rows.append('<tr><td colspan="5" class="muted">没有记录</td></tr>')
        blocks.append("""
<h2>%s <span class="muted small mono">%s</span></h2>
<table><thead><tr><th>游戏</th><th>时长</th><th>启动</th><th>最近游玩</th><th>事件</th></tr></thead>
<tbody>%s</tbody></table>
""" % (h(user.get("name") or "用户"), h(user.get("id", "")), "".join(rows)))

    header = ('<div class="muted small" style="margin-bottom:10px">导出时间：%s · 用户 %d 个</div>'
              % (h(fmt_time(data.get("exportTimestamp", 0))), len(data.get("users", []))))
    body = device_filter(devices, current, "/playlog") + header + charts + "".join(blocks)
    return admin_layout("playlog", "游玩事件", body, "管理员", csrf)


def playlog_title_page(device, user, title, summary, events, csrf) -> str:
    rows = []
    for ev in events:
        rows.append("<tr><td>%s</td><td>%s</td><td class=\"muted mono small\">%s</td></tr>"
                    % (h(ev.get("type", "")), h(fmt_time(ev.get("clockTimestamp", 0))),
                       h(ev.get("steadyTimestamp", 0))))
    if not rows:
        rows.append('<tr><td colspan="3" class="muted">没有事件</td></tr>')

    daily = daily_playtime(events, 14)
    daily_chart = chart_columns("最近 14 天游玩时长（按启动/关闭配对）", daily,
                                lambda v: fmt_secs(v))

    body = """
<div class="card" style="margin-bottom:16px">
  <div class="muted small">%s · %s</div>
  <div style="font-size:20px;margin:4px 0">%s</div>
  <div class="muted">累计 %s · 启动 %s 次 · 最近 %s</div>
  <div style="margin-top:10px"><a class="btn ghost small" href="/playlog?device=%s">返回</a></div>
</div>
%s
<h2>事件（%d）</h2>
<div class="scroll"><table><thead><tr><th>类型</th><th>时间</th><th>steady</th></tr></thead>
<tbody>%s</tbody></table></div>
""" % (h(device), h(user), h(title), h(fmt_hours(int(summary.get("playtime", 0)) * 1_000_000_000)),
       h(str(summary.get("launches", 0))), h(fmt_time(summary.get("lastPlayed", 0))),
       q(device), daily_chart, len(events), "".join(rows))
    return admin_layout("playlog", "游玩事件", body, "管理员", csrf)


# ---------------------------------------------------------------------------
# settings / errors
# ---------------------------------------------------------------------------

def settings_page(base_urls, token, devices, user, csrf, message="", kind="ok") -> str:
    base = "".join('<div class="copybox"><div><div class="muted small">服务器地址</div>'
                   '<code>%s</code></div>'
                   '<button class="btn ghost small" onclick="copy(this)">复制</button></div>'
                   % h(u) for u in base_urls) or '<div class="empty">未检测到局域网地址</div>'
    masked = token[:4] + "…" + token[-4:] if len(token) > 12 else token
    body = """
%s
<h2>客户端对接</h2>
<div class="card">
  <div class="muted small">在 Switch 应用的「设置」里填入：</div>
  %s
  <div class="copybox"><div><div class="muted small">访问令牌</div>
    <code id="tok" style="cursor:pointer" onclick="this.textContent='%s'">%s</code>
    <div class="muted small">点击令牌可显示完整内容；输入时不区分大小写，横线可省略</div>
  </div><button class="btn ghost small" onclick="copy(document.getElementById('tok'))">复制</button></div>
  <form method="post" action="/settings/token" onsubmit="return confirm('重置后所有客户端需要更新令牌，确定？')">
    %s<button class="btn danger small" type="submit">重置访问令牌</button>
  </form>
</div>
<h2>修改管理员密码</h2>
<div class="card" style="max-width:560px">
  <form method="post" action="/settings/password">
    %s
    <div class="field"><label>当前密码</label><input type="password" name="old" required></div>
    <div class="field"><label>新密码（至少 8 位）</label>
      <input type="password" name="new" minlength="8" required></div>
    <div class="field"><label>确认新密码</label>
      <input type="password" name="new2" minlength="8" required></div>
    <button class="btn" type="submit">保存</button>
  </form>
</div>
<h2>已连接设备</h2>
<table><thead><tr><th>设备 ID</th><th>名称</th><th>IP</th><th>版本</th><th>最近连接</th></tr></thead>
<tbody>%s</tbody></table>
<script>
function copy(el){navigator.clipboard.writeText(el.textContent.trim()).then(()=>{
  el.style.color='#35c48a';setTimeout(()=>el.style.color='',900);}).catch(()=>{});}
</script>
""" % (flash(message, kind), base, h(token), h(masked), csrf_field(csrf), csrf_field(csrf),
       "".join("<tr><td class=\"mono\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
               % (h(d["id"]), h(d["name"] or "-"), h(d["ip"] or "-"),
                  h(d["version"] or "-"), h(fmt_time(d["last_seen"]))) for d in devices)
       or '<tr><td colspan="5" class="muted">还没有设备</td></tr>')
    return admin_layout("settings", "对接设置", body, user, csrf)


def error_page(code: int, message: str) -> str:
    return simple_layout(str(code), """
<div class="login-wrap"><div class="login-card" style="text-align:center">
  <h1 style="font-size:44px;margin:0 0 8px">%d</h1>
  <div class="muted" style="margin-bottom:18px">%s</div>
  <a class="btn ghost" href="/">返回首页</a>
</div></div>""" % (code, h(message)))
