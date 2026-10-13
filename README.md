# rime-ids-search

librime 插件：**部件检字（IDS 模糊搜字）翻译器组件**，用 C++ 实现，替代原 `shared/lua/ids_search.lua`（Lua 版每次按键 ~180ms 的瓶颈：逐候选文件 IO + `string.find` 扫倒排 + GC 抖动）。

关键设计：

- 数据文件（`ids_index.bin` / `ids_meta.bin`）**整读驻留内存**（约 41MB，懒加载），检索过程零文件 IO；
- 多项查询 = **真·倒排 set 交集**（base 取倒排最小项，其余项逐项用 LRU 缓存的 `unordered_set` 过滤），单次按键目标 < 1ms；
- 语义与 Lua v1.4.0 完全一致（拼音/双拼切分、Tier1/2/3 打分、ids_custom、disabled_blocks、show_* 开关），并顺带消除了 `string.find` 未对齐字节误报。

## 组件注册

- 模块：`RIME_REGISTER_MODULE(ids_search)`（插件名 `librime-ids-search`）
- 组件：`ids_search_translator`（schema 中 `translators: - ids_search_translator`）
- 配置命名空间：`ids_search_translator:`（引擎以 `- ids_search_translator` 引用时 name_space 即此值）

## 配置示例（schema.yaml）

```yaml
engine:
  translators:
    - ids_search_translator   # 替换原 lua_translator@*ids_search

ids_search_translator:
  index_file: build/ids_index.bin   # 相对路径基于用户数据目录；不存在则回退共享数据目录；绝对路径直接使用
  meta_file:  build/ids_meta.bin
  tag:          ids_search           # 需与 recognizer 的 tag 一致
  prefix:       "I"                  # 触发前缀，与 recognizer 模式一致
  tips:         〔部件检字〕
  max_results:  60
  shuangpin_scheme: flypy            # flypy | zrm | quanpin
  show_pinyin:  true
  show_ids:     true
  show_unicode: true
  show_block:   true
  # disabled_blocks: [扩A, 扩展B]     # 可选
  # custom_file: ids_custom.txt       # 可选，相对路径同上解析
```

数据文件需要放入触发方用户数据目录的 `build/` 子目录（如 `<user_data>/build/ids_index.bin`），或放在共享数据目录对应位置。

## 构建

```sh
# 在 librime 源码根目录（已启用外部插件模式：ENABLE_EXTERNAL_PLUGINS=ON）
cmake -S . -B build
cmake --build build
# 产出：build/lib/rime-plugins/librime-ids-search.so
```

## 部署

将新建的 `librime.so`（及打印其 rime-plugins 机制）与 `librime-ids-search.so` 一起安装到运行方：

- 插件 .so 放到 `librime.so` 同级的 `rime-plugins/` 目录（`plugins_module` 从 `librime.so` 所在目录的 `rime-plugins/` 加载）；
- 确保前端（fcitx5-rime / ibus-rime / Weasel）加载的是这次新构建的 librime。

## 验证

- 冒烟：`I木口` / `Ikouhao` / `I老六` / `I木木木` / `I氵工`
- 性能：单字查询 <0.1ms、多项查询 <1ms（Lua 版为 ~183ms）
- 回归：与 Lua v1.4.0 的 top-60 候选对照（见 rime-config `dist/ids_search` 测试集）