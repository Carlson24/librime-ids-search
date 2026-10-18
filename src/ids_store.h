//
// ids_store.h — ids_search 插件数据层。
// 加载 IDS3 单文件（mmap 惰性映射），提供稠密 id / 热字段 / 整数部件匹配。
//
#ifndef RIME_IDS_SEARCH_IDS_STORE_H_
#define RIME_IDS_SEARCH_IDS_STORE_H_

#include <cstdint>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rime/common.h>

namespace ids_search {

namespace fs = std::filesystem;

// 偏旁等价归一表（与生成脚本的 C2 表完全一致）。
const rime::hash_map<std::string, std::string>& CanonMap();
std::string Canon(const std::string& c);

// 区块别名表。
const rime::hash_map<std::string, int>& BlockAliasMap();

constexpr uint16_t kNoToken = 0xFFFF;
constexpr uint32_t kInvalidId = 0xFFFFFFFF;

// IDS3 section type ids（必须与 generate_data_v2.py 一致）。
enum SectionType {
  kSecCp2Id = 1,
  kSecId2Cp = 2,
  kSecHot = 3,
  kSecAltsOff = 4,
  kSecAlts = 5,
  kSecCompSetOff = 6,
  kSecCompSet = 7,
  kSecColdOff = 8,
  kSecCold = 9,
  kSecTokens = 10,
  kSecPy = 11,
  kSecKeys = 12,
  kSecPostHead = 13,
  kSecPostSmall = 14,
  kSecPostBitset = 15,
};

// HOT 记录（12 字节）：freq u32, depth u8, block u8, char_token u16, flags
// u32。
struct HotRec {
  uint32_t freq = 0;
  uint8_t depth = 0;
  uint8_t block = 0;
  uint16_t char_token = kNoToken;
  uint32_t flags = 0;  // bit L 置位表示存在长度为 L 的 alts 变体
};

struct KeyEntry {
  uint32_t head_off = 0;  // POST_HEAD 的 u32 索引
  uint32_t head_cnt = 0;
  uint32_t full_off =
      0;  // kind 0: POST_SMALL u32 索引; kind 1: POST_BITSET u64 字索引
  uint32_t full_cnt = 0;
  uint8_t kind = 0;  // 0 = 升序 u32 数组; 1 = 位图
};

// 小端读取（未对齐安全）。
inline uint16_t Rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// 简单 LRU 缓存（供切分 / matcher / 查询结果缓存使用）。
template <class Key, class Val>
class LruCache {
 public:
  explicit LruCache(size_t cap) : cap_(cap == 0 ? 1 : cap) {}

  Val* Find(const Key& k) {
    auto it = map_.find(k);
    if (it == map_.end())
      return nullptr;
    order_.splice(order_.begin(), order_, it->second.order_it);
    return &it->second.val;
  }

  Val& Ensure(const Key& k) {
    auto it = map_.find(k);
    if (it != map_.end()) {
      order_.splice(order_.begin(), order_, it->second.order_it);
      return it->second.val;
    }
    if (map_.size() >= cap_) {
      Key oldest = order_.back();
      order_.pop_back();
      map_.erase(oldest);
    }
    auto res = map_.emplace(k, Entry{Val(), order_.end()});
    order_.push_front(k);
    res.first->second.order_it = order_.begin();
    return res.first->second.val;
  }

  void Put(const Key& k, Val v) {
    auto it = map_.find(k);
    if (it != map_.end()) {
      it->second.val = std::move(v);
      order_.splice(order_.begin(), order_, it->second.order_it);
      return;
    }
    if (map_.size() >= cap_) {
      Key oldest = order_.back();
      order_.pop_back();
      map_.erase(oldest);
    }
    auto res = map_.emplace(k, Entry{std::move(v), order_.end()});
    order_.push_front(k);
    res.first->second.order_it = order_.begin();
  }

  void Erase(const Key& k) {
    auto it = map_.find(k);
    if (it == map_.end())
      return;
    order_.erase(it->second.order_it);
    map_.erase(it);
  }

  void Clear() {
    map_.clear();
    order_.clear();
  }

 private:
  struct Entry {
    Val val;
    typename std::list<Key>::iterator order_it;
  };
  rime::hash_map<Key, Entry> map_;
  std::list<Key> order_;
  size_t cap_;
};

// IDS3 数据仓库：mmap 惰性映射 + 稠密 id 直索引。
class IdsStore {
 public:
  static IdsStore& Instance();

  bool Load(const fs::path& data_path, const fs::path& custom_path);
  bool loaded() const { return loaded_; }

  uint32_t num_chars() const { return num_chars_; }
  uint32_t max_cp() const { return max_cp_; }

  uint32_t IdOfCp(uint32_t cp) const {
    if (cp == 0 || cp >= max_cp_)
      return kInvalidId;
    uint32_t v = cp2id_[cp];
    return v == 0 ? kInvalidId : v - 1;
  }
  uint32_t CpOfId(uint32_t id) const {
    return id < num_chars_ ? id2cp_[id] : 0;
  }

  const HotRec& Hot(uint32_t id) const {
    static const HotRec kEmpty;
    return id < num_chars_ ? hot_[id] : kEmpty;
  }

  // ALTS / COMPSET / COLD 记录指针。
  const uint8_t* Alts(uint32_t id) const { return alts_ + alts_off_[id]; }
  const uint8_t* CompSetRec(uint32_t id) const {
    return compset_ + compset_off_[id];
  }
  // 直接取字典序升序的部件 id 列表。
  void CompSet(uint32_t id, const uint16_t** out, uint16_t* n) const {
    const uint8_t* p = CompSetRec(id);
    *n = Rd16(p);
    *out = reinterpret_cast<const uint16_t*>(p + 2);
  }

  // 供 translator 直接解码 COLD：返回 py/ids 长度与指针。
  void Cold(uint32_t id,
            const uint8_t** py,
            uint16_t* py_len,
            const uint8_t** ids,
            uint16_t* ids_len) const {
    const uint8_t* p = cold_ + cold_off_[id];
    *py_len = Rd16(p);
    *py = p + 2;
    p = *py + *py_len;
    *ids_len = Rd16(p);
    *ids = p + 2;
  }

  // tokens
  uint16_t NTokens() const { return num_tokens_; }
  const std::string& TokenStr(uint16_t id) const {
    static const std::string kEmpty;
    return id < num_tokens_ ? token_str_[id] : kEmpty;
  }
  uint32_t TokenCp(uint16_t id) const {
    return id < num_tokens_ ? token_cp_[id] : 0;
  }
  bool FindToken(const std::string& s, uint16_t* out) const {
    auto it = token_index_.find(s);
    if (it == token_index_.end())
      return false;
    *out = it->second;
    return true;
  }

  // 找一个 term 的 token id：先原文，再 Canon 归一。
  uint16_t ResolveTerm(const std::string& s) const {
    uint16_t t;
    if (FindToken(s, &t))
      return t;
    std::string c = Canon(s);
    if (c != s && FindToken(c, &t))
      return t;
    return kNoToken;
  }

  // keys / postings
  const rime::hash_map<std::string, KeyEntry>& keys() const { return keys_; }
  const KeyEntry* FindKey(const std::string& k) const {
    auto it = keys_.find(k);
    return it == keys_.end() ? nullptr : &it->second;
  }
  const uint32_t* HeadIds(const KeyEntry& e) const {
    return post_head_ + e.head_off;
  }
  bool FullContains(const KeyEntry& e, uint32_t id) const {
    if (e.kind == 0)
      return SmallContains(e, id);
    const uint64_t* w =
        post_bitset_ + e.full_off + (static_cast<size_t>(id) >> 6);
    return (*w >> (id & 63)) & 1ULL;
  }
  template <class F>
  void ForEachFull(const KeyEntry& e, F&& f) const {
    if (e.kind == 0) {
      const uint32_t* a = post_small_ + e.full_off;
      for (uint32_t i = 0; i < e.full_cnt; ++i)
        if (!f(a[i]))
          return;
    } else {
      const uint64_t* base = post_bitset_ + e.full_off;
      for (uint32_t id = 0; id < num_chars_; ++id) {
        if ((base[id >> 6] >> (id & 63)) & 1ULL)
          if (!f(id))
            return;
      }
    }
  }

  // pinyin
  const std::map<std::string, std::vector<uint16_t>>& py_map() const {
    return py_map_;
  }
  const rime::hash_set<std::string>& valid_pinyins() const {
    return valid_pinyins_;
  }

  const rime::hash_set<int>& custom_disabled_blocks() const {
    return custom_disabled_blocks_;
  }

 private:
  IdsStore() = default;
  ~IdsStore();
  IdsStore(const IdsStore&) = delete;
  IdsStore& operator=(const IdsStore&) = delete;

  bool Parse();
  void ApplyCustomRules(const fs::path& path);
  bool SmallContains(const KeyEntry& e, uint32_t id) const;

  struct MappedFile;
  std::shared_ptr<MappedFile> file_;  // mmap / 文件缓冲的所有权
  const uint8_t* base_ = nullptr;
  size_t size_ = 0;

  const uint32_t* cp2id_ = nullptr;
  const uint32_t* id2cp_ = nullptr;
  const HotRec* hot_ = nullptr;
  const uint32_t* alts_off_ = nullptr;
  const uint8_t* alts_ = nullptr;
  const uint32_t* compset_off_ = nullptr;
  const uint8_t* compset_ = nullptr;
  const uint32_t* cold_off_ = nullptr;
  const uint8_t* cold_ = nullptr;
  const uint32_t* post_head_ = nullptr;
  const uint32_t* post_small_ = nullptr;
  const uint64_t* post_bitset_ = nullptr;

  uint32_t num_chars_ = 0;
  uint32_t max_cp_ = 0;
  uint32_t num_tokens_ = 0;

  std::vector<std::string> token_str_;
  std::vector<uint32_t> token_cp_;
  rime::hash_map<std::string, uint16_t> token_index_;

  rime::hash_map<std::string, KeyEntry> keys_;
  std::map<std::string, std::vector<uint16_t>> py_map_;
  rime::hash_set<std::string> valid_pinyins_;
  rime::hash_set<int> custom_disabled_blocks_;
  bool loaded_ = false;
};

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_STORE_H_
