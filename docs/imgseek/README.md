> 原样拷贝自万象图搜仓 `README.md`（HEAD `ad750f0`，2026-09-04 提取，#133 Tier2 解除阻塞件）。字段级 API 对接参考见同目录 [API_SCHEMA.md](API_SCHEMA.md)（对照源码提取，比本 README 的 API 一览更细）。

# 万象图搜 (imgseek)

[English](./README.en-US.md) | 中文

本地超轻量图片语义检索服务 —— Everything 的图片版：**一个搜索框，同时搜文件名、图片内文字（OCR）、画面内容（自然语言语义）**。

- 免 Docker，`python main.py` 即起即停；零侵入，不复制不移动你的照片，删掉 `data/` 即完全卸载
- 中英双 CLIP 模型一键切换（Chinese-CLIP ViT-B/16 + OpenAI CLIP B/32），绕开单一模型的语义盲区
- 几十万张量级：检索 p50 < 100ms（fp32 常驻向量矩阵），前端固定行高虚拟滚动
- 后台流水线全自动：扫描 → 解码 → 缩略图 → OCR → CLIP 嵌入，各阶段状态落库，中断重启即断点续传

## 架构

| 组件 | 选型 |
|---|---|
| 服务 | FastAPI + uvicorn，绑定 127.0.0.1:8747 单机单worker，阻塞路由走线程池 |
| OCR | RapidOCR（PaddleOCR 系模型，onnxruntime 推理）：CUDA 可用自动 GPU，否则 CPU 回退 |
| 语义 | Chinese-CLIP ViT-B/16 + OpenAI CLIP B/32（Xenova ONNX 导出），onnxruntime 推理 |
| 向量检索 | fp32 常驻矩阵单次 matmul（50 万条 p50 ≈ 72ms）+ `.f16bin` 落盘 + slot 复用 |
| 全文索引 | SQLite FTS5 trigram（外部内容表 + 触发器同步），短词退化 LIKE 兜底 |
| 元数据 | SQLite WAL；`image/folder/vector_slot/embed_status` 等表，状态字段即断点续传检查点 |
| 融合排序 | 文件名 LIKE / OCR 全文 / 语义向量三路 RRF（Reciprocal Rank Fusion） |
| 前端 | 单文件原生 JS（`web/index.html`）：虚拟滚动 + 对象池 + 缩略图懒加载 + 索引管理页 |

模块划分：`imgseek/` 包内 `scanner`（增量扫描）、`decoder`（一次解码派生三份输入）、`thumbs`、`ocr`、`clip_models`、`vectors`、`pipeline`（CPU 池 + GPU 串行线程）、`search`、`db`、`api`；根目录 `config.py` 集中全部参数与模型注册表。

## 安装与运行

```bash
# Python 3.10+；GPU 推理需自备匹配的 CUDA 运行时（或直接 CPU 回退）
pip install "fastapi>=0.115" "uvicorn>=0.30" "onnxruntime-gpu" \
            "rapidocr-onnxruntime" "pillow" "numpy" "tokenizers>=0.20"

python main.py                # 自动打开浏览器 http://127.0.0.1:8747
python main.py --pause        # 启动但暂停流水线（不自动续跑索引）
python main.py --rescan       # 立即触发一次全量增量扫描
```

首次使用：右上角「+ 目录」添加要索引的文件夹 → 自动扫描 → 片刻后文件名与语义可搜，OCR 文字索引后台慢啃。索引管理页支持逐目录「索引启停 / 结果纳排 / 删除索引」与拖动排序。

## API 一览

```
GET  /api/search?q=&model=&sort=    融合搜索（name/ocr/sem 三路 RRF）
GET  /api/thumb/{id}                内容寻址缩略图（immutable 缓存）
GET  /api/file/{id}                 原图预览；POST /api/open {id} 用系统默认程序打开
GET/POST/DELETE /api/folders        监视目录管理（增删、启停、排序）
POST /api/scan/start                触发全量/增量扫描
POST /api/models/activate {key}     切换 cn_clip_b16 / clip_b32（权重缺失自动下载）
POST /api/retry {stage}             重跑失败项（thumb/ocr/embed）
POST /api/unload                    释放 OCR 会话与常驻向量
GET  /api/status                    进度 / 速率 / 后端状态
```

## 与 Gaze 查看器的关系

本项目是 [Gaze](https://github.com/ThreeAurora/Gaze)（Windows 本地图片查看器）「以文搜图」功能的后端：Gaze 客户端通过 `127.0.0.1:8747` 的 HTTP API 提交自然语言查询并取回命中缩略图，检索与索引能力全部由本服务承担。二者独立运行——不用 Gaze 也可以单独通过浏览器使用本项目。

## 模型权重与数据说明

- 模型权重随仓库以 ≤90MB 分片分发（GitHub 单文件 100MB 硬限），克隆后按 `data/models/REBUILD_MODELS.md` 重组即可；从 HuggingFace 重新下载的路径仍保留：首次激活模型时按 `config.py` 注册表自动下载到 `data/models/`（约 650MB/模型），网络走本地代理 `127.0.0.1:7890` 直连 huggingface.co（hf-mirror 对大文件会 308 回源导致失败）；已有代理环境变量时不覆盖。
- `data/index.db*`（索引库快照）、`data/thumbs/`（缩略图缓存）与 `data/vectors/`（fp16 向量，重算代价高）均随仓库备份；`index.db-wal/-shm` 为服务运行中的活动文件，入库的是提交时刻的快照。
- 仓库中 `data/demo_report.html` 为开发期抽样演示报告样例，可随时删除。

## 已知边界

- HEIC 依赖 pillow-heif；个别 10bit HDR 图可能解码失败（标失败不出占位，不阻塞队列）
- 空闲 30 分钟自动释放 OCR 会话与常驻向量，查询降级为冷路径约 2s，重新激活模型即恢复
- 中文 FTS 用 trigram：≥3 字符词走 MATCH 索引，更短词退化为 LIKE 扫描

## 致谢

- [Chinese-CLIP](https://github.com/OFA-Sys/Chinese-CLIP) / [OpenAI CLIP](https://github.com/openai/CLIP)（Xenova 的 ONNX 导出版本）
- [RapidOCR](https://github.com/RapidAI/RapidOCR)（基于 PaddleOCR 模型）
- [onnxruntime](https://github.com/microsoft/onnxruntime)、[FastAPI](https://github.com/fastapi/fastapi)、[SQLite FTS5](https://www.sqlite.org/fts5.html)

## 许可证

未附开源许可证（当前为私有项目）。如需引用代码或模型配置，请先联系作者。
