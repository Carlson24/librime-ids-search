//
// ids_search_translator.cc — 部件检字翻译器（C++，IDS3
// 数据格式，面向查询延迟）。
//
#include "ids_search_translator.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/service.h>
#include <rime/translation.h>

#include "ids_store.h"

namespace ids_search {

namespace {

using rime::an;
using rime::hash_map;
using rime::hash_set;
using rime::New;

// ---------------------------------------------------------------------------
// UTF-8 帮助函数
// ---------------------------------------------------------------------------

uint32_t DecodeNext(const std::string& s, size_t* i) {
  size_t n = s.size();
  if (*i >= n)
    return 0;
  unsigned char b1 = static_cast<unsigned char>(s[*i]);
  if (b1 < 0x80) {
    *i += 1;
    return b1;
  }
  if (b1 < 0xE0) {
    if (*i + 1 >= n)
      return 0;
    uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
    *i += 2;
    return (b1 - 0xC0) * 0x40 + (b2 - 0x80);
  }
  if (b1 < 0xF0) {
    if (*i + 2 >= n)
      return 0;
    uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
    uint32_t b3 = static_cast<unsigned char>(s[*i + 2]);
    *i += 3;
    return (b1 - 0xE0) * 0x1000 + (b2 - 0x80) * 0x40 + (b3 - 0x80);
  }
  if (*i + 3 >= n)
    return 0;
  uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
  uint32_t b3 = static_cast<unsigned char>(s[*i + 2]);
  uint32_t b4 = static_cast<unsigned char>(s[*i + 3]);
  *i += 4;
  return (b1 - 0xF0) * 0x40000 + (b2 - 0x80) * 0x1000 + (b3 - 0x80) * 0x40 +
         (b4 - 0x80);
}

uint32_t CpOf(const std::string& ch) {
  size_t i = 0;
  return DecodeNext(ch, &i);
}

std::string CpToUtf8(uint32_t cp) {
  char buf[4];
  size_t n;
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    n = 1;
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 4;
  }
  return std::string(buf, n);
}

std::vector<std::string> Utf8Chars(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0, n = s.size();
  while (i < n) {
    size_t old = i;
    DecodeNext(s, &i);
    if (i == old) {
      out.push_back(s.substr(old, 1));
      i = old + 1;
    } else {
      out.push_back(s.substr(old, i - old));
    }
  }
  return out;
}

std::string AsciiLower(const std::string& s) {
  std::string r = s;
  for (auto& ch : r) {
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch + 32);
  }
  return r;
}

bool IsAsciiAlphaOnly(const std::string& s) {
  for (unsigned char c : s) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
      return false;
  }
  return !s.empty();
}

bool HasSeparator(const std::string& s) {
  for (unsigned char c : s) {
    if (c == '\'' || c == '/' || c == '.' || c == '_' || c == ' ' ||
        c == '\t' || c == '\r' || c == '\n')
      return true;
  }
  return false;
}

std::vector<std::string> SplitSeparator(const std::string& s) {
  std::vector<std::string> parts;
  std::string cur;
  for (unsigned char c : s) {
    if (c == '\'' || c == '/' || c == '.' || c == '_' || c == ' ' ||
        c == '\t' || c == '\r' || c == '\n') {
      if (!cur.empty()) {
        parts.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(static_cast<char>(c));
    }
  }
  if (!cur.empty())
    parts.push_back(cur);
  return parts;
}

std::string Join(const std::vector<std::string>& v, const std::string& sep) {
  std::string r;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i)
      r += sep;
    r += v[i];
  }
  return r;
}

// Epoch 标记集：O(1) 去重，零分配。
struct EpochSet {
  std::vector<uint32_t> mark;
  uint32_t gen = 0;
  void Reset(size_t n) {
    if (mark.size() < n)
      mark.assign(n, 0);
    if (++gen == 0) {
      std::fill(mark.begin(), mark.end(), 0);
      gen = 1;
    }
  }
  bool Has(uint32_t x) const { return mark[x] == gen; }
  void Add(uint32_t x) { mark[x] = gen; }
};

// ---------------------------------------------------------------------------
// 双拼解码（flypy / zrm）
// ---------------------------------------------------------------------------

const char* SpInitMap(char c) {
  switch (c) {
    case 'v':
      return "zh";
    case 'i':
      return "ch";
    case 'u':
      return "sh";
    default:
      return nullptr;
  }
}

const char* SpInitialsMap(char c) {
  switch (c) {
    case 'v':
      return "zh";
    case 'i':
      return "ch";
    case 'u':
      return "sh";
    case 'a':
      return "a";
    case 'o':
      return "o";
    case 'e':
      return "e";
    default:
      return nullptr;
  }
}

const char* FlypyFinalMap(char c) {
  switch (c) {
    case 'q':
      return "iu";
    case 'w':
      return "ei";
    case 'e':
      return "e";
    case 'r':
      return "uan";
    case 'y':
      return "un";
    case 'u':
      return "u";
    case 'i':
      return "i";
    case 'p':
      return "ie";
    case 'a':
      return "a";
    case 'd':
      return "ai";
    case 'f':
      return "en";
    case 'g':
      return "eng";
    case 'h':
      return "ang";
    case 'j':
      return "an";
    case 'z':
      return "ou";
    case 'c':
      return "ao";
    case 'b':
      return "in";
    case 'n':
      return "iao";
    case 'm':
      return "ian";
    default:
      return nullptr;
  }
}

const char* ZrmFinalMap(char c) {
  switch (c) {
    case 'q':
      return "iu";
    case 'w':
      return "ia";
    case 'r':
      return "uan";
    case 'p':
      return "un";
    case 'u':
      return "u";
    case 'i':
      return "i";
    case 'f':
      return "en";
    case 'g':
      return "eng";
    case 'h':
      return "ang";
    case 'j':
      return "an";
    case 'k':
      return "ao";
    case 'l':
      return "ai";
    case 'z':
      return "ou";
    case 'c':
      return "ao";
    case 'b':
      return "in";
    case 'm':
      return "ian";
    default:
      return nullptr;
  }
}

bool IsIn(const char* init, std::initializer_list<const char*> list) {
  for (const char* s : list) {
    if (std::strcmp(init, s) == 0)
      return true;
  }
  return false;
}

std::string DecodeFlypySyllable(const std::string& code) {
  if (code.size() != 2)
    return "";
  char c1 = code[0];
  char c2 = code[1];
  if (c1 == 'a') {
    if (c2 == 'a')
      return "a";
    if (c2 == 'i' || c2 == 'n' || c2 == 'o')
      return std::string("a") + c2;
    if (c2 == 'h')
      return "ang";
  } else if (c1 == 'o') {
    if (c2 == 'o')
      return "o";
    if (c2 == 'u')
      return "ou";
  } else if (c1 == 'e') {
    if (c2 == 'e')
      return "e";
    if (c2 == 'i' || c2 == 'n' || c2 == 'r')
      return std::string("e") + c2;
    if (c2 == 'g')
      return "eng";
  }
  std::string init;
  if (const char* m = SpInitMap(c1))
    init = m;
  else
    init = std::string(1, c1);
  const char* final = nullptr;
  if (c2 == 'k') {
    final =
        IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h"}) ? "uai" : "ing";
  } else if (c2 == 'l') {
    final = IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h", "w"}) ? "uang"
                                                                       : "iang";
  } else if (c2 == 'x') {
    final = IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h"}) ? "ua" : "ia";
  } else if (c2 == 's') {
    final = IsIn(init.c_str(), {"j", "q", "x"}) ? "iong" : "ong";
  } else if (c2 == 't') {
    final = "ue";
  } else if (c2 == 'v') {
    if (IsIn(init.c_str(), {"n", "l"}))
      return init + "v";
    if (IsIn(init.c_str(), {"j", "q", "x", "y"}))
      return init + "u";
    final = "ui";
  } else if (c2 == 'o') {
    final = IsIn(init.c_str(), {"b", "p", "m", "f"}) ? "o" : "uo";
  } else {
    final = FlypyFinalMap(c2);
  }
  if (!final)
    return "";
  if (IsIn(init.c_str(), {"j", "q", "x", "y"}) && std::string(final) == "ue")
    return init + "ue";
  if (IsIn(init.c_str(), {"l", "n"}) && std::string(final) == "ue")
    return init + "ve";
  return init + final;
}

std::string DecodeZrmSyllable(const std::string& code) {
  if (code.size() != 2)
    return "";
  char c1 = code[0];
  char c2 = code[1];
  if (c1 == 'a') {
    if (c2 == 'a')
      return "a";
    if (c2 == 'i' || c2 == 'n' || c2 == 'o')
      return std::string("a") + c2;
    if (c2 == 'h')
      return "ang";
  } else if (c1 == 'o') {
    if (c2 == 'o')
      return "o";
    if (c2 == 'u')
      return "ou";
  } else if (c1 == 'e') {
    if (c2 == 'e')
      return "e";
    if (c2 == 'i' || c2 == 'n' || c2 == 'r')
      return std::string("e") + c2;
    if (c2 == 'g')
      return "eng";
  }
  std::string init;
  if (const char* m = SpInitMap(c1))
    init = m;
  else
    init = std::string(1, c1);
  const char* final = nullptr;
  if (c2 == 'y') {
    final =
        IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h"}) ? "uai" : "ing";
  } else if (c2 == 'd') {
    final =
        IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h"}) ? "uang" : "iang";
  } else if (c2 == 'w') {
    final = IsIn(init.c_str(), {"zh", "ch", "sh", "g", "k", "h"}) ? "ua" : "ia";
  } else if (c2 == 's') {
    final = IsIn(init.c_str(), {"j", "q", "x"}) ? "iong" : "ong";
  } else if (c2 == 't') {
    final = "ue";
  } else if (c2 == 'v') {
    if (IsIn(init.c_str(), {"n", "l"}))
      return init + "v";
    if (IsIn(init.c_str(), {"j", "q", "x", "y"}))
      return init + "u";
    final = "ui";
  } else if (c2 == 'o') {
    final = IsIn(init.c_str(), {"b", "p", "m", "f"}) ? "o" : "uo";
  } else {
    final = ZrmFinalMap(c2);
  }
  if (!final)
    return "";
  return init + final;
}

bool DecodeShuangpinString(IdsStore& store,
                           const std::string& s,
                           const std::string& scheme,
                           std::vector<std::string>* out) {
  auto decoder = (scheme == "zrm") ? &DecodeZrmSyllable : &DecodeFlypySyllable;
  std::vector<std::string> parts;
  if (HasSeparator(s))
    parts = SplitSeparator(s);
  else
    parts.push_back(s);
  std::vector<std::string> res;
  for (const auto& part : parts) {
    size_t n_part = part.size();
    size_t pairs = n_part / 2;
    for (size_t i = 0; i < pairs; ++i) {
      std::string pair = part.substr(i * 2, 2);
      std::string syl = decoder(pair);
      if (!syl.empty() && store.valid_pinyins().count(syl))
        res.push_back(syl);
      else
        return false;
    }
    if (n_part % 2 == 1) {
      char rem = part[n_part - 1];
      if (const char* m = SpInitialsMap(rem))
        res.push_back(m);
      else
        res.push_back(std::string(1, rem));
    }
  }
  out->swap(res);
  return true;
}

// ---------------------------------------------------------------------------
// 全拼最优切分
// ---------------------------------------------------------------------------

const hash_set<std::string>& ZeroInitials() {
  static const hash_set<std::string> k = {"a",   "o",  "e",  "ai", "ao",  "an",
                                          "ang", "ou", "er", "en", "eng", "ei"};
  return k;
}
const hash_set<std::string>& Initials2() {
  static const hash_set<std::string> k = {"zh", "ch", "sh"};
  return k;
}
const hash_set<std::string>& Initials1() {
  static const hash_set<std::string> k = {"b", "p", "m", "f", "d", "t", "n",
                                          "l", "g", "k", "h", "j", "q", "x",
                                          "r", "z", "c", "s", "y", "w"};
  return k;
}

std::vector<std::string> SegmentPinyinOptimal(IdsStore& store,
                                              const std::string& s) {
  size_t n = s.size();
  std::vector<std::string> result;
  if (n == 0)
    return result;
  struct Memo {
    bool set = false;
    int cost = 0;
    std::vector<std::string> segs;
  };
  std::vector<Memo> memo(n + 1);
  std::function<void(size_t)> dfs = [&](size_t idx) {
    if (idx >= n) {
      memo[idx].set = true;
      memo[idx].cost = 0;
      memo[idx].segs.clear();
      return;
    }
    if (memo[idx].set)
      return;
    int best_cost = 1000000000;
    std::vector<std::string> best_segs;
    size_t max_len = std::min<size_t>(6, n - idx);
    bool found = false;
    for (size_t l = max_len; l >= 1; --l) {
      std::string sub = s.substr(idx, l);
      bool is_valid = (store.valid_pinyins().count(sub) > 0) ||
                      (store.py_map().count(sub) > 0);
      bool is_single_init = (l == 1 && Initials1().count(sub)) ||
                            (l == 2 && Initials2().count(sub));
      if (is_valid || is_single_init) {
        int step_cost = 0;
        if (is_valid) {
          step_cost = 10 - static_cast<int>(l);
          if (ZeroInitials().count(sub) && idx > 0) {
            char prev = s[idx - 1];
            if (prev == 'n' || prev == 'g' || prev == 'r')
              step_cost += 15;
          }
        } else {
          step_cost = 30 + (idx < 2 ? 0 : static_cast<int>(idx * 2));
        }
        dfs(idx + l);
        int total = step_cost + memo[idx + l].cost;
        if (total < best_cost) {
          best_cost = total;
          found = true;
          best_segs.clear();
          best_segs.push_back(sub);
          for (const auto& seg : memo[idx + l].segs)
            best_segs.push_back(seg);
        }
      }
    }
    if (!found) {
      std::string sub = s.substr(idx, 1);
      dfs(idx + 1);
      best_cost = 100 + memo[idx + 1].cost;
      best_segs.clear();
      best_segs.push_back(sub);
      for (const auto& seg : memo[idx + 1].segs)
        best_segs.push_back(seg);
    }
    memo[idx].set = true;
    memo[idx].cost = best_cost;
    memo[idx].segs.swap(best_segs);
  };
  dfs(0);
  return memo[0].segs;
}

// ---------------------------------------------------------------------------
// 拼音切分缓存与 matcher 缓存
// ---------------------------------------------------------------------------

LruCache<std::string, std::vector<std::string>> g_seg_cache(2048);
LruCache<std::string, std::vector<uint16_t>> g_prefix_cache(128);
LruCache<std::string, std::vector<uint16_t>> g_matcher_cache(128);
LruCache<std::string, std::map<uint32_t, int>> g_rank_cache(64);

std::vector<std::string> SegmentAlphaString(IdsStore& store,
                                            const std::string& chunk,
                                            const std::string& mode) {
  if (chunk.empty())
    return {};
  std::string key = chunk + ":" + mode;
  if (const std::vector<std::string>* hit = g_seg_cache.Find(key))
    return *hit;
  const auto& py_map = store.py_map();
  std::vector<std::string> items;
  if (py_map.count(chunk) && (mode == "quanpin" || chunk.size() == 2)) {
    items.push_back(chunk);
    g_seg_cache.Put(key, items);
    return items;
  }
  if (mode == "flypy" || mode == "zrm") {
    std::vector<std::string> sp;
    if (DecodeShuangpinString(store, chunk, mode, &sp) && !sp.empty()) {
      g_seg_cache.Put(key, sp);
      return sp;
    }
    std::vector<std::string> segs = SegmentPinyinOptimal(store, chunk);
    if (!segs.empty()) {
      g_seg_cache.Put(key, segs);
      return segs;
    }
    items.push_back(chunk);
  } else {
    std::vector<std::string> segs = SegmentPinyinOptimal(store, chunk);
    if (!segs.empty()) {
      bool has_single_frag = false;
      for (const auto& seg : segs) {
        if (seg.size() == 1 && !store.valid_pinyins().count(seg)) {
          has_single_frag = true;
          break;
        }
      }
      if ((has_single_frag || chunk.size() <= 4) && chunk.size() >= 2 &&
          chunk.size() % 2 == 0) {
        std::vector<std::string> sp;
        if (DecodeShuangpinString(store, chunk, "flypy", &sp)) {
          bool all_valid = !sp.empty();
          for (const auto& seg : sp) {
            if (!store.valid_pinyins().count(seg)) {
              all_valid = false;
              break;
            }
          }
          if (all_valid) {
            g_seg_cache.Put(key, sp);
            return sp;
          }
        }
        if (DecodeShuangpinString(store, chunk, "zrm", &sp)) {
          bool all_valid = !sp.empty();
          for (const auto& seg : sp) {
            if (!store.valid_pinyins().count(seg)) {
              all_valid = false;
              break;
            }
          }
          if (all_valid) {
            g_seg_cache.Put(key, sp);
            return sp;
          }
        }
      }
      g_seg_cache.Put(key, segs);
      return segs;
    }
    items.push_back(chunk);
  }
  g_seg_cache.Put(key, items);
  return items;
}

const std::vector<uint16_t>* GetCompsByPrefix(IdsStore& store,
                                              const std::string& pref) {
  if (pref.empty())
    return nullptr;
  std::vector<uint16_t>* cached = g_prefix_cache.Find(pref);
  if (cached)
    return cached->empty() ? nullptr : cached;
  std::vector<uint16_t> res;
  hash_set<uint16_t> seen;
  size_t pref_len = pref.size();
  for (const auto& kv : store.py_map()) {
    if (kv.first.size() >= pref_len &&
        kv.first.compare(0, pref_len, pref) == 0) {
      for (uint16_t t : kv.second) {
        if (seen.insert(t).second)
          res.push_back(t);
      }
    }
  }
  if (res.size() > 1) {
    std::sort(res.begin(), res.end(), [&](uint16_t a, uint16_t b) {
      auto freq = [&](uint16_t t) -> uint32_t {
        uint32_t cp = store.TokenCp(t);
        uint32_t id = cp ? store.IdOfCp(cp) : kInvalidId;
        return id == kInvalidId ? 0 : store.Hot(id).freq;
      };
      return freq(a) > freq(b);
    });
  }
  auto& slot = g_prefix_cache.Ensure(pref);
  slot = res;
  return slot.empty() ? nullptr : &slot;
}

const std::vector<uint16_t>* GetTermMatcher(IdsStore& store,
                                            const std::string& term) {
  std::vector<uint16_t>* cached = g_matcher_cache.Find(term);
  if (cached)
    return cached;
  std::vector<uint16_t> v;
  auto it = store.py_map().find(term);
  if (it != store.py_map().end() && !it->second.empty()) {
    v = it->second;
  } else {
    const std::vector<uint16_t>* pref = GetCompsByPrefix(store, term);
    if (pref && !pref->empty()) {
      v = *pref;
    } else {
      // 原文与 Canon 归一两者都要纳入，才能命中 alts 中已归一的部件 token。
      uint16_t t;
      if (store.FindToken(term, &t))
        v.push_back(t);
      std::string c = Canon(term);
      if (c != term && store.FindToken(c, &t))
        v.push_back(t);
    }
  }
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  auto& slot = g_matcher_cache.Ensure(term);
  slot = std::move(v);
  return &slot;
}

const std::map<uint32_t, int>* GetDirectCompRank(IdsStore& store,
                                                 const std::string& term) {
  std::map<uint32_t, int>* cached = g_rank_cache.Find(term);
  if (cached)
    return cached;
  std::map<uint32_t, int> rank;
  int r = 1;
  auto it = store.py_map().find(term);
  if (it != store.py_map().end()) {
    for (uint16_t t : it->second) {
      uint32_t cp = store.TokenCp(t);
      if (cp > 0 && !rank.count(cp))
        rank[cp] = r++;
    }
  } else {
    uint32_t cp = 0;
    uint16_t t = store.ResolveTerm(term);
    if (t != kNoToken)
      cp = store.TokenCp(t);
    if (cp == 0)
      cp = CpOf(term);
    if (cp > 0 && !rank.count(cp))
      rank[cp] = r++;
  }
  auto& slot = g_rank_cache.Ensure(term);
  slot = std::move(rank);
  return &slot;
}

// ---------------------------------------------------------------------------
// 查询解析
// ---------------------------------------------------------------------------

struct ParsedQuery {
  std::vector<std::string> items;
  std::vector<std::string> order;
  hash_map<std::string, int> counts;
};

ParsedQuery ParseQuery(IdsStore& store,
                       const std::string& raw_query,
                       const std::string& mode) {
  std::string lower = AsciiLower(raw_query);
  ParsedQuery pq;
  std::vector<std::string> items;
  if (HasSeparator(lower)) {
    for (const auto& part : SplitSeparator(lower)) {
      if (part.empty())
        continue;
      std::vector<std::string> segs = SegmentAlphaString(store, part, mode);
      for (const auto& s : segs)
        items.push_back(s);
    }
  } else if (IsAsciiAlphaOnly(lower)) {
    items = SegmentAlphaString(store, lower, mode);
  } else {
    std::string cur;
    for (const auto& ch : Utf8Chars(lower)) {
      if (ch.size() == 1 && ch[0] >= 'a' && ch[0] <= 'z') {
        cur += ch;
      } else {
        if (!cur.empty()) {
          std::vector<std::string> segs = SegmentAlphaString(store, cur, mode);
          for (const auto& s : segs)
            items.push_back(s);
          cur.clear();
        }
        items.push_back(ch);
      }
    }
    if (!cur.empty()) {
      std::vector<std::string> segs = SegmentAlphaString(store, cur, mode);
      for (const auto& s : segs)
        items.push_back(s);
    }
  }
  for (const auto& item : items) {
    auto it = pq.counts.find(item);
    if (it == pq.counts.end()) {
      pq.counts[item] = 1;
      pq.order.push_back(item);
    } else {
      it->second += 1;
    }
  }
  pq.items = std::move(items);
  return pq;
}

// ---------------------------------------------------------------------------
// 匹配 / 打分
// ---------------------------------------------------------------------------

inline bool MatcherHas(const std::vector<uint16_t>& m, uint16_t t) {
  return std::binary_search(m.begin(), m.end(), t);
}

uint32_t FnvTokens(const uint16_t* t, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; ++i) {
    uint16_t v = t[i];
    h ^= (v & 0xFF);
    h *= 16777619u;
    h ^= ((v >> 8) & 0xFF);
    h *= 16777619u;
  }
  return h;
}

bool BipartiteMatch(const uint16_t* comps,
                    uint32_t total,
                    const std::vector<const std::vector<uint16_t>*>& matchers,
                    bool* used,
                    uint32_t idx) {
  if (idx == total)
    return true;
  for (uint32_t j = 0; j < total; ++j) {
    if (!used[j] && MatcherHas(*matchers[j], comps[idx])) {
      used[j] = true;
      if (BipartiteMatch(comps, total, matchers, used, idx + 1))
        return true;
      used[j] = false;
    }
  }
  return false;
}

// alts 记录布局：u8 n_groups; { u8 len; u16 n_variants; { u16 tokens[len]; u32
// sig } }
int CheckDirectMatch(const uint8_t* alts,
                     uint32_t total,
                     const std::vector<const std::vector<uint16_t>*>& matchers,
                     bool all_singleton,
                     uint32_t target_sig) {
  if (total == 0 || total > 64)
    return 0;
  uint8_t ng = *alts++;
  int best = 0;
  for (uint8_t g = 0; g < ng; ++g) {
    uint8_t L = *alts++;
    uint16_t nv = Rd16(alts);
    alts += 2;
    if (L != total) {
      alts += static_cast<size_t>(nv) * (static_cast<size_t>(L) * 2 + 4);
      continue;
    }
    for (uint16_t v = 0; v < nv; ++v) {
      const uint8_t* tok = alts;
      bool ordered = false;
      if (!all_singleton ||
          Rd32(tok + static_cast<size_t>(L) * 2) == target_sig) {
        ordered = true;
        for (uint32_t i = 0; i < total; ++i) {
          if (!MatcherHas(*matchers[i], Rd16(tok + i * 2))) {
            ordered = false;
            break;
          }
        }
      }
      if (ordered)
        return 2;
      if (best < 1) {
        if (total == 2) {
          uint16_t c0 = Rd16(tok), c1 = Rd16(tok + 2);
          const auto& m0 = *matchers[0];
          const auto& m1 = *matchers[1];
          if ((MatcherHas(m0, c0) && MatcherHas(m1, c1)) ||
              (MatcherHas(m0, c1) && MatcherHas(m1, c0)))
            best = 1;
        } else if (total == 3) {
          uint16_t c0 = Rd16(tok), c1 = Rd16(tok + 2), c2 = Rd16(tok + 4);
          const auto& m0 = *matchers[0];
          const auto& m1 = *matchers[1];
          const auto& m2 = *matchers[2];
          if ((MatcherHas(m0, c0) &&
               ((MatcherHas(m1, c1) && MatcherHas(m2, c2)) ||
                (MatcherHas(m1, c2) && MatcherHas(m2, c1)))) ||
              (MatcherHas(m0, c1) &&
               ((MatcherHas(m1, c0) && MatcherHas(m2, c2)) ||
                (MatcherHas(m1, c2) && MatcherHas(m2, c0)))) ||
              (MatcherHas(m0, c2) &&
               ((MatcherHas(m1, c0) && MatcherHas(m2, c1)) ||
                (MatcherHas(m1, c1) && MatcherHas(m2, c0)))))
            best = 1;
        } else {
          uint16_t comps[64];
          for (uint32_t i = 0; i < total; ++i)
            comps[i] = Rd16(tok + i * 2);
          bool used[64] = {false};
          if (BipartiteMatch(comps, total, matchers, used, 0))
            best = 1;
        }
      }
      alts += static_cast<size_t>(L) * 2 + 4;
    }
  }
  return best;
}

int64_t ScoreCandidate(
    IdsStore& store,
    uint32_t id,
    const std::vector<const std::vector<uint16_t>*>& matchers,
    size_t order_count) {
  const HotRec& h = store.Hot(id);
  int freq = static_cast<int>(h.freq);
  int depth = static_cast<int>(h.depth);
  if (order_count == 1 && !matchers.empty()) {
    uint16_t ct = h.char_token;
    if (ct != kNoToken && MatcherHas(*matchers[0], ct))
      return 30000000 + freq;
  }
  size_t total = matchers.size();
  int mt = 0;
  if (total >= 1 && total <= 31 && (h.flags & (1u << total))) {
    bool all_singleton = true;
    for (auto* m : matchers) {
      if (m->size() != 1) {
        all_singleton = false;
        break;
      }
    }
    uint32_t target_sig = 2166136261u;
    if (all_singleton) {
      for (auto* m : matchers) {
        uint16_t v = m->front();
        target_sig ^= (v & 0xFF);
        target_sig *= 16777619u;
        target_sig ^= ((v >> 8) & 0xFF);
        target_sig *= 16777619u;
      }
    }
    mt = CheckDirectMatch(store.Alts(id), static_cast<uint32_t>(total),
                          matchers, all_singleton, target_sig);
  }
  int64_t common_bonus = (freq >= 1900000) ? 5000000 : 0;
  if (mt == 2)
    return 20000000 + common_bonus + freq;
  if (mt == 1)
    return 15000000 + common_bonus + freq;
  return static_cast<int64_t>(freq) - static_cast<int64_t>(depth) * 1000;
}

// ---------------------------------------------------------------------------
// 候选提取
// ---------------------------------------------------------------------------

bool HasKey(IdsStore& store, const std::string& key) {
  return store.FindKey(key) != nullptr;
}

void AddPostingCps(IdsStore& store,
                   const std::string& key,
                   uint32_t limit,
                   uint32_t quota,
                   std::vector<uint32_t>* cp_list,
                   EpochSet* seen,
                   const hash_set<int>* disabled_blocks) {
  const KeyEntry* e = store.FindKey(key);
  if (!e || e->full_cnt == 0)
    return;
  uint32_t read = std::min<uint32_t>(e->head_cnt, quota);
  const uint32_t* head = store.HeadIds(*e);
  uint32_t added = 0;
  for (uint32_t i = 0; i < read; ++i) {
    uint32_t id = head[i];
    if (seen->Has(id))
      continue;
    seen->Add(id);
    bool keep = true;
    if (disabled_blocks &&
        disabled_blocks->count(static_cast<int>(store.Hot(id).block)))
      keep = false;
    if (keep) {
      cp_list->push_back(id);
      ++added;
      if (cp_list->size() >= limit || added >= quota)
        return;
    }
  }
  if (cp_list->size() >= limit || read < e->head_cnt)
    return;
  store.ForEachFull(*e, [&](uint32_t id) -> bool {
    if (seen->Has(id))
      return true;
    seen->Add(id);
    bool keep = true;
    if (disabled_blocks &&
        disabled_blocks->count(static_cast<int>(store.Hot(id).block)))
      keep = false;
    if (keep) {
      cp_list->push_back(id);
      ++added;
      if (cp_list->size() >= limit)
        return false;
    }
    return true;
  });
}

bool AddSingleCp(IdsStore& store,
                 std::vector<uint32_t>* cp_list,
                 EpochSet* seen,
                 uint32_t limit,
                 const std::string& ch,
                 const hash_set<int>* disabled_blocks) {
  uint32_t cp = CpOf(ch);
  uint32_t id = cp ? store.IdOfCp(cp) : kInvalidId;
  if (id == kInvalidId || seen->Has(id))
    return false;
  seen->Add(id);
  if (disabled_blocks &&
      disabled_blocks->count(static_cast<int>(store.Hot(id).block)))
    return false;
  cp_list->push_back(id);
  return cp_list->size() >= limit;
}

EpochSet& SeenSet() {
  static EpochSet s;
  return s;
}

std::vector<uint32_t> GetCandidatesForTerm(
    IdsStore& store,
    const std::string& term,
    int count,
    uint32_t max_chars,
    const std::vector<std::string>* custom_keys,
    const hash_set<int>* disabled_blocks,
    bool is_multi_term) {
  std::vector<uint32_t> cp_list;
  EpochSet& seen = SeenSet();
  seen.Reset(store.num_chars());
  uint32_t limit = max_chars == 0 ? 200000 : max_chars;

  std::string cn_term = Canon(term);

  if (count == 1 && !is_multi_term) {
    if ((HasKey(store, cn_term) || HasKey(store, term)) &&
        !(term.size() >= 1 && term[0] == '{')) {
      if (AddSingleCp(store, &cp_list, &seen, limit, cn_term, disabled_blocks))
        return cp_list;
      if (cn_term != term) {
        if (AddSingleCp(store, &cp_list, &seen, limit, term, disabled_blocks))
          return cp_list;
      }
    }
    auto pit = store.py_map().find(term);
    if (pit != store.py_map().end()) {
      for (uint16_t t : pit->second) {
        const std::string& comp = store.TokenStr(t);
        if (!(comp.size() >= 1 && comp[0] == '{')) {
          if (AddSingleCp(store, &cp_list, &seen, limit, comp, disabled_blocks))
            return cp_list;
        }
      }
    }
  }

  if (custom_keys && !custom_keys->empty()) {
    uint32_t quota = (custom_keys->size() <= 2)
                         ? limit
                         : std::max<uint32_t>(60, limit / custom_keys->size());
    for (const auto& k : *custom_keys) {
      AddPostingCps(store, k, limit, quota, &cp_list, &seen, disabled_blocks);
      if (static_cast<uint32_t>(cp_list.size()) >= limit)
        break;
    }
    if (cp_list.size() < limit) {
      for (const auto& k : *custom_keys) {
        AddPostingCps(store, k, limit, limit, &cp_list, &seen, disabled_blocks);
        if (static_cast<uint32_t>(cp_list.size()) >= limit)
          break;
      }
    }
  } else {
    bool has_cn = HasKey(store, cn_term);
    bool has_term = HasKey(store, term);
    bool has_cn_n =
        count > 1 && HasKey(store, cn_term + "*" + std::to_string(count));
    bool has_term_n =
        count > 1 && HasKey(store, term + "*" + std::to_string(count));
    if (has_cn || has_term || has_cn_n || has_term_n) {
      std::string key;
      if (count > 1) {
        if (has_cn_n)
          key = cn_term + "*" + std::to_string(count);
        else if (has_term_n)
          key = term + "*" + std::to_string(count);
      }
      if (key.empty()) {
        if (has_cn)
          key = cn_term;
        else
          key = term;
      }
      AddPostingCps(store, key, limit, limit, &cp_list, &seen, disabled_blocks);
    } else {
      auto pit = store.py_map().find(term);
      if (pit != store.py_map().end()) {
        const auto& comps = pit->second;
        uint32_t quota = (comps.size() <= 2)
                             ? limit
                             : std::max<uint32_t>(60, limit / comps.size());
        auto emit = [&](uint32_t q) {
          for (uint16_t t : comps) {
            std::string key =
                (count > 1) ? store.TokenStr(t) + "*" + std::to_string(count)
                            : store.TokenStr(t);
            if (HasKey(store, key))
              AddPostingCps(store, key, limit, q, &cp_list, &seen,
                            disabled_blocks);
            else if (count > 1 && HasKey(store, store.TokenStr(t)))
              AddPostingCps(store, store.TokenStr(t), limit, q, &cp_list, &seen,
                            disabled_blocks);
            if (static_cast<uint32_t>(cp_list.size()) >= limit)
              return;
          }
        };
        emit(quota);
        if (cp_list.size() < limit)
          emit(limit);
      } else {
        const std::vector<uint16_t>* pref = GetCompsByPrefix(store, term);
        if (pref && !pref->empty()) {
          uint32_t quota =
              std::max<uint32_t>(60, limit / std::max<size_t>(1, pref->size()));
          for (uint16_t t : *pref) {
            std::string key =
                (count > 1) ? store.TokenStr(t) + "*" + std::to_string(count)
                            : store.TokenStr(t);
            if (HasKey(store, key))
              AddPostingCps(store, key, limit, quota, &cp_list, &seen,
                            disabled_blocks);
            else if (count > 1 && HasKey(store, store.TokenStr(t)))
              AddPostingCps(store, store.TokenStr(t), limit, quota, &cp_list,
                            &seen, disabled_blocks);
            if (static_cast<uint32_t>(cp_list.size()) >= limit)
              break;
          }
        }
      }
    }
  }
  return cp_list;
}

// ---------------------------------------------------------------------------
// 多项查询 term 信息
// ---------------------------------------------------------------------------

struct TermInfo {
  std::string term;
  int mult = 1;
  bool is_pref = false;
  std::vector<std::string> comps;
  std::vector<uint16_t> tokens;
  std::vector<std::string> post_keys;
  size_t data_size = 0;
};

LruCache<std::string, std::shared_ptr<TermInfo>> g_term_cache(200);

std::shared_ptr<TermInfo> BuildTermInfo(IdsStore& store,
                                        const std::string& term,
                                        int mult) {
  auto info = std::make_shared<TermInfo>();
  info->term = term;
  info->mult = mult;
  std::string cn_term = Canon(term);
  bool is_pref = false;
  std::vector<std::string> comps;
  auto pit = store.py_map().find(term);
  if (pit != store.py_map().end()) {
    for (uint16_t t : pit->second)
      comps.push_back(store.TokenStr(t));
  } else if (HasKey(store, cn_term) || HasKey(store, term)) {
    comps.push_back(cn_term);
    if (cn_term != term)
      comps.push_back(term);
  } else if (const std::vector<uint16_t>* pref =
                 GetCompsByPrefix(store, term)) {
    for (uint16_t t : *pref)
      comps.push_back(store.TokenStr(t));
    is_pref = true;
  } else {
    comps.push_back(cn_term);
    if (cn_term != term)
      comps.push_back(term);
  }
  info->is_pref = is_pref;
  info->comps = comps;
  for (const auto& c : comps) {
    uint16_t t = store.ResolveTerm(c);
    if (t != kNoToken)
      info->tokens.push_back(t);
  }
  std::sort(info->tokens.begin(), info->tokens.end());
  info->tokens.erase(std::unique(info->tokens.begin(), info->tokens.end()),
                     info->tokens.end());

  int max_keys = is_pref ? 25 : 250;
  hash_set<std::string> seen_post;
  for (const auto& c : comps) {
    std::string cn = Canon(c);
    std::string suffix = (mult > 1) ? "*" + std::to_string(mult) : "";
    std::string k1 = c + suffix;
    std::string k2 = cn + suffix;
    std::string use_k;
    if (HasKey(store, k2))
      use_k = k2;
    else if (HasKey(store, k1))
      use_k = k1;
    if (!use_k.empty() && seen_post.insert(use_k).second) {
      info->post_keys.push_back(use_k);
      if (static_cast<int>(info->post_keys.size()) >= max_keys)
        break;
    }
  }
  size_t sz = 0;
  for (const auto& k : info->post_keys) {
    if (const KeyEntry* e = store.FindKey(k))
      sz += static_cast<size_t>(e->full_cnt) * 4;
  }
  info->data_size = sz;
  return info;
}

// ---------------------------------------------------------------------------
// 主检索
// ---------------------------------------------------------------------------

LruCache<std::string, std::vector<uint32_t>> g_query_cache(256);

std::vector<uint32_t> SearchComponents(IdsStore& store,
                                       const std::string& query_str,
                                       int max_results,
                                       const std::string& mode,
                                       const hash_set<int>* disabled_blocks) {
  if (query_str.empty())
    return {};
  std::string cache_key =
      query_str + ":" + std::to_string(max_results) + ":" + mode;
  if (disabled_blocks && !disabled_blocks->empty()) {
    std::vector<int> ks(disabled_blocks->begin(), disabled_blocks->end());
    std::sort(ks.begin(), ks.end());
    std::string blk;
    for (int b : ks) {
      if (!blk.empty())
        blk += ",";
      blk += std::to_string(b);
    }
    cache_key += ":blk=" + blk;
  }
  if (const std::vector<uint32_t>* hit = g_query_cache.Find(cache_key))
    return *hit;

  ParsedQuery pq = ParseQuery(store, query_str, mode);
  if (pq.order.empty())
    return {};

  std::vector<const std::vector<uint16_t>*> matchers;
  matchers.reserve(pq.items.size());
  for (const auto& item : pq.items)
    matchers.push_back(GetTermMatcher(store, item));

  size_t order_count = pq.order.size();

  // ----- 场景 A：单项 -----
  if (order_count == 1) {
    const std::string& term = pq.order[0];
    const std::map<uint32_t, int>* direct_rank = GetDirectCompRank(store, term);
    bool is_pref = (store.valid_pinyins().count(term) == 0) &&
                   (GetCompsByPrefix(store, term) != nullptr);
    uint32_t limit =
        is_pref ? std::max(250u, static_cast<uint32_t>(max_results) * 3)
                : std::max(480u, static_cast<uint32_t>(max_results) * 8);
    std::vector<uint32_t> cands =
        GetCandidatesForTerm(store, term, pq.counts.at(term), limit, nullptr,
                             disabled_blocks, /*is_multi_term=*/false);
    if (cands.empty())
      return {};

    std::vector<std::pair<uint32_t, int64_t>> scored;
    scored.reserve(cands.size());
    for (uint32_t id : cands) {
      int64_t sc = ScoreCandidate(store, id, matchers, order_count);
      if (direct_rank) {
        uint32_t cp = store.CpOfId(id);
        auto rit = direct_rank->find(cp);
        if (rit != direct_rank->end()) {
          int64_t f = static_cast<int64_t>(store.Hot(id).freq);
          sc = 40000000 + f - rit->second * 10;
        }
      }
      scored.push_back({id, sc});
    }
    std::sort(scored.begin(), scored.end(),
              [](const std::pair<uint32_t, int64_t>& a,
                 const std::pair<uint32_t, int64_t>& b) {
                return a.second > b.second;
              });
    std::vector<uint32_t> results;
    size_t lim =
        std::min<size_t>(scored.size(), static_cast<size_t>(max_results));
    for (size_t i = 0; i < lim; ++i)
      results.push_back(scored[i].first);
    auto& slot = g_query_cache.Ensure(cache_key);
    slot = results;
    return results;
  }

  // ----- 场景 B：多项 -----
  std::vector<std::shared_ptr<TermInfo>> terms_info;
  for (const auto& term : pq.order) {
    int mult = pq.counts.at(term);
    std::string k = term + ":" + std::to_string(mult);
    std::shared_ptr<TermInfo>& sp = g_term_cache.Ensure(k);
    if (!sp)
      sp = BuildTermInfo(store, term, mult);
    if (!sp->comps.empty())
      terms_info.push_back(sp);
  }
  if (terms_info.empty())
    return {};

  std::sort(terms_info.begin(), terms_info.end(),
            [](const std::shared_ptr<TermInfo>& a,
               const std::shared_ptr<TermInfo>& b) {
              return a->data_size < b->data_size;
            });

  TermInfo* base = terms_info[0].get();
  bool has_any_pref = false;
  for (const auto& t : terms_info) {
    if (t->is_pref) {
      has_any_pref = true;
      break;
    }
  }
  uint32_t base_limit = has_any_pref ? 1500 : 8000;
  std::vector<uint32_t> current =
      GetCandidatesForTerm(store, base->term, base->mult, base_limit,
                           &base->post_keys, disabled_blocks,
                           /*is_multi_term=*/true);
  if (current.empty())
    return {};

  for (size_t idx = 1; idx < terms_info.size(); ++idx) {
    if (current.empty())
      break;
    TermInfo* t = terms_info[idx].get();
    std::vector<uint32_t> filtered;
    filtered.reserve(current.size());
    for (uint32_t id : current) {
      const uint16_t* cs;
      uint16_t cn;
      store.CompSet(id, &cs, &cn);
      bool matched = false;
      size_t a = 0, b = 0;
      const auto& toks = t->tokens;
      while (a < cn && b < toks.size()) {
        if (cs[a] == toks[b]) {
          matched = true;
          break;
        } else if (cs[a] < toks[b]) {
          ++a;
        } else {
          ++b;
        }
      }
      if (!matched) {
        for (const auto& k : t->post_keys) {
          if (const KeyEntry* e = store.FindKey(k)) {
            if (store.FullContains(*e, id)) {
              matched = true;
              break;
            }
          }
        }
      }
      if (matched)
        filtered.push_back(id);
    }
    current.swap(filtered);
  }
  if (current.empty())
    return {};

  uint32_t score_limit = has_any_pref ? 500 : 1500;
  std::vector<std::pair<uint32_t, int64_t>> scored;
  size_t n_score =
      std::min<size_t>(current.size(), static_cast<size_t>(score_limit));
  scored.reserve(n_score);
  for (size_t i = 0; i < n_score; ++i)
    scored.push_back(
        {current[i], ScoreCandidate(store, current[i], matchers, order_count)});
  std::sort(scored.begin(), scored.end(),
            [](const std::pair<uint32_t, int64_t>& a,
               const std::pair<uint32_t, int64_t>& b) {
              return a.second > b.second;
            });
  std::vector<uint32_t> results;
  size_t lim =
      std::min<size_t>(scored.size(), static_cast<size_t>(max_results));
  for (size_t i = 0; i < lim; ++i)
    results.push_back(scored[i].first);
  auto& slot = g_query_cache.Ensure(cache_key);
  slot = results;
  return results;
}

const char* kBlockNames[17] = {
    "基本",     "扩A",      "扩B",      "扩C",  "扩D", "扩E",
    "扩F",      "扩G",      "扩H",      "扩I",  "扩J", "兼容区",
    "兼容补充", "部首补充", "康熙部首", "笔画", "CJK"};

std::string ToLower(const std::string& s) {
  return AsciiLower(s);
}

}  // namespace

// ---------------------------------------------------------------------------
// IdsSearchTranslator
// ---------------------------------------------------------------------------

IdsSearchTranslator::IdsSearchTranslator(const rime::Ticket& ticket)
    : rime::Translator(ticket) {
  // 组件以 `- ids_search_translator` 引用时 name_space 默认是 "translator"，
  // 自纠正到配置命名空间 ids_search。
  if (ticket.name_space == "translator" || name_space_.empty()) {
    name_space_ = "ids_search";
  }
  Initialize();
}

void IdsSearchTranslator::Initialize() {
  initialized_ = true;
  if (!engine_ || !engine_->schema())
    return;
  rime::Config* config = engine_->schema()->config();
  if (!config)
    return;
  std::string ns = name_space_ + "/";
  config->GetString(ns + "data_file", &data_path_);
  config->GetString(ns + "custom_file", &custom_path_);
  if (data_path_.empty())
    data_path_ = "ids_data.bin";

  config->GetString(ns + "tag", &tag_);
  if (tag_.empty())
    tag_ = "ids_search";
  config->GetString(ns + "prefix", &prefix_);
  config->GetString(ns + "shuangpin_scheme", &shuangpin_scheme_);
  if (shuangpin_scheme_ != "zrm" && shuangpin_scheme_ != "quanpin")
    shuangpin_scheme_ = "flypy";
  config->GetInt(ns + "max_results", &max_results_);
  if (max_results_ <= 0)
    max_results_ = 60;
  config->GetString(ns + "tips", &tips_);
  if (tips_.empty())
    tips_ = "〔部件检字〕";

  bool v;
  if (config->GetBool(ns + "show_pinyin", &v))
    show_pinyin_ = v;
  if (config->GetBool(ns + "show_ids", &v))
    show_ids_ = v;
  if (config->GetBool(ns + "show_unicode", &v))
    show_unicode_ = v;
  if (config->GetBool(ns + "show_block", &v))
    show_block_ = v;

  for (size_t i = 0; i < 64; ++i) {
    std::string s_name;
    if (config->GetString("switches/@" + std::to_string(i) + "/name",
                          &s_name)) {
      if (s_name == "show_pinyin")
        has_sw_pinyin_ = true;
      else if (s_name == "show_ids")
        has_sw_ids_ = true;
      else if (s_name == "show_unicode")
        has_sw_unicode_ = true;
      else if (s_name == "show_block")
        has_sw_block_ = true;
    } else {
      if (config->GetItem("switches/@" + std::to_string(i)) == nullptr)
        break;
    }
  }

  if (rime::an<rime::ConfigList> list =
          config->GetList(ns + "disabled_blocks")) {
    for (size_t i = 0; i < list->size(); ++i) {
      if (rime::an<rime::ConfigValue> value = list->GetValueAt(i)) {
        std::string block_name;
        if (value->GetString(&block_name)) {
          auto it = BlockAliasMap().find(ToLower(block_name));
          if (it != BlockAliasMap().end())
            disabled_blocks_.insert(it->second);
        }
      }
    }
  }
  {
    std::string str;
    if (config->GetString(ns + "disabled_blocks", &str) && !str.empty()) {
      size_t i = 0;
      while (i < str.size()) {
        while (i < str.size() &&
               (str[i] == ',' || str[i] == ' ' || str[i] == '\t' ||
                str[i] == '\r' || str[i] == '\n'))
          ++i;
        size_t j = i;
        while (j < str.size() && str[j] != ',' && str[j] != ' ' &&
               str[j] != '\t' && str[j] != '\r' && str[j] != '\n')
          ++j;
        if (j > i) {
          auto it = BlockAliasMap().find(ToLower(str.substr(i, j - i)));
          if (it != BlockAliasMap().end())
            disabled_blocks_.insert(it->second);
        }
        i = j;
      }
    }
  }
  disabled_blocks_enabled_ = !disabled_blocks_.empty();

  prefix_len_ = prefix_.size();
  prefix_byte_ = prefix_len_ ? prefix_[0] : 0;
}

std::string IdsSearchTranslator::PinyinMode() {
  std::string mode = shuangpin_scheme_;
  if (engine_ && engine_->context()) {
    rime::Context* ctx = engine_->context();
    if (ctx->get_option("shuangpin_flypy") ||
        ctx->get_option("double_pinyin")) {
      mode = "flypy";
    } else if (ctx->get_option("shuangpin_zrm")) {
      mode = "zrm";
    } else if (ctx->get_option("pinyin_quanpin")) {
      mode = "quanpin";
    }
  }
  return mode;
}

bool IdsSearchTranslator::EnsureLoaded() {
  IdsStore& store = IdsStore::Instance();
  if (!store.loaded()) {
    if (!initialized_)
      Initialize();
    std::string dp = data_path_.empty() ? "ids_data.bin" : data_path_;
    rime::Deployer& deployer = rime::Service::instance().deployer();
    auto resolve = [&](const std::string& filename) -> fs::path {
      fs::path p(filename);
      if (p.is_absolute())
        return p;
      fs::path ud = deployer.user_data_dir / filename;
      if (fs::exists(ud))
        return ud;
      fs::path sd = deployer.shared_data_dir / filename;
      if (fs::exists(sd))
        return sd;
      return ud;
    };
    fs::path data = resolve(dp);
    fs::path custom;
    if (!custom_path_.empty())
      custom = resolve(custom_path_);
    bool ok = store.Load(data, custom);
    if (!ok) {
      LOG(ERROR) << "ids_search_translator: cannot load data file: '" << data
                 << "'";
      return false;
    }
  }
  for (int b : store.custom_disabled_blocks()) {
    if (disabled_blocks_.insert(b).second)
      disabled_blocks_enabled_ = true;
  }
  return true;
}

std::vector<uint32_t> IdsSearchTranslator::Search(const std::string& query_raw,
                                                  const std::string& mode) {
  IdsStore& store = IdsStore::Instance();
  const hash_set<int>* disabled =
      disabled_blocks_enabled_ ? &disabled_blocks_ : nullptr;
  return SearchComponents(store, query_raw, max_results_, mode, disabled);
}

rime::an<rime::Translation> IdsSearchTranslator::BuildCandidates(
    const std::vector<uint32_t>& results,
    const std::string& query_raw,
    size_t start_pos,
    size_t end_pos) {
  IdsStore& store = IdsStore::Instance();
  bool sw_p = show_pinyin_;
  bool sw_i = show_ids_;
  bool sw_u = show_unicode_;
  bool sw_b = show_block_;
  if (engine_ && engine_->context()) {
    rime::Context* ctx = engine_->context();
    if (has_sw_pinyin_)
      sw_p = ctx->get_option("show_pinyin");
    if (has_sw_ids_)
      sw_i = ctx->get_option("show_ids");
    if (has_sw_unicode_)
      sw_u = ctx->get_option("show_unicode");
    if (has_sw_block_)
      sw_b = ctx->get_option("show_block");
  }
  auto translation = rime::New<rime::FifoTranslation>();
  for (uint32_t id : results) {
    const uint8_t* py;
    const uint8_t* ids;
    uint16_t py_len, ids_len;
    store.Cold(id, &py, &py_len, &ids, &ids_len);
    const HotRec& hot = store.Hot(id);
    std::vector<std::string> parts;
    if (sw_p && py_len > 0)
      parts.push_back(std::string(reinterpret_cast<const char*>(py), py_len));
    if (sw_i && ids_len > 0)
      parts.push_back(std::string(reinterpret_cast<const char*>(ids), ids_len));
    uint32_t cp = store.CpOfId(id);
    if (sw_u) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%04X", cp);
      parts.push_back(buf);
    }
    if (sw_b) {
      const char* bname = hot.block < 17 ? kBlockNames[hot.block] : "基本";
      parts.push_back(bname);
    }
    std::string comment = Join(parts, " ");
    std::string ch = CpToUtf8(cp);
    auto cand = rime::New<rime::SimpleCandidate>(
        "ids_search", start_pos, end_pos, ch, comment,
        prefix_len_ > 0 ? query_raw : "");
    cand->set_quality(100.0);
    translation->Append(cand);
  }
  return translation;
}

rime::an<rime::Translation> IdsSearchTranslator::Query(
    const std::string& input,
    const rime::Segment& segment) {
  if (!segment.HasTag(tag_))
    return nullptr;
  if (!initialized_)
    Initialize();

  std::string query_raw = input;
  size_t start_pos = segment.start;
  size_t end_pos = segment.end;

  if (prefix_len_ > 0) {
    if (input.empty() || static_cast<unsigned char>(input[0]) !=
                             static_cast<unsigned char>(prefix_byte_))
      return nullptr;
    if (prefix_len_ > 1 && input.compare(0, prefix_len_, prefix_) != 0)
      return nullptr;
    query_raw = input.substr(prefix_len_);
  }

  if (query_raw.empty()) {
    auto cand = rime::New<rime::SimpleCandidate>(
        "ids_search", start_pos, end_pos, "", tips_, input + " " + tips_);
    auto translation = rime::New<rime::FifoTranslation>();
    translation->Append(cand);
    return translation;
  }

  if (!EnsureLoaded())
    return nullptr;

  std::string mode = PinyinMode();
  std::vector<uint32_t> results = Search(query_raw, mode);
  if (results.empty())
    return nullptr;
  return BuildCandidates(results, query_raw, start_pos, end_pos);
}

}  // namespace ids_search
