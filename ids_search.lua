-- ids_search.lua (v1.6.0 高性能求交增强版 — 32-bit 定长倒排表 + 自包含紧凑元数据)
-- 字统 (zi.tools) 风格全量 Unicode 汉字部件模糊搜字 / 结构检索模块
--
-- 融合要点：
-- A. 保留新版 IDX2/MET2 二进制流式架构（0 大字符串常驻，0 堆 GC 抖动）
-- B. 移植旧版改版：
--    1) 小禾双拼 cropy 全面支持
--    2) 完整 ids_custom.txt 解析（-block / -er / -er 阝 / 阝 -er / 阝 =fu yi / 追加别名）
--    3) 空查询不触发数据加载
--    4) prefix_byte 快速门禁
--    5) 4+ 部件无序完美匹配
--    6) 多重复 key 回退修复
--    7) 拼音前缀候选按字频排序
--    8) 缓存键完整化 + 缓存容量调大
--    9) preload 补全

local ids_search = {}

-- 双拼解码表
local SP_INIT_MAP = { v = "zh", i = "ch", u = "sh" }
local SP_INITIALS = { v = "zh", i = "ch", u = "sh", a = "a", o = "o", e = "e" }

local FLYPY_F_MAP = {
    q = "iu", w = "ei", e = "e", r = "uan", y = "un",
    u = "u", i = "i", p = "ie", a = "a", d = "ai",
    f = "en", g = "eng", h = "ang", j = "an", z = "ou",
    c = "ao", b = "in", n = "iao", m = "ian"
}
local ZRM_F_MAP = {
    q = "iu", w = "ia", r = "uan", p = "un",
    u = "u", i = "i", f = "en", g = "eng", h = "ang",
    j = "an", k = "ao", l = "ai", z = "ou", c = "ao",
    b = "in", m = "ian"
}
local CROPY_F_MAP = FLYPY_F_MAP

local BLOCK_NAMES = {
    [0] = "基本", [1] = "扩A", [2] = "扩B", [3] = "扩C",
    [4] = "扩D", [5] = "扩E", [6] = "扩F", [7] = "扩G",
    [8] = "扩H", [9] = "扩I", [10] = "扩J", [11] = "兼容区",
    [12] = "兼容补充", [13] = "部首补充", [14] = "康熙部首",
    [15] = "笔画", [16] = "CJK"
}

local BLOCK_ALIAS_MAP = {
    ["基本"] = 0, ["基本区"] = 0, ["basic"] = 0, ["base"] = 0, ["cjk"] = 0,
    ["扩a"] = 1, ["exta"] = 1, ["ext-a"] = 1, ["a"] = 1,
    ["扩b"] = 2, ["extb"] = 2, ["ext-b"] = 2, ["b"] = 2,
    ["扩c"] = 3, ["extc"] = 3, ["ext-c"] = 3, ["c"] = 3,
    ["扩d"] = 4, ["extd"] = 4, ["ext-d"] = 4, ["d"] = 4,
    ["扩e"] = 5, ["exte"] = 5, ["ext-e"] = 5, ["e"] = 5,
    ["扩f"] = 6, ["extf"] = 6, ["ext-f"] = 6, ["f"] = 6,
    ["扩g"] = 7, ["extg"] = 7, ["ext-g"] = 7, ["g"] = 7,
    ["扩h"] = 8, ["exth"] = 8, ["ext-h"] = 8, ["h"] = 8,
    ["扩i"] = 9, ["exti"] = 9, ["ext-i"] = 9, ["i"] = 9,
    ["扩j"] = 10, ["extj"] = 10, ["ext-j"] = 10, ["j"] = 10,
    ["兼容区"] = 11, ["兼容"] = 11, ["compat"] = 11,
    ["兼容补充"] = 12, ["compat-sup"] = 12,
    ["部首补充"] = 13, ["rad-sup"] = 13,
    ["康熙部首"] = 14, ["kangxi"] = 14,
    ["笔画"] = 15, ["strokes"] = 15,
}

local data_loaded = false
local file_idx = nil
local index_offsets = nil
local index_counts = nil
local postings_base = 0

local file_meta = nil
local meta_max_cp = 0x33500

local py_map = {}
local valid_pinyins = {}
local prefix_comps_cache = {}

-- 偏旁等价归一表（融合旧版补充项）
local canon_map = {
    ["氵"] = "⺡", ["水"] = "⺡", ["⺡"] = "⺡", ["氺"] = "⺡",
    ["灬"] = "⺣", ["火"] = "⺣", ["⺣"] = "⺣",
    ["亻"] = "⺅", ["人"] = "⺅", ["𠆢"] = "⺅", ["⺅"] = "⺅",
    ["扌"] = "龵", ["手"] = "龵", ["龵"] = "龵",
    ["忄"] = "⺖", ["心"] = "⺖", ["⺖"] = "⺖", ["⺗"] = "⺖",
    ["⺼"] = "肉", ["月"] = "肉", ["肉"] = "肉", ["⺝"] = "肉",
    ["艹"] = "⺾", ["艸"] = "⺾", ["⺾"] = "⺾", ["⻀"] = "⺾", ["草"] = "⺾",
    ["礻"] = "⺬", ["示"] = "⺬", ["⺬"] = "⺬",
    ["衤"] = "⺭", ["衣"] = "⺭", ["⺭"] = "⺭", ["𧘇"] = "⺭",
    ["釒"] = "⻐", ["金"] = "⻐", ["钅"] = "⻐", ["⻐"] = "⻐", ["⻑"] = "⻐",
    ["飠"] = "⻠", ["食"] = "⻠", ["饣"] = "⻠", ["⻠"] = "⻠", ["⻡"] = "⻠", ["𩙿"] = "⻠",
    ["糹"] = "⺰", ["糸"] = "⺰", ["纟"] = "⺰", ["⺰"] = "⺰", ["⺱"] = "⺰",
    ["犭"] = "⺲", ["犬"] = "⺲", ["⺲"] = "⺲",
    ["辶"] = "⻌", ["辵"] = "⻌", ["⻌"] = "⻌", ["⻍"] = "⻌",
    ["阝"] = "⻖", ["阜"] = "⻖", ["邑"] = "⻖", ["⻖"] = "⻖", ["⻗"] = "⻖",
    ["王"] = "⺩", ["⺩"] = "⺩", ["𤣩"] = "⺩", ["玉"] = "⺩",
    ["足"] = "⻊", ["⻊"] = "⻊", ["𧾷"] = "⻊",
    ["四"] = "⺫", ["罒"] = "⺫", ["网"] = "⺫", ["⺫"] = "⺫",
    ["竹"] = "⺮", ["⺮"] = "⺮", ["𥫗"] = "⺮",
    ["刀"] = "⺉", ["刂"] = "⺉", ["⺈"] = "⺉", ["⺉"] = "⺉",
    ["羊"] = "⺶", ["⺶"] = "⺶", ["⺷"] = "⺶", ["𦍌"] = "⺶",
    ["⺆"] = "冂", ["冂"] = "冂",
    ["攵"] = "⺙", ["攴"] = "⺙", ["⺙"] = "⺙",
    ["耂"] = "⺹", ["老"] = "⺹", ["⺹"] = "⺹",
    ["言"] = "讠", ["讠"] = "讠", ["訁"] = "讠",
    ["牛"] = "牜", ["牜"] = "牜", ["⺧"] = "牜",
    ["龍"] = "龙", ["龙"] = "龙",
    ["馬"] = "马", ["马"] = "马",
    ["鳥"] = "鸟", ["鸟"] = "鸟",
    ["魚"] = "鱼", ["鱼"] = "鱼",
    ["車"] = "车", ["车"] = "车",
    ["門"] = "门", ["门"] = "门",
    ["見"] = "见", ["见"] = "见",
    ["貝"] = "贝", ["贝"] = "贝",
    ["頁"] = "页", ["页"] = "页",
    ["風"] = "风", ["风"] = "风",
    ["飛"] = "飞", ["飞"] = "飞",
    ["黽"] = "黾", ["黾"] = "黾",
    ["齊"] = "齐", ["齐"] = "齐",
    ["齒"] = "齿", ["齿"] = "齿",
    ["長"] = "长", ["长"] = "长",
    ["日"] = "日", ["曰"] = "日",
    -- 笔画名称归一
    ["撇"] = "㇒", ["丿"] = "㇒", ["㇒"] = "㇒",
    ["横"] = "㇐", ["一"] = "㇐", ["㇐"] = "㇐",
    ["竖"] = "㇑", ["丨"] = "㇑", ["㇑"] = "㇑",
    ["点"] = "㇔", ["丶"] = "㇔", ["㇔"] = "㇔",
    ["捺"] = "㇏", ["乀"] = "㇏", ["乁"] = "㇏", ["㇏"] = "㇏",
    ["提"] = "㇀", ["㇀"] = "㇀",
    ["折"] = "乛", ["乛"] = "乛",
    ["钩"] = "亅", ["鉤"] = "亅", ["亅"] = "亅",
}

local function canon(c)
    if not c then return c end
    if string.sub(c, 1, 1) == "{" and string.sub(c, -1) == "}" then
        local inner = string.sub(c, 2, -2)
        return canon_map[inner] or inner
    end
    return canon_map[c] or c
end

local function utf8_codepoint(ch)
    local b1 = string.byte(ch, 1)
    if not b1 then return 0 end
    if b1 < 0x80 then
        return b1
    elseif b1 < 0xE0 then
        local b2 = string.byte(ch, 2)
        return (b1 - 0xC0) * 0x40 + (b2 - 0x80)
    elseif b1 < 0xF0 then
        local b2, b3 = string.byte(ch, 2), string.byte(ch, 3)
        return (b1 - 0xE0) * 0x1000 + (b2 - 0x80) * 0x40 + (b3 - 0x80)
    else
        local b2, b3, b4 = string.byte(ch, 2), string.byte(ch, 3), string.byte(ch, 4)
        return (b1 - 0xF0) * 0x40000 + (b2 - 0x80) * 0x1000 + (b3 - 0x80) * 0x40 + (b4 - 0x80)
    end
end

local function utf8_chars(str)
    local chars = {}
    for uchar in string.gmatch(str, "[%z\1-\127\194-\244][\128-\191]*") do
        table.insert(chars, uchar)
    end
    return chars
end

local function find_data_file(filename)
    local rel = filename:gsub("^[\\/]+", "")
    local candidates = {}
    local function add_base(base)
        if base and base ~= "" then
            candidates[#candidates + 1] = base .. "/" .. rel
            candidates[#candidates + 1] = base .. "/lua/data/" .. rel
            candidates[#candidates + 1] = base .. "/lua/" .. rel
        end
    end
    local user_dir   = rime_api and rime_api.get_user_data_dir and rime_api.get_user_data_dir()
    local shared_dir = rime_api and rime_api.get_shared_data_dir and rime_api.get_shared_data_dir()
    add_base(user_dir)
    add_base(shared_dir)
    candidates[#candidates + 1] = filename
    for _, path in ipairs(candidates) do
        local f = io.open(path, "r")
        if f then f:close(); return path end
    end
    return nil
end

local function has_index_key(k)
    return index_offsets and (index_offsets[k] ~= nil)
end

--------------------------------------------------------------------------------
-- MET2 元数据寻址与解析（融合旧版补充：缓存容量调大至 5000）
--------------------------------------------------------------------------------
local meta_info_cache = {}
local meta_info_cache_count = 0
local MAX_CACHE_ENTRIES = 5000

local function get_meta_info(cp)
    local cached = meta_info_cache[cp]
    if cached then return cached end

    if not file_meta or cp <= 0 or cp >= meta_max_cp then
        cached = { freq = 0, block_id = 0, depth = 10, py = "", ids = "", alts_str = "", alts = {} }
        return cached
    end

    file_meta:seek("set", 12 + cp * 4)
    local raw_rec = file_meta:read(4)
    if not raw_rec or #raw_rec < 4 then
        cached = { freq = 0, block_id = 0, depth = 10, py = "", ids = "", alts_str = "", alts = {} }
        return cached
    end
    local rec_pos = string.unpack("<I4", raw_rec, 1)
    if rec_pos == 0 then
        cached = { freq = 0, block_id = 0, depth = 10, py = "", ids = "", alts_str = "", alts = {} }
        if meta_info_cache_count >= MAX_CACHE_ENTRIES then
            meta_info_cache = {}; meta_info_cache_count = 0
        end
        meta_info_cache[cp] = cached
        meta_info_cache_count = meta_info_cache_count + 1
        return cached
    end

    file_meta:seek("set", rec_pos - 1)
    local header = file_meta:read(8)
    if not header or #header < 8 then
        cached = { freq = 0, block_id = 0, depth = 10, py = "", ids = "", alts_str = "", alts = {} }
        return cached
    end
    local freq, block_id, depth, py_len, ids_len = string.unpack("<IBBBB", header, 1)
    local py = (py_len > 0) and file_meta:read(py_len) or ""
    local ids = (ids_len > 0) and file_meta:read(ids_len) or ""
    local raw_alts_len = file_meta:read(2)
    local alts_len = (raw_alts_len and #raw_alts_len == 2) and string.unpack("<I2", raw_alts_len, 1) or 0
    local alts_str = (alts_len > 0) and file_meta:read(alts_len) or ""

    cached = {
        freq = freq, block_id = block_id, depth = depth,
        py = py, ids = ids, alts_str = alts_str, alts = nil
    }

    if meta_info_cache_count >= MAX_CACHE_ENTRIES then
        meta_info_cache = {}; meta_info_cache_count = 0
    end
    meta_info_cache[cp] = cached
    meta_info_cache_count = meta_info_cache_count + 1
    return cached
end

local function get_meta_alts(meta)
    if not meta then return {} end
    if meta.alts then return meta.alts end
    local alts = {}
    local alts_str = meta.alts_str
    if alts_str and #alts_str > 0 then
        for alt in string.gmatch(alts_str, "[^|]+") do
            local comps = {}
            for c in string.gmatch(alt, "[^,]+") do
                table.insert(comps, canon(c))
            end
            table.insert(alts, comps)
        end
    end
    meta.alts = alts
    return alts
end

--------------------------------------------------------------------------------
-- 拼音前缀按需查询（融合旧版：按字频排序，保证高频部件先被读取）
--------------------------------------------------------------------------------
local function get_comps_by_prefix(pref)
    if not pref or #pref == 0 then return nil end
    if prefix_comps_cache[pref] ~= nil then
        local c = prefix_comps_cache[pref]
        return c or nil
    end
    local res = nil
    local seen = {}
    local pref_len = #pref
    for py, comps in pairs(py_map) do
        if #py >= pref_len and string.sub(py, 1, pref_len) == pref then
            for _, c in ipairs(comps) do
                if not seen[c] then
                    seen[c] = true
                    if not res then res = {} end
                    table.insert(res, c)
                end
            end
        end
    end
    if res and #res > 1 then
        local freq_cache = {}
        for _, c in ipairs(res) do
            local cp = utf8_codepoint(c)
            local f = 0
            if cp > 0 then
                local m = get_meta_info(cp)
                f = (m and m.freq) or 0
            end
            freq_cache[c] = f
        end
        table.sort(res, function(a, b) return freq_cache[a] > freq_cache[b] end)
    end
    prefix_comps_cache[pref] = res or false
    return res
end

--------------------------------------------------------------------------------
-- 加载二进制数据文件 (IDX2 + MET2 + ids_custom.txt 完整解析)
--------------------------------------------------------------------------------
local function ensure_data_loaded(env)
    if data_loaded then return true end

    local idx_bin_path = find_data_file("ids_index.bin")
    local meta_bin_path = find_data_file("ids_meta.bin")
    if not idx_bin_path or not meta_bin_path then
        return false
    end

    -- 1. 打开 MET2
    local f_meta = io.open(meta_bin_path, "rb")
    if not f_meta then return false end
    local meta_header = f_meta:read(12)
    if not meta_header or #meta_header < 12 then f_meta:close(); return false end
    local meta_magic, max_cp, _data_len = string.unpack("<c4II", meta_header, 1)
    if meta_magic ~= "MET2" then f_meta:close(); return false end
    file_meta = f_meta
    meta_max_cp = max_cp

    -- 2. 打开 IDX2
    local f_idx = io.open(idx_bin_path, "rb")
    if not f_idx then
        if file_meta then file_meta:close(); file_meta = nil end
        return false
    end
    local idx_header = f_idx:read(16)
    if not idx_header or #idx_header < 16 then
        f_idx:close()
        if file_meta then file_meta:close(); file_meta = nil end
        return false
    end
    local idx_magic, _num_keys, keys_dir_len, py_block_len = string.unpack("<c4III", idx_header, 1)
    if idx_magic ~= "IDX2" then
        f_idx:close()
        if file_meta then file_meta:close(); file_meta = nil end
        return false
    end
    file_idx = f_idx

    -- 3. 解析 @py 拼音块
    local py_block = file_idx:read(py_block_len)
    local pos_py = 1
    local len_py = #py_block
    while pos_py <= len_py do
        local nl = string.find(py_block, "\n", pos_py, true) or (len_py + 1)
        local end_line = (string.byte(py_block, nl - 1) == 13) and (nl - 2) or (nl - 1)
        if end_line >= pos_py then
            local line = string.sub(py_block, pos_py, end_line)
            local t1 = string.find(line, "\t", 1, true)
            if t1 then
                local t2 = string.find(line, "\t", t1 + 1, true)
                if t2 then
                    local py = string.sub(line, t1 + 1, t2 - 1)
                    local comps_str = string.sub(line, t2 + 1)
                    local comps = {}
                    for c in string.gmatch(comps_str, "%S+") do table.insert(comps, c) end
                    py_map[py] = comps
                    valid_pinyins[py] = true
                end
            end
        end
        pos_py = nl + 1
    end

    -- 4. 解析 keys_dir
    index_offsets = {}
    index_counts = {}
    local keys_dir = file_idx:read(keys_dir_len)
    local pos_dir = 1
    local dir_end = #keys_dir
    postings_base = 16 + py_block_len + keys_dir_len
    while pos_dir <= dir_end - 10 do
        local k_len = string.unpack("<I2", keys_dir, pos_dir)
        pos_dir = pos_dir + 2
        local k = string.sub(keys_dir, pos_dir, pos_dir + k_len - 1)
        pos_dir = pos_dir + k_len
        local off, count = string.unpack("<I4I4", keys_dir, pos_dir)
        pos_dir = pos_dir + 8
        index_offsets[k] = off
        index_counts[k] = count
    end

    -- 5. 完整解析 ids_custom.txt（-block / -er / -er 阝 / 阝 -er / 阝 =fu yi / 追加别名）
    local custom_path = find_data_file("ids_custom.txt")
    if custom_path then
        local f_cust = io.open(custom_path, "r")
        if f_cust then
            -- 第一遍：清理 + 收集 + 处理 -block
            local lines = {}
            for raw_line in f_cust:lines() do
                local line = string.gsub(raw_line, "#.*$", "")
                line = string.gsub(line, "^%s+", "")
                line = string.gsub(line, "%s+$", "")
                if #line > 0 then
                    table.insert(lines, line)
                    local tokens = {}
                    for tok in string.gmatch(line, "%S+") do table.insert(tokens, tok) end
                    if #tokens >= 2 and (tokens[1] == "-block" or tokens[1] == "-区" or tokens[1] == "-分区") then
                        if env and env.disabled_blocks then
                            for i = 2, #tokens do
                                local bid = BLOCK_ALIAS_MAP[string.lower(tokens[i])]
                                if bid then env.disabled_blocks[bid] = true end
                            end
                        end
                    end
                end
            end
            f_cust:close()

            -- 第二遍：应用 py_map 变更
            for _, line in ipairs(lines) do
                local tokens = {}
                for tok in string.gmatch(line, "%S+") do table.insert(tokens, tok) end
                if #tokens > 0 then
                    local first = tokens[1]
                    if first ~= "-block" and first ~= "-区" and first ~= "-分区" then
                        if string.sub(first, 1, 1) == "-" and #tokens == 1 then
                            -- 全局禁用读音: -er
                            local del_py = string.lower(string.sub(first, 2))
                            py_map[del_py] = nil
                            valid_pinyins[del_py] = nil
                        elseif string.sub(first, 1, 1) == "-" and #tokens >= 2 then
                            -- 禁用指定拼音下的部件: -er 阝
                            local del_py = string.lower(string.sub(first, 2))
                            if py_map[del_py] then
                                local del_set = {}
                                for i = 2, #tokens do del_set[tokens[i]] = true end
                                local new_list = {}
                                for _, c in ipairs(py_map[del_py]) do
                                    if not del_set[c] then table.insert(new_list, c) end
                                end
                                if #new_list > 0 then
                                    py_map[del_py] = new_list
                                else
                                    py_map[del_py] = nil
                                    valid_pinyins[del_py] = nil
                                end
                            end
                        else
                            -- 部件开头
                            local comp = first
                            local has_override = false
                            for i = 2, #tokens do
                                if string.sub(tokens[i], 1, 1) == "=" then
                                    has_override = true; break
                                end
                            end
                            if has_override then
                                -- 覆盖重设: 阝 =fu yi
                                for p, clist in pairs(py_map) do
                                    local filtered = {}
                                    for _, c in ipairs(clist) do
                                        if c ~= comp then table.insert(filtered, c) end
                                    end
                                    if #filtered > 0 then
                                        py_map[p] = filtered
                                    else
                                        py_map[p] = nil
                                        valid_pinyins[p] = nil
                                    end
                                end
                                for i = 2, #tokens do
                                    local tok = tokens[i]
                                    local new_py
                                    if string.sub(tok, 1, 1) == "=" then
                                        new_py = string.lower(string.sub(tok, 2))
                                    else
                                        new_py = string.lower(tok)
                                    end
                                    if #new_py > 0 then
                                        if not py_map[new_py] then py_map[new_py] = {} end
                                        local exists = false
                                        for _, c in ipairs(py_map[new_py]) do
                                            if c == comp then exists = true; break end
                                        end
                                        if not exists then table.insert(py_map[new_py], comp) end
                                        valid_pinyins[new_py] = true
                                    end
                                end
                            else
                                for i = 2, #tokens do
                                    local tok = tokens[i]
                                    if string.sub(tok, 1, 1) == "-" then
                                        -- 精准删除: 阝 -er
                                        local del_py = string.lower(string.sub(tok, 2))
                                        if py_map[del_py] then
                                            local new_list = {}
                                            for _, c in ipairs(py_map[del_py]) do
                                                if c ~= comp then table.insert(new_list, c) end
                                            end
                                            if #new_list > 0 then
                                                py_map[del_py] = new_list
                                            else
                                                py_map[del_py] = nil
                                                valid_pinyins[del_py] = nil
                                            end
                                        end
                                    else
                                        -- 追加别名: 氵 sandianshui
                                        local add_py = string.lower(tok)
                                        if not py_map[add_py] then py_map[add_py] = {} end
                                        local exists = false
                                        for _, c in ipairs(py_map[add_py]) do
                                            if c == comp then exists = true; break end
                                        end
                                        if not exists then table.insert(py_map[add_py], comp) end
                                        valid_pinyins[add_py] = true
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    prefix_comps_cache = {}
    data_loaded = true
    collectgarbage("step")
    return true
end

--------------------------------------------------------------------------------
-- 双拼解码器（小鹤 flypy / 自然码 zrm / 小禾 cropy）
--------------------------------------------------------------------------------
local function decode_flypy_syllable(code)
    if #code ~= 2 then return nil end
    local c1 = string.sub(code, 1, 1)
    local c2 = string.sub(code, 2, 2)
    if c1 == "a" then
        if c2 == "a" then return "a" end
        if c2 == "i" or c2 == "n" or c2 == "o" then return "a" .. c2 end
        if c2 == "h" then return "ang" end
    elseif c1 == "o" then
        if c2 == "o" then return "o" end
        if c2 == "u" then return "ou" end
    elseif c1 == "e" then
        if c2 == "e" then return "e" end
        if c2 == "i" or c2 == "n" or c2 == "r" then return "e" .. c2 end
        if c2 == "g" then return "eng" end
    end
    local init = SP_INIT_MAP[c1] or c1
    local final
    if c2 == "k" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "uai" or "ing"
    elseif c2 == "l" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h" or init == "w") and "uang" or "iang"
    elseif c2 == "x" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "ua" or "ia"
    elseif c2 == "s" then
        final = (init == "j" or init == "q" or init == "x") and "iong" or "ong"
    elseif c2 == "t" then
        final = "ue"
    elseif c2 == "v" then
        if init == "n" or init == "l" then return init .. "v"
        elseif init == "j" or init == "q" or init == "x" or init == "y" then return init .. "u"
        else final = "ui" end
    elseif c2 == "o" then
        final = (init == "b" or init == "p" or init == "m" or init == "f") and "o" or "uo"
    else
        final = FLYPY_F_MAP[c2]
    end
    if not final then return nil end
    if (init == "j" or init == "q" or init == "x" or init == "y") and final == "ue" then return init .. "ue" end
    if (init == "l" or init == "n") and final == "ue" then return init .. "ve" end
    return init .. final
end

local function decode_zrm_syllable(code)
    if #code ~= 2 then return nil end
    local c1 = string.sub(code, 1, 1)
    local c2 = string.sub(code, 2, 2)
    if c1 == "a" then
        if c2 == "a" then return "a" end
        if c2 == "i" or c2 == "n" or c2 == "o" then return "a" .. c2 end
        if c2 == "h" then return "ang" end
    elseif c1 == "o" then
        if c2 == "o" then return "o" end
        if c2 == "u" then return "ou" end
    elseif c1 == "e" then
        if c2 == "e" then return "e" end
        if c2 == "i" or c2 == "n" or c2 == "r" then return "e" .. c2 end
        if c2 == "g" then return "eng" end
    end
    local init = SP_INIT_MAP[c1] or c1
    local final
    if c2 == "y" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "uai" or "ing"
    elseif c2 == "d" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "uang" or "iang"
    elseif c2 == "w" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "ua" or "ia"
    elseif c2 == "s" then
        final = (init == "j" or init == "q" or init == "x") and "iong" or "ong"
    elseif c2 == "t" then
        final = "ue"
    elseif c2 == "v" then
        if init == "n" or init == "l" then return init .. "v"
        elseif init == "j" or init == "q" or init == "x" or init == "y" then return init .. "u"
        else final = "ui" end
    elseif c2 == "o" then
        final = (init == "b" or init == "p" or init == "m" or init == "f") and "o" or "uo"
    else
        final = ZRM_F_MAP[c2]
    end
    if not final then return nil end
    return init .. final
end

-- 小禾双拼 cropy：t → ve（n/l 的 üe）；w → ei / ue（j/q/x/y）
local function decode_cropy_syllable(code)
    if #code ~= 2 then return nil end
    local c1 = string.sub(code, 1, 1)
    local c2 = string.sub(code, 2, 2)
    if c1 == "a" then
        if c2 == "a" then return "a" end
        if c2 == "i" or c2 == "n" or c2 == "o" then return "a" .. c2 end
        if c2 == "h" then return "ang" end
    elseif c1 == "o" then
        if c2 == "o" then return "o" end
        if c2 == "u" then return "ou" end
    elseif c1 == "e" then
        if c2 == "e" then return "e" end
        if c2 == "i" or c2 == "n" or c2 == "r" then return "e" .. c2 end
        if c2 == "g" then return "eng" end
    end
    local init = SP_INIT_MAP[c1] or c1
    local final
    if c2 == "k" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "uai" or "ing"
    elseif c2 == "l" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h" or init == "w") and "uang" or "iang"
    elseif c2 == "x" then
        final = (init == "zh" or init == "ch" or init == "sh" or init == "g" or init == "k" or init == "h") and "ua" or "ia"
    elseif c2 == "s" then
        final = (init == "j" or init == "q" or init == "x") and "iong" or "ong"
    elseif c2 == "t" then
        final = "ve"
    elseif c2 == "v" then
        if init == "n" or init == "l" then return init .. "v"
        elseif init == "j" or init == "q" or init == "x" or init == "y" then return init .. "u"
        else final = "ui" end
    elseif c2 == "o" then
        final = (init == "b" or init == "p" or init == "m" or init == "f") and "o" or "uo"
    elseif c2 == "w" then
        if init == "j" or init == "q" or init == "x" or init == "y" then final = "ue"
        else final = "ei" end
    else
        final = CROPY_F_MAP[c2]
    end
    if not final then return nil end
    if (init == "j" or init == "q" or init == "x" or init == "y") and final == "ue" then return init .. "ue" end
    if (init == "l" or init == "n") and final == "ue" then return init .. "ve" end
    return init .. final
end

local function decode_shuangpin_string(s, scheme)
    local decoder
    if scheme == "zrm" then decoder = decode_zrm_syllable
    elseif scheme == "cropy" then decoder = decode_cropy_syllable
    else decoder = decode_flypy_syllable end
    local parts = {}
    if string.find(s, "['/%._%s]") then
        for p in string.gmatch(s, "[^'/%._%s]+") do table.insert(parts, p) end
    else
        table.insert(parts, s)
    end
    local res = {}
    for _, part in ipairs(parts) do
        local n_part = #part
        local pairs_count = math.floor(n_part / 2)
        for i = 0, pairs_count - 1 do
            local pair = string.sub(part, i * 2 + 1, i * 2 + 2)
            local syl = decoder(pair)
            if syl and valid_pinyins[syl] then
                table.insert(res, syl)
            else
                return nil
            end
        end
        if n_part % 2 == 1 then
            local rem = string.sub(part, n_part, n_part)
            local init = SP_INITIALS[rem] or rem
            table.insert(res, init)
        end
    end
    return res
end

--------------------------------------------------------------------------------
-- 全拼最优切分（DP，只保留最优路径）
--------------------------------------------------------------------------------
local zero_initials = {
    ["a"] = true, ["o"] = true, ["e"] = true,
    ["ai"] = true, ["ao"] = true, ["an"] = true, ["ang"] = true,
    ["ou"] = true, ["er"] = true, ["en"] = true, ["eng"] = true, ["ei"] = true
}
local initials_2 = { ["zh"] = true, ["ch"] = true, ["sh"] = true }
local initials_1 = {
    ["b"] = true, ["p"] = true, ["m"] = true, ["f"] = true,
    ["d"] = true, ["t"] = true, ["n"] = true, ["l"] = true,
    ["g"] = true, ["k"] = true, ["h"] = true, ["j"] = true,
    ["q"] = true, ["x"] = true, ["r"] = true, ["z"] = true,
    ["c"] = true, ["s"] = true, ["y"] = true, ["w"] = true
}

local function segment_pinyin_optimal(s)
    local n = #s
    if n == 0 then return {} end
    local memo = {}
    local function dfs(idx)
        if idx > n then return 0, {} end
        if memo[idx] then return memo[idx].cost, memo[idx].segs end
        local best_cost = 1e9
        local best_segs = nil
        local max_len = math.min(6, n - idx + 1)
        for l = max_len, 1, -1 do
            local sub = string.sub(s, idx, idx + l - 1)
            local is_valid = (valid_pinyins[sub] == true) or (py_map[sub] ~= nil)
            local is_single_init = (l == 1 and initials_1[sub]) or (l == 2 and initials_2[sub])
            if is_valid or is_single_init then
                local step_cost = 0
                if is_valid then
                    step_cost = 10 - l
                    if zero_initials[sub] and idx > 1 then
                        local prev_char = string.sub(s, idx - 1, idx - 1)
                        if prev_char == "n" or prev_char == "g" or prev_char == "r" then
                            step_cost = step_cost + 15
                        end
                    end
                else
                    step_cost = 30 + (idx <= 2 and 0 or (idx * 2))
                end
                local rem_cost, rem_segs = dfs(idx + l)
                local total_cost = step_cost + rem_cost
                if total_cost < best_cost then
                    best_cost = total_cost
                    local cur_segs = { sub }
                    for _, seg in ipairs(rem_segs) do table.insert(cur_segs, seg) end
                    best_segs = cur_segs
                end
            end
        end
        if not best_segs then
            local sub = string.sub(s, idx, idx)
            local rem_cost, rem_segs = dfs(idx + 1)
            best_cost = 100 + rem_cost
            best_segs = { sub }
            for _, seg in ipairs(rem_segs) do table.insert(best_segs, seg) end
        end
        memo[idx] = { cost = best_cost, segs = best_segs }
        return best_cost, best_segs
    end
    local _, result = dfs(1)
    return result
end

local seg_cache = {}
local seg_cache_order = {}
local SEG_CACHE_MAX = 2048
local function set_seg_cache(key, val)
    if not seg_cache[key] then
        table.insert(seg_cache_order, key)
        if #seg_cache_order > SEG_CACHE_MAX then
            local oldest = table.remove(seg_cache_order, 1)
            seg_cache[oldest] = nil
        end
    end
    seg_cache[key] = val
end

local function segment_alpha_string(chunk, pinyin_mode)
    local seg_key = chunk .. ":" .. (pinyin_mode or "")
    local cached = seg_cache[seg_key]
    if cached then return cached end
    local items = {}
    if #chunk == 0 then return items end

    if py_map[chunk] and (pinyin_mode == "quanpin" or #chunk == 2) then
        table.insert(items, chunk)
        set_seg_cache(seg_key, items)
        return items
    end

    if pinyin_mode == "flypy" or pinyin_mode == "zrm" or pinyin_mode == "cropy" then
        local sp_segs = decode_shuangpin_string(chunk, pinyin_mode)
        if sp_segs and #sp_segs > 0 then
            set_seg_cache(seg_key, sp_segs)
            return sp_segs
        end
        local segs = segment_pinyin_optimal(chunk)
        if segs then
            set_seg_cache(seg_key, segs)
            return segs
        end
        table.insert(items, chunk)
    else
        local segs = segment_pinyin_optimal(chunk)
        if segs and #segs > 0 then
            local has_single_frag = false
            for _, s in ipairs(segs) do
                if #s == 1 and not valid_pinyins[s] then
                    has_single_frag = true; break
                end
            end
            if (has_single_frag or #chunk <= 4) and #chunk >= 2 and (#chunk % 2 == 0) then
                local sp_segs = decode_shuangpin_string(chunk, "flypy")
                if sp_segs and #sp_segs > 0 then
                    local all_valid = true
                    for _, s in ipairs(sp_segs) do
                        if not valid_pinyins[s] then all_valid = false; break end
                    end
                    if all_valid then set_seg_cache(seg_key, sp_segs); return sp_segs end
                end
                local zrm_segs = decode_shuangpin_string(chunk, "zrm")
                if zrm_segs and #zrm_segs > 0 then
                    local all_valid = true
                    for _, s in ipairs(zrm_segs) do
                        if not valid_pinyins[s] then all_valid = false; break end
                    end
                    if all_valid then set_seg_cache(seg_key, zrm_segs); return zrm_segs end
                end
            end
            set_seg_cache(seg_key, segs)
            return segs
        end
        table.insert(items, chunk)
    end
    set_seg_cache(seg_key, items)
    return items
end

local function parse_query(raw_query, env)
    local raw_query = string.lower(raw_query)
    local items = {}
    local pinyin_mode = (env and env.shuangpin_scheme) or "quanpin"
    if env and env.engine and env.engine.context then
        local ctx = env.engine.context
        if ctx:get_option("shuangpin_cropy") or ctx:get_option("cropy") then
            pinyin_mode = "cropy"
        elseif ctx:get_option("shuangpin_flypy") or ctx:get_option("double_pinyin") then
            pinyin_mode = "flypy"
        elseif ctx:get_option("shuangpin_zrm") then
            pinyin_mode = "zrm"
        elseif ctx:get_option("pinyin_quanpin") then
            pinyin_mode = "quanpin"
        end
    end

    if string.find(raw_query, "['/%._%s]") then
        for part in string.gmatch(raw_query, "[^'/%._%s]+") do
            if #part > 0 then
                local part_items = segment_alpha_string(part, pinyin_mode)
                for _, it in ipairs(part_items) do table.insert(items, it) end
            end
        end
    elseif string.match(raw_query, "^[a-z]+$") then
        items = segment_alpha_string(raw_query, pinyin_mode)
    else
        local cur_alpha = ""
        for _, ch in ipairs(utf8_chars(raw_query)) do
            local b = string.byte(ch)
            if b and b >= 97 and b <= 122 then
                cur_alpha = cur_alpha .. ch
            else
                if #cur_alpha > 0 then
                    local segs = segment_alpha_string(cur_alpha, pinyin_mode)
                    for _, s in ipairs(segs) do table.insert(items, s) end
                    cur_alpha = ""
                end
                table.insert(items, ch)
            end
        end
        if #cur_alpha > 0 then
            local segs = segment_alpha_string(cur_alpha, pinyin_mode)
            for _, s in ipairs(segs) do table.insert(items, s) end
        end
    end

    local counts = {}
    local order = {}
    for _, item in ipairs(items) do
        if not counts[item] then
            counts[item] = 1
            table.insert(order, item)
        else
            counts[item] = counts[item] + 1
        end
    end
    return items, order, counts
end

--------------------------------------------------------------------------------
-- 倒排候选提取与高性能比对缓存 (ids1.6 优化)
--------------------------------------------------------------------------------
local posting_set_cache = {}
local posting_set_cache_keys = {}
local MAX_POSTING_SET_CACHE = 80

local function get_posting_set(entry)
    local off = entry.off
    local set = posting_set_cache[off]
    if set then return set end

    set = {}
    if file_idx and entry.len > 0 then
        file_idx:seek("set", off)
        local chk = file_idx:read(entry.len)
        if chk then
            local n = math.floor(#chk / 4)
            for i = 1, n do
                local cp = string.unpack("<I4", chk, (i - 1) * 4 + 1)
                set[cp] = true
            end
        end
    end

    if not posting_set_cache[off] then
        table.insert(posting_set_cache_keys, off)
        if #posting_set_cache_keys > MAX_POSTING_SET_CACHE then
            local old_off = table.remove(posting_set_cache_keys, 1)
            posting_set_cache[old_off] = nil
        end
    end
    posting_set_cache[off] = set
    return set
end
local function add_posting_cps(cp_list, seen, limit, key, quota, disabled_blocks)
    local off = index_offsets[key]
    local count = index_counts[key]
    if not off or not count or count == 0 or not file_idx then return end
    local read_count = math.min(count, quota)
    file_idx:seek("set", postings_base + off - 1)
    local chunk = file_idx:read(read_count * 4)
    if not chunk then return end
    local added = 0
    local actual_len = math.floor(#chunk / 4)
    for i = 1, actual_len do
        local cp = string.unpack("<I4", chunk, (i - 1) * 4 + 1)
        if not seen[cp] then
            seen[cp] = true
            local keep = true
            if disabled_blocks then
                local meta = get_meta_info(cp)
                if meta and disabled_blocks[meta.block_id] then keep = false end
            end
            if keep then
                table.insert(cp_list, cp)
                added = added + 1
                if #cp_list >= limit or added >= quota then break end
            end
        end
    end
end

local function add_single_cp(cp_list, seen, limit, ch, disabled_blocks)
    local cp = utf8_codepoint(ch)
    if cp > 0 and not seen[cp] then
        seen[cp] = true
        local keep = true
        if disabled_blocks then
            local meta = get_meta_info(cp)
            if meta and disabled_blocks[meta.block_id] then keep = false end
        end
        if keep then
            table.insert(cp_list, cp)
            if #cp_list >= limit then return true end
        end
    end
    return false
end

-- 融合修复：多重复 key 回退不再产生 nil key
local function get_candidates_for_term(term, count, max_chars, custom_keys, disabled_blocks, is_multi_term)
    local cp_list = {}
    local seen = {}
    local limit = max_chars or 200000
    local cn_term = canon(term)

    if count == 1 and not is_multi_term then
        if (has_index_key(cn_term) or has_index_key(term)) and not term:find("^{") then
            if add_single_cp(cp_list, seen, limit, cn_term, disabled_blocks) then return cp_list end
            if cn_term ~= term then
                if add_single_cp(cp_list, seen, limit, term, disabled_blocks) then return cp_list end
            end
        end
        if py_map[term] then
            for _, comp in ipairs(py_map[term]) do
                if not comp:find("^{") then
                    if add_single_cp(cp_list, seen, limit, comp, disabled_blocks) then return cp_list end
                end
            end
        end
    end

    if custom_keys and #custom_keys > 0 then
        local quota = (#custom_keys <= 2) and limit or math.max(60, math.floor(limit / #custom_keys))
        for _, k in ipairs(custom_keys) do
            add_posting_cps(cp_list, seen, limit, k, quota, disabled_blocks)
            if #cp_list >= limit then break end
        end
        if #cp_list < limit then
            for _, k in ipairs(custom_keys) do
                add_posting_cps(cp_list, seen, limit, k, limit, disabled_blocks)
                if #cp_list >= limit then break end
            end
        end
    else
        -- 修复：先决定 key，不存在则回退到 cn_term / term
        local key = nil
        if count > 1 then
            if has_index_key(cn_term .. "*" .. count) then
                key = cn_term .. "*" .. count
            elseif has_index_key(term .. "*" .. count) then
                key = term .. "*" .. count
            end
        end
        if not key then
            if has_index_key(cn_term) then key = cn_term
            elseif has_index_key(term) then key = term end
        end

        if key then
            add_posting_cps(cp_list, seen, limit, key, limit, disabled_blocks)
        elseif py_map[term] then
            local comps = py_map[term]
            local quota = (#comps <= 2) and limit or math.max(60, math.floor(limit / #comps))
            for _, comp in ipairs(comps) do
                local k = (count > 1) and (comp .. "*" .. count) or comp
                if has_index_key(k) then
                    add_posting_cps(cp_list, seen, limit, k, quota, disabled_blocks)
                elseif count > 1 and has_index_key(comp) then
                    add_posting_cps(cp_list, seen, limit, comp, quota, disabled_blocks)
                end
                if #cp_list >= limit then break end
            end
            if #cp_list < limit then
                for _, comp in ipairs(comps) do
                    local k = (count > 1) and (comp .. "*" .. count) or comp
                    if has_index_key(k) then
                        add_posting_cps(cp_list, seen, limit, k, limit, disabled_blocks)
                    elseif count > 1 and has_index_key(comp) then
                        add_posting_cps(cp_list, seen, limit, comp, limit, disabled_blocks)
                    end
                    if #cp_list >= limit then break end
                end
            end
        else
            local pref_comps = get_comps_by_prefix(term)
            if pref_comps and #pref_comps > 0 then
                local quota = math.max(60, math.floor(limit / math.max(1, #pref_comps)))
                for _, comp in ipairs(pref_comps) do
                    local k = (count > 1) and (comp .. "*" .. count) or comp
                    if has_index_key(k) then
                        add_posting_cps(cp_list, seen, limit, k, quota, disabled_blocks)
                    elseif count > 1 and has_index_key(comp) then
                        add_posting_cps(cp_list, seen, limit, comp, quota, disabled_blocks)
                    end
                    if #cp_list >= limit then break end
                end
            end
        end
    end

    return cp_list
end

local term_matcher_cache = {}
local function get_term_matcher(term)
    local cached = term_matcher_cache[term]
    if cached then return cached end
    local valid_set = {}
    if py_map[term] and #py_map[term] > 0 then
        for _, comp in ipairs(py_map[term]) do
            valid_set[canon(comp)] = true
            valid_set[comp] = true
        end
    else
        local pref_comps = get_comps_by_prefix(term)
        if pref_comps and #pref_comps > 0 then
            for _, comp in ipairs(pref_comps) do
                valid_set[canon(comp)] = true
                valid_set[comp] = true
            end
        else
            valid_set[canon(term)] = true
            valid_set[term] = true
        end
    end
    term_matcher_cache[term] = valid_set
    return valid_set
end

local function build_term_matchers(items)
    local matchers = {}
    for i, term in ipairs(items) do
        matchers[i] = get_term_matcher(term)
    end
    return matchers
end

-- 4+ 部件无序完美匹配的递归回溯：显式传参，避免每次创建闭包
local function perm_dfs(used, comps, matchers, idx, total)
    if idx > total then return true end
    local c = comps[idx]
    for i = 1, total do
        if not used[i] and matchers[i][c] then
            used[i] = true
            if perm_dfs(used, comps, matchers, idx + 1, total) then
                return true
            end
            used[i] = false
        end
    end
    return false
end

-- 融合：补 4+ 部件无序完美匹配
local function check_direct_match(alts, matchers)
    if not alts or #alts == 0 then return 0 end
    local total_terms = #matchers
    local best_match = 0
    for idx_alt = 1, #alts do
        local comps = alts[idx_alt]
        if #comps == total_terms then
            local is_ordered = true
            for i = 1, total_terms do
                if not matchers[i][comps[i]] then
                    is_ordered = false; break
                end
            end
            if is_ordered then return 2 end

            if best_match < 1 then
                if total_terms == 2 then
                    if (matchers[1][comps[1]] and matchers[2][comps[2]]) or
                       (matchers[1][comps[2]] and matchers[2][comps[1]]) then
                        best_match = 1
                    end
                elseif total_terms == 3 then
                    local c1, c2, c3 = comps[1], comps[2], comps[3]
                    local m1, m2, m3 = matchers[1], matchers[2], matchers[3]
                    if (m1[c1] and ((m2[c2] and m3[c3]) or (m2[c3] and m3[c2]))) or
                       (m1[c2] and ((m2[c1] and m3[c3]) or (m2[c3] and m3[c1]))) or
                       (m1[c3] and ((m2[c1] and m3[c2]) or (m2[c2] and m3[c1]))) then
                        best_match = 1
                    end
                else
                    local used = {}
                    if perm_dfs(used, comps, matchers, 1, total_terms) then
                        best_match = 1
                    end
                end
            end
        end
    end
    return best_match
end

local direct_comp_rank_cache = {}
local function get_direct_comp_rank(term)
    local cached = direct_comp_rank_cache[term]
    if cached then return cached end
    local comps = py_map[term] or { term }
    local rank = {}
    local r = 1
    for _, c in ipairs(comps) do
        local cp = utf8_codepoint(c)
        if cp > 0 and not rank[cp] then
            rank[cp] = r
            r = r + 1
        end
    end
    direct_comp_rank_cache[term] = rank
    return rank
end

--------------------------------------------------------------------------------
-- 查询缓存（容量调大）
--------------------------------------------------------------------------------
local query_cache = {}
local query_cache_order = {}
local MAX_QUERY_CACHE = 256

local term_info_cache = {}
local term_info_cache_keys = {}
local MAX_TERM_CACHE = 200

local function search_components(query_str, max_results, env)
    if not query_str or #query_str == 0 then return {} end

    -- 融合：缓存键补全 max_results + 双拼模式 + disabled_blocks
    local cache_key = query_str .. ":" .. tostring(max_results or 60)
    local ctx = env and env.engine and env.engine.context
    if ctx then
        if ctx:get_option("shuangpin_cropy") or ctx:get_option("cropy") then
            cache_key = cache_key .. ":cropy"
        elseif ctx:get_option("shuangpin_flypy") or ctx:get_option("double_pinyin") then
            cache_key = cache_key .. ":flypy"
        elseif ctx:get_option("shuangpin_zrm") then
            cache_key = cache_key .. ":zrm"
        elseif ctx:get_option("pinyin_quanpin") then
            cache_key = cache_key .. ":quanpin"
        end
    elseif env and env.shuangpin_scheme then
        cache_key = cache_key .. ":" .. env.shuangpin_scheme
    end
    if env and env.disabled_blocks and next(env.disabled_blocks) then
        local ks = {}
        for k in pairs(env.disabled_blocks) do ks[#ks+1] = tostring(k) end
        table.sort(ks)
        cache_key = cache_key .. ":blk=" .. table.concat(ks, ",")
    end

    if query_cache[cache_key] then return query_cache[cache_key] end

    local items, order, counts = parse_query(query_str, env)
    if #order == 0 then return {} end

    local matchers = build_term_matchers(items)
    local order_count = #order
    local disabled_blocks = env and env.disabled_blocks

    local function score_candidate(cp)
        local meta = get_meta_info(cp)
        local depth = meta.depth
        local freq = meta.freq
        local ch = utf8.char(cp)
        if order_count == 1 and matchers[1] and (matchers[1][ch] or matchers[1][canon(ch)]) then
            return 30000000 + freq
        end
        local match_type = check_direct_match(get_meta_alts(meta), matchers)
        local common_bonus = (freq >= 1900000) and 5000000 or 0
        if match_type == 2 then
            return 20000000 + common_bonus + freq
        elseif match_type == 1 then
            return 15000000 + common_bonus + freq
        else
            return freq - depth * 1000
        end
    end

    local final_results = {}

    -- 场景 A：单项
    if order_count == 1 then
        local term = order[1]
        local direct_comp_rank = get_direct_comp_rank(term)
        local is_pref = (valid_pinyins[term] == nil) and (get_comps_by_prefix(term) ~= nil)
        local limit = is_pref and math.max(250, (max_results or 60) * 3) or math.max(480, (max_results or 60) * 8)
        local cands = get_candidates_for_term(term, counts[term] or 1, limit, nil, disabled_blocks)
        if #cands == 0 then return {} end
        local scored = {}
        for _, cp in ipairs(cands) do
            local sc = score_candidate(cp)
            if direct_comp_rank and direct_comp_rank[cp] then
                local meta = get_meta_info(cp)
                local f = (meta and meta.freq) or 0
                sc = 40000000 + f - direct_comp_rank[cp] * 10
            end
            table.insert(scored, { cp = cp, sc = sc })
        end
        table.sort(scored, function(a, b) return a.sc > b.sc end)
        local lim = math.min(#scored, max_results or 60)
        for i = 1, lim do table.insert(final_results, scored[i].cp) end

        if not query_cache[cache_key] then
            table.insert(query_cache_order, cache_key)
            if #query_cache_order > MAX_QUERY_CACHE then
                local oldest = table.remove(query_cache_order, 1)
                query_cache[oldest] = nil
            end
        end
        query_cache[cache_key] = final_results
        return final_results
    end

    -- 场景 B：多项
    local terms_info = {}
    for _, term in ipairs(order) do
        local mult = counts[term] or 1
        local term_cache_key = term .. ":" .. mult
        local t_info = term_info_cache[term_cache_key]
        if not t_info then
            local comps = {}
            local is_pref = false
            local cn_term = canon(term)
            if py_map[term] then
                comps = py_map[term]
            elseif has_index_key(cn_term) or has_index_key(term) then
                comps = (cn_term ~= term) and { cn_term, term } or { term }
            elseif get_comps_by_prefix(term) then
                comps = get_comps_by_prefix(term)
                is_pref = true
            else
                comps = (cn_term ~= term) and { cn_term, term } or { term }
            end
            if #comps == 0 then return {} end

            local seen_post_keys = {}
            local unique_post_keys = {}
            local max_keys = is_pref and 25 or 250
            for _, c in ipairs(comps) do
                local cn = canon(c)
                local k1 = (mult > 1) and (c .. "*" .. mult) or c
                local k2 = (mult > 1) and (cn .. "*" .. mult) or cn
                local use_k = has_index_key(k2) and k2 or (has_index_key(k1) and k1 or nil)
                if use_k and not seen_post_keys[use_k] then
                    seen_post_keys[use_k] = true
                    table.insert(unique_post_keys, use_k)
                    if #unique_post_keys >= max_keys then break end
                end
            end

            local sz = 0
            local post_entries = {}
            for _, key in ipairs(unique_post_keys) do
                local off = index_offsets[key]
                local cnt = index_counts[key]
                if off and cnt and cnt > 0 then
                    sz = sz + cnt * 4
                    table.insert(post_entries, {
                        off = postings_base + off - 1,
                        cnt = cnt,
                        len = cnt * 4
                    })
                end
            end

            local comp_set = {}
            local comp_cps = {}
            for _, c in ipairs(comps) do
                comp_set[canon(c)] = true
                comp_set[c] = true
                local c_cp = utf8_codepoint(c)
                if c_cp > 0 then comp_cps[c_cp] = true end
            end

            t_info = {
                term = term, comps = comps,
                unique_post_keys = unique_post_keys, is_pref = is_pref,
                count = #comps, mult = mult, data_size = sz,
                comp_set = comp_set, comp_cps = comp_cps,
                post_entries = post_entries
            }
            if not term_info_cache[term_cache_key] then
                table.insert(term_info_cache_keys, term_cache_key)
                if #term_info_cache_keys > MAX_TERM_CACHE then
                    local old_k = table.remove(term_info_cache_keys, 1)
                    term_info_cache[old_k] = nil
                end
            end
            term_info_cache[term_cache_key] = t_info
        end
        table.insert(terms_info, t_info)
    end

    table.sort(terms_info, function(a, b) return a.data_size < b.data_size end)

    local base = terms_info[1]
    local has_any_pref = false
    for _, info in ipairs(terms_info) do
        if info.is_pref then has_any_pref = true; break end
    end
    local base_limit = has_any_pref and 1500 or 8000
    local current_candidates = get_candidates_for_term(base.term, base.mult, base_limit, base.unique_post_keys, disabled_blocks, true)
    if #current_candidates == 0 then return {} end

    for idx = 2, #terms_info do
        if #current_candidates == 0 then break end
        local t_info = terms_info[idx]
        local comp_cps = t_info.comp_cps
        local comp_set = t_info.comp_set
        local post_entries = t_info.post_entries

        local filtered = {}
        local post_chunks = nil
        for _, cp in ipairs(current_candidates) do
            local matched = false
            local meta = get_meta_info(cp)
            if meta then
                local alts = get_meta_alts(meta)
                for _, alt in ipairs(alts) do
                    for _, comp in ipairs(alt) do
                        if comp_set[comp] or comp_set[canon(comp)] then
                            matched = true; break
                        end
                    end
                    if matched then break end
                end
            end
            if not matched and post_entries and #post_entries > 0 then
                for _, entry in ipairs(post_entries) do
                    local p_set = get_posting_set(entry)
                    if p_set and p_set[cp] then
                        matched = true
                        break
                    end
                end
            end
            if matched then table.insert(filtered, cp) end
        end
        current_candidates = filtered
    end

    if #current_candidates == 0 then return {} end

    local scored = {}
    local score_limit = math.min(#current_candidates, has_any_pref and 500 or 1500)
    for i = 1, score_limit do
        local cp = current_candidates[i]
        table.insert(scored, { cp = cp, sc = score_candidate(cp) })
    end
    table.sort(scored, function(a, b) return a.sc > b.sc end)

    local lim = math.min(#scored, max_results or 60)
    for i = 1, lim do table.insert(final_results, scored[i].cp) end

    if not query_cache[cache_key] then
        table.insert(query_cache_order, cache_key)
        if #query_cache_order > MAX_QUERY_CACHE then
            local oldest = table.remove(query_cache_order, 1)
            query_cache[oldest] = nil
        end
    end
    query_cache[cache_key] = final_results
    return final_results
end

--------------------------------------------------------------------------------
-- Rime 接口（init / func / fini / preload）
--------------------------------------------------------------------------------
function ids_search.preload()
    -- 保持纯懒加载，不做任何磁盘预热
end

function ids_search.init(env)
    if not env then return end
    local config = env.engine and env.engine.schema and env.engine.schema.config
    if config then
        env.prefix = config:get_string("ids_search/prefix") or ""
        env.max_results = config:get_int("ids_search/max_results") or 60
        env.shuangpin_scheme = config:get_string("ids_search/shuangpin_scheme") or "flypy"

        local p = config:get_bool("ids_search/show_pinyin")
        env.show_pinyin = (p == nil) and true or p
        local s = config:get_bool("ids_search/show_ids")
        env.show_ids = (s == nil) and true or s
        local u = config:get_bool("ids_search/show_unicode")
        env.show_unicode = (u == nil) and true or u
        local b = config:get_bool("ids_search/show_block")
        env.show_block = (b == nil) and true or b
        env.tag = config:get_string("ids_search/tag") or ""
        env.tips = config:get_string("ids_search/tips") or "〔部件检字〕"

        for i = 0, 30 do
            local s_name = config:get_string("switches/@" .. i .. "/name")
            if s_name == "show_pinyin" then env.has_sw_pinyin = true end
            if s_name == "show_ids" then env.has_sw_ids = true end
            if s_name == "show_unicode" then env.has_sw_unicode = true end
            if s_name == "show_block" then env.has_sw_block = true end
            if not s_name and not config:get_value("switches/@" .. i) then break end
        end

        env.disabled_blocks = {}
        local list = config:get_list("ids_search/disabled_blocks")
        if list then
            for i = 0, list.size - 1 do
                local val = list:get_value_at(i)
                if val then
                    local bid = BLOCK_ALIAS_MAP[string.lower(val:get_string())]
                    if bid then env.disabled_blocks[bid] = true end
                end
            end
        end
        local str = config:get_string("ids_search/disabled_blocks")
        if str and #str > 0 then
            for b in string.gmatch(str, "[^,%s]+") do
                local bid = BLOCK_ALIAS_MAP[string.lower(b)]
                if bid then env.disabled_blocks[bid] = true end
            end
        end
    else
        env.prefix = ""
        env.tag = ""
        env.max_results = 60
        env.shuangpin_scheme = "flypy"
        env.show_pinyin = true
        env.show_ids = true
        env.show_unicode = true
        env.show_block = true
        env.tips = "〔部件检字〕"
        env.disabled_blocks = {}
    end

    if env.prefix and #env.prefix > 0 then
        env.prefix_len = #env.prefix
        env.prefix_byte = string.byte(env.prefix, 1)
    else
        env.prefix_len = 0
        env.prefix_byte = nil
    end
end

function ids_search.func(input, seg, env)
    -- tag 快速门禁
    if env and env.tag and env.tag ~= "" and seg and not seg:has_tag(env.tag) then
        return
    end

    local prefix = (env and env.prefix) or ""
    local plen = (env and env.prefix_len) or 0
    local query_raw = input

    if plen > 0 then
        if string.byte(input, 1) ~= env.prefix_byte then return end
        if plen > 1 and string.sub(input, 1, plen) ~= prefix then return end
        query_raw = string.sub(input, plen + 1)
        local composition = env and env.engine and env.engine.context and env.engine.context.composition
        if composition and not composition:empty() then
            local segment = composition:back()
            if segment then segment.prompt = env.tips or "〔部件检字〕" end
        end
    end

    if #query_raw == 0 then
        -- 融合：空查询只出提示，不加载数据
        local start_pos = seg and seg.start or 0
        local end_pos = seg and seg._end or #input
        local prompt_cand = Candidate("ids_search", start_pos, end_pos, "", env.tips or "〔部件检字〕")
        prompt_cand.preedit = input .. " " .. (env.tips or "〔部件检字〕")
        yield(prompt_cand)
        return
    end

    if not ensure_data_loaded(env) then return end

    local max_count = (env and env.max_results) or 60
    local results = search_components(query_raw, max_count, env)
    if #results == 0 then return end

    local context = env and env.engine and env.engine.context
    local show_pinyin = env and env.show_pinyin
    if show_pinyin == nil then show_pinyin = true end
    if env and env.has_sw_pinyin and context then
        show_pinyin = context:get_option("show_pinyin")
    end
    local show_ids = env and env.show_ids
    if show_ids == nil then show_ids = true end
    if env and env.has_sw_ids and context then
        show_ids = context:get_option("show_ids")
    end
    local show_unicode = env and env.show_unicode
    if show_unicode == nil then show_unicode = true end
    if env and env.has_sw_unicode and context then
        show_unicode = context:get_option("show_unicode")
    end
    local show_block = env and env.show_block
    if show_block == nil then show_block = true end
    if env and env.has_sw_block and context then
        show_block = context:get_option("show_block")
    end

    for _, cp in ipairs(results) do
        local meta = get_meta_info(cp)
        local parts = {}
        if show_pinyin and meta.py and #meta.py > 0 then table.insert(parts, meta.py) end
        if show_ids and meta.ids and #meta.ids > 0 then table.insert(parts, meta.ids) end
        if show_unicode then table.insert(parts, string.format("%04X", cp)) end
        if show_block then
            local bname = BLOCK_NAMES[meta.block_id] or "基本"
            table.insert(parts, bname)
        end
        local comment = table.concat(parts, " ")

        local ch = utf8.char(cp)
        local start_pos = seg and seg.start or 0
        local end_pos = seg and seg._end or #input
        local cand = Candidate("ids_search", start_pos, end_pos, ch, comment)
        cand.quality = 100
        if plen > 0 then cand.preedit = query_raw end
        yield(cand)
    end
end

function ids_search.fini(env)
    -- 保持文件流常驻，避免跨窗口重复打开
end

setmetatable(ids_search, {
    __call = function(self, input, seg, env)
        return self.func(input, seg, env)
    end
})

return { init = ids_search.init, func = ids_search.func, fini = ids_search.fini, preload = ids_search.preload }