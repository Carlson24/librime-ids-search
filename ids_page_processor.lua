-- ids_page_processor.lua
-- 专门用于 ids_search 方案（及宿主方案挂载副方案）的翻页按键守卫处理器
-- 解决两个核心问题：
-- 1. 在宿主方案（如万象拼音 wanxiang_pro）挂载大写 I 前缀时，输入 Ixbhd 后按句号 (.)，
--    因 recognizer 的 uppercase 正则 ([A-Z][-_+.'0-9A-Za-z]*$) 优先匹配，导致句号被当作英文单词字符吸入编码串（变成 xbhd.）而无法翻页；
-- 2. 候选末页按句号/第一页按逗号时，按键漏传给 punctuator 导致误上屏或符号泄漏。

local M = {}

local K_REJECT = 0
local K_ACCEPT = 1
local K_NOOP = 2

function M.init(env)
    env.paging = false
    local config = env.engine and env.engine.schema and env.engine.schema.config
    if config then
        env.prefix = config:get_string("ids_search/prefix") or ""
        env.schema_id = (env.engine.schema and env.engine.schema.schema_id) or ""
    else
        env.prefix = ""
        env.schema_id = ""
    end
end

function M.func(key_event, env)
    -- 忽略按键释放事件
    if key_event:release() then
        return K_NOOP
    end

    local code = key_event.keycode

    -- 极速短路：仅对 6 个翻页键响应，其余所有常规按键（a-z、0-9、空格、回车、退格等）纳秒级直接放行，0 延迟
    -- 键码：0x2e (.), 0x2c (,), 0x3d (=), 0x2d (-), 0x5d (]), 0x5b ([)
    if code ~= 0x2e and code ~= 0x2c and code ~= 0x3d and code ~= 0x2d and code ~= 0x5d and code ~= 0x5b then
        return K_NOOP
    end

    -- 若含有 Ctrl / Alt 修饰键，直接放行（确保 Ctrl+. 切换中英标点等功能不受干扰）
    if key_event:ctrl() or key_event:alt() then
        return K_NOOP
    end

    -- 递归守卫
    if env.paging then
        return K_NOOP
    end

    local context = env.engine and env.engine.context
    if not context then
        return K_NOOP
    end

    -- 仅在正在输入且有候选菜单时生效
    if not context:is_composing() or not context:has_menu() then
        return K_NOOP
    end

    local input = context.input or ""

    -- 判断当前是否处于 ids_search 检字状态：
    -- 1) 独立方案 (schema_id 为 ids_search)
    -- 2) 宿主方案副方案 (input 以前缀 env.prefix 开头)
    local is_ids = false
    if env.schema_id == "ids_search" then
        is_ids = true
    elseif env.prefix and env.prefix ~= "" then
        if string.sub(input, 1, #env.prefix) == env.prefix then
            is_ids = true
        end
    end

    if not is_ids then
        local comp = context.composition
        if comp and not comp:empty() then
            local seg = comp:back()
            if seg and seg:has_tag("ids_search") then
                is_ids = true
            end
        end
    end

    -- 若不在检字状态，彻底放行，严禁干扰宿主方案原生按键逻辑
    if not is_ids then
        return K_NOOP
    end

    -- 下翻页键：句号 (.)、等号 (=)、右中括号 (])
    -- 键码：0x2e (.), 0x3d (=), 0x5d (])
    if code == 0x2e or code == 0x3d or code == 0x5d then
        env.paging = true
        env.engine:process_key(KeyEvent("Page_Down"))
        env.paging = false
        -- 核心防御：彻底吞噬按键，防止被 recognizer 的 uppercase 正则劫持追加至编码，也杜绝漏传给 punctuator
        return K_ACCEPT
    end

    -- 上翻页键：逗号 (,)、减号 (-)、左中括号 ([)
    -- 键码：0x2c (,), 0x2d (-), 0x5b ([)
    if code == 0x2c or code == 0x2d or code == 0x5b then
        env.paging = true
        env.engine:process_key(KeyEvent("Page_Up"))
        env.paging = false
        -- 核心防御：即便处于第一页，亦彻底吞噬按键，杜绝标点泄漏
        return K_ACCEPT
    end

    return K_NOOP
end

function M.fini(env)
end

return M
