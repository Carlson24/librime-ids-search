# rime-ids-search

librime 插件：**部件检字（IDS 模糊搜字）翻译器组件**，C++ 实现。

## 数据格式（IDS3）

单文件 `ids_data.bin`（约 24MB），面向**查询延迟**设计：`mmap` 惰性映射、稠密字 id、
整数部件匹配、位图/有序数组倒排、生成期预算的 tier 提示。

- **稠密 id**：103k+ 汉字映射到 `0..N-1`（`CP2ID`/`ID2CP`），打分/寻址全部走 id。
- **HOT**：按 id 直索引 `{freq, depth, block, char_token, flags}`，打分零间接跳转；
  `flags.len_mask` 记录该字各长度变体是否存在，用于命中快否。
- **整数部件匹配**：14k+ 部件 token 表（`u16`）；alts 存成按长度分组的平坦 `u16` 序列 +
  每变体 `u32` 有序签名（单 token 查询可 O(1) 快判，否则回退整数成员比较）。
- **倒排**：每 key 存字频序 top-1024 热头（有界候选生成）；完整集合大 key 用**位图**、
  小 key 用**id 升序 `u32`**；多项交集用位图 AND / 双指针归并，不建哈希表。
- **PY/TOKENS/KEYS**：拼音→token、token 词表、倒排键表。

区间（section）采用节表，全部定长、小端、8 字节对齐。

## 组件注册

- 模块：`RIME_REGISTER_MODULE(ids_search)`（插件名 `librime-ids-search`）
- 组件：`ids_search_translator`（schema 中 `translators: - ids_search_translator`）
- 配置命名空间：`ids_search`（代码自纠正 name_space 到该值）

## 配置示例（schema.yaml）

```yaml
engine:
  translators:
    - ids_search_translator   # 组件名

ids_search:                   # 配置命名空间
  data_file: build/ids_data.bin   # 相对路径基于用户数据目录；不存在则回退共享数据目录；绝对路径直接使用
  tag:          ids_search        # 需与 recognizer 的 tag 一致
  prefix:       "I"               # 触发前缀，与 recognizer 模式一致
  tips:         〔部件检字〕
  max_results:  60
  shuangpin_scheme: flypy         # flypy | zrm | quanpin
  show_pinyin:  true
  show_ids:     true
  show_unicode: true
  show_block:   true
  # disabled_blocks: [扩A, 扩展B]   # 可选（列表或逗号/空格分隔字符串）
  # custom_file: ids_custom.txt     # 可选，相对路径同上解析
```

数据文件放入触发方用户数据目录（如 `<user_data>/build/ids_data.bin`），或共享数据目录。

## 生成数据

```sh
cd plugins/ids-search/data
python3 generate_data_v2.py    # 依赖 pypinyin；产出 ids_data.bin
```

## 构建

```sh
# 在 librime 源码根目录（已启用外部插件模式：ENABLE_EXTERNAL_PLUGINS=ON）
cmake -S . -B build
cmake --build build --target rime-ids-search
# 产出：build/lib/rime-plugins/librime-ids-search.so
```

## 部署

将新建的 `librime.so` 与 `librime-ids-search.so` 一起安装到运行方：

- 插件 .so 放到 `librime.so` 同级的 `rime-plugins/` 目录；
- 确保前端（fcitx5-rime / ibus-rime / Weasel）加载的是这次新构建的 librime。

## 验证

- 冒烟：`I木口` / `Ikouhao` / `I老六` / `I木木木` / `I氵工`
- 性能目标：单字查询 <0.1ms、多项查询 <1ms

## 备注

- `ids_search.lua` 与 `ids_page_processor.lua` 为历史文件，不再与 IDS3 磁盘格式对应
  （`ids_page_processor.lua` 仍可用于翻页键处理）。
