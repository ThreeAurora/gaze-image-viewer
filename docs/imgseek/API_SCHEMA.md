# 万象图搜 (imgseek) HTTP API 字段级 schema —— Gaze 客户端对接权威参考

> **出处与效力**：全部字段逐条对照源码提取（万象图搜仓 HEAD `ad750f0`：`imgseek/api.py`、`imgseek/search.py`、`imgseek/config.py`、`imgseek/db.py`、`imgseek/ocr.py`、`imgseek/clip_models.py`），零猜测。FastAPI 路由返回裸 dict 且**无 response_model**——openapi.json 记不了响应字段，本文即唯一权威。
> **提取时间**：2026-09-04（#133 Tier2 解除阻塞件）。
> **服务形态**：FastAPI + uvicorn，绑定 `127.0.0.1:8747` 单机单 worker；阻塞路由（普通 def）自动走线程池。gaze 侧键 `ImgSearch/port`。
> **错误约定**：业务错 = HTTP 4xx + `{"detail": "<消息>"}`（FastAPI HTTPException 默认形，如 400 "not a directory"/"unknown model"/"no active model session"/"unknown stage"，404 无 body 或带 detail）；未捕获异常 = HTTP 500 + `{"error": "<str>"}`（api.py `json_errors` 统一兜底）。

## 端点总表

| 方法与路径 | 用途 | 请求体 | 成功响应 |
|---|---|---|---|
| GET / | 内置 web 前端（no-cache） | — | index.html |
| GET /api/status | 进度/速率/后端状态 | — | 见下 |
| GET /api/folders | 监视目录列表（含统计） | — | `{"folders": [...]}` |
| POST /api/folders | 加目录 | `{"path": str}` | `{"ok": true, "msg": ""|"already exists"}`（非目录=400） |
| DELETE /api/folders/{id} | 删目录（触发 purge 扫描） | — | `{"ok": true}` |
| POST /api/folders/{id}/toggle | 纳入/移出搜索与索引 | `{"enabled": bool}` | `{"ok": true}` |
| POST /api/folders/{id}/pause | 单目录暂停索引处理（不影响搜索纳入） | `{"on": bool}` | `{"ok": true}` |
| POST /api/folders/reorder | 目录排序 | `{"ids": [int]}`（按新顺序） | `{"ok": true}` |
| POST /api/open-data | 打开服务端 data/ 目录 | — | `{"ok": true}` |
| POST /api/scan/start | 触发全量/增量扫描 | — | `{"ok": true}` |
| POST /api/retry | 失败项重跑 | `{"stage": "thumb"|"ocr"|"embed"}` | `{"ok": true, "reset": int}`；未知 stage=400，embed 无会话=400 |
| POST /api/unload | 释放 OCR 会话+常驻向量（查询转冷路径） | — | `{"ok": true}` |
| POST /api/pipeline/pause | 流水线全局暂停/继续 | `{"on": bool}` | `{"ok": true, "paused": bool}` |
| POST /api/models/activate | 切换激活模型 | `{"key": str}` | `{"ok": true}`；未知 key=400 |
| GET /api/search | 三路融合搜索 | query 参数 | 见下 |
| GET /api/thumb/{id} | 缩略图（webp，immutable 缓存） | — | 图片字节；坏图=PNG 占位；无记录=404 |
| GET /api/file/{id} | 原图文件 | — | 文件字节；无记录/盘上缺失=404 |
| POST /api/open | 系统默认程序打开 | `{"id": int}` | `{"ok": true}`；无记录/缺失=404 |
| GET /api/detail/{id} | 单图完整元数据 | — | image 表整行 dict（见下） |

## GET /api/status 响应字段

```jsonc
{
  "active_model": "cn_clip_b16",     // str；settings.active_model，默认 config.DEFAULT_MODEL
  "models": [                        // 数组，按 config.MODELS 注册序：cn_clip_b16, clip_b32
    {
      "key": "cn_clip_b16",          // str
      "label": "中文 Chinese-CLIP B/16",  // str；config.MODELS[key]["label"]（设计内不翻）
      "session": "unloaded",         // str 枚举：unloaded(初始/缺省) | loading | ready | error
      "error": "",                   // str；激活失败消息（≤300 字符），正常为 ""
      "pending_embed": 0             // int；embed_status 中该模型 status=0 计数
    }
  ],
  "images_total": 0,                 // int；image 表 COUNT(*)（含 dead=1）
  "pending": {"thumb": 0, "ocr": 0}, // int；status=0 且 dead=0 计数
  "failed": {"thumb": 0, "ocr": 0},  // int；status=2 计数
  "ocr_backend": "unloaded",         // str 枚举：unloaded | cuda | cpu
  "scanning": false,                 // bool；流水线扫描中
  "paused": false,                   // bool；流水线全局暂停中
  "rate_per_sec": 0.0                // float；10 秒滑窗处理速率（round 1 位）
}
```

## GET /api/folders 响应字段

`{"folders": [ <每目录一行> ]}`，按 sort_order, id 排序：

```jsonc
{
  "id": 1,            // int
  "path": "G:/photos",// str（服务端 abspath+normpath 存库）
  "enabled": 1,       // int 0/1；0=不索引且不出现在搜索结果
  "paused": 0,        // int 0/1；1=暂停索引处理，但 enabled=1 时仍参与搜索
  "sort_order": 0,    // int（/api/folders/reorder 写入）
  "images": 3,        // int；该目录 image 行数（LEFT JOIN，含 dead）
  "processed": 3,     // int；thumb_status=1 且 dead=0
  "pending": 0,       // int；thumb_status=0 且 dead=0（注意：按缩略图阶段计）
  "dead": 0           // int；dead=1 行数（盘上已消失，待 purge）
}
```

## GET /api/search 响应字段

请求：`GET /api/search?q=<查询词>&model=<模型键,可省>&sort=<relevance|name|mtime|size,默认 relevance>`

- `q` 空时：返回最近 mtime 前 500 张（无 sources）。
- `model` 省略时用服务端 active_model。
- 返回上限 `SEARCH_LIMIT = 500`。

```jsonc
{
  "elapsed_ms": 85,      // int
  "total": 42,           // int；= 本页 results 长度（截断后）
  "truncated": false,    // bool；name/ocr 路命中数触及上限时 true（语义路恒 false）
  "results": [
    {
      "id": 123, "path": "...", "filename": "...", "ext": "jpg",
      "size": 1024,      // int 字节
      "mtime": 0.0,      // REAL epoch 秒（可为 0/NULL）
      "width": 1920, "height": 1080,   // 可为 null
      "sources": [                       // 命中来源，可多值；relevance 排序时按路并入
        {"type": "name"},                        // 文件名/路径 LIKE 命中
        {"type": "ocr", "matched": "<查询词>"},  // OCR 全文命中；无 snippet 字段！
        {"type": "sem", "score": 0.4123}         // 语义命中；余弦分 round 4 位，服务端已按阈值过滤
      ]
      // 无 rrf 字段：relevance 排序时服务端 pop 掉；非 relevance 时也不返回
    }
  ]
}
```

**OCR 片段定案（#133 备忘）**：API **没有** snippet/片段字段——ocr source 只带 `matched`（即查询词原文）。要片段须另发 `GET /api/detail/{id}` 取 `ocr_text` 在客户端自行截取上下文。

## GET /api/detail/{id} 响应 = image 表整行

```jsonc
{
  "id": 1, "folder_id": 1,
  "path": "...", "filename": "...", "ext": "jpg",
  "size": 0, "mtime": 0.0,          // REAL epoch 秒
  "width": null, "height": null,    // 解码前可为 null
  "content_hash": "sha1hex|null",   // 缩略图寻址键
  "last_seen_epoch": 0, "dead": 0,
  "thumb_status": 1, "ocr_status": 1,  // 0 待处理 1 完成 2 永久失败
  "ocr_text": "...",                // OCR 全文（空串=无文字/未跑）
  "error": null, "indexed_at": 0.0
}
```

## 模型注册表（config.MODELS，/api/models/activate 的 key 取值）

| key | label | dim | 语义命中下限（服务端已滤） | 说明 |
|---|---|---|---|---|
| `cn_clip_b16`（默认） | 中文 Chinese-CLIP B/16 | 512 | 0.38 | 整模架构，文本最长 52 token |
| `clip_b32` | 英文 CLIP B/32 | 512 | 0.25 | 分塔架构，文本最长 77 token |

权重缺失时 activate 会后台自动下载（走本地代理 7890 直连 huggingface），期间 `/api/status` 的该模型 `session="loading"`。

## Tier2 对接要点（源码定案备忘）

1. **状态条**：`/api/status` 的 `scanning/paused/rate_per_sec/pending/failed/pending_embed` 直接可用；`models[].session` 枚举见上。
2. **唤醒 vs 搜索**：空闲 30 分钟服务自动 unload（`IDLE_UNLOAD_SECONDS=1800`），之后首次查询走冷路径 ~2s——UI 必须区分"正在唤醒后端"与"正在搜索"。
3. **目录管理**：`enabled` 控制是否纳入索引与搜索；`paused` 只停索引处理不摘搜索。删除目录走 DELETE 后服务端自动 purge。
4. **重试**：`stage` 三值 `thumb/ocr/embed`；embed 重跑只作用于当前激活模型。
5. **语义阈值**：服务端已按 `SEM_MIN_SCORE` 过滤（cn_clip_b16≥0.38 / clip_b32≥0.25），客户端不必再筛，但排序展示可直接用 `sources[].score`。
6. **客户端通用 get/post 已就绪**（imgsearch.h `ImgClient`），Tier2 只需加 UI，勿动传输层。
7. **服务生命周期铁律**（#222）：Gaze 默认不拉起服务（`ImgSearch/autoStart` 默认 false），唯一拉起点=搜索时 ensureRunningAsync 且开关打开。
