//
// ids_store.h — ids_search 插件数据层
// 加载 IDX2 / MET2 二进制数据到内存，提供寻址与缓存。
//
#ifndef RIME_IDS_SEARCH_IDS_STORE_H_
#define RIME_IDS_SEARCH_IDS_STORE_H_

#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <rime/common.h>

namespace ids_search {

namespace fs = std::filesystem;

// 偏旁等价归一表（与 Lua ids_search.lua 的 canon_map 完全一致）
const rime::hash_map<std::string, std::string>& CanonMap();
std::string Canon(const std::string& c);

struct MetaRecord {
  uint32_t freq = 0;
  uint8_t block_id = 0;
  uint8_t depth = 10;
  std::string py;
  std::string ids;
  std::string alts_str;
  std::vector<std::vector<std::string>>
      alts;  // alts_str 解析结果（部件已 canon 归一）
  bool valid = false;
};

struct PostingsInfo {
  uint32_t off = 0;  // 1-indexed，相对 postings 段起始
  uint32_t count = 0;
};

// 简单 LRU 缓存（供 meta / postings set / 查询 / 切分等使用）
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

// IDS 数据仓库：懒加载单例，全部数据驻留内存。
class IdsStore {
 public:
  static IdsStore& Instance();

  bool Load(const fs::path& index_path,
            const fs::path& meta_path,
            const fs::path& custom_path);
  bool loaded() const { return loaded_; }

  const rime::hash_map<std::string, PostingsInfo>& keys() const {
    return keys_;
  }
  const std::map<std::string, std::vector<std::string>>& py_map() const {
    return py_map_;
  }
  const rime::hash_set<std::string>& valid_pinyins() const {
    return valid_pinyins_;
  }

  // 元数据查询（带 LRU 缓存）。越界/无记录返回共享的空记录。
  const MetaRecord* GetMeta(uint32_t cp);

  // 读取某 key 倒排的前 limit 条 codepoint（字频降序）。
  size_t GetPostings(const std::string& key,
                     uint32_t limit,
                     std::vector<uint32_t>* out);

  // 某 key 的完整倒排集合（带 LRU 缓存）。
  const std::shared_ptr<rime::hash_set<uint32_t>>& GetPostingsSet(
      const std::string& key);

 private:
  IdsStore() = default;
  IdsStore(const IdsStore&) = delete;
  IdsStore& operator=(const IdsStore&) = delete;

  bool ParseIndex();
  bool ParseMeta();
  MetaRecord ParseMetaRecord(size_t pos) const;
  void ApplyCustomRules(const fs::path& path);

  bool loaded_ = false;
  std::string index_blob_;
  std::string meta_blob_;
  std::vector<uint32_t> meta_offsets_;
  uint32_t meta_max_cp_ = 0;
  uint32_t postings_base_ = 0;
  uint32_t num_keys_ = 0;

  rime::hash_map<std::string, PostingsInfo> keys_;
  std::map<std::string, std::vector<std::string>> py_map_;
  rime::hash_set<std::string> valid_pinyins_;

  LruCache<uint32_t, std::shared_ptr<MetaRecord>> meta_cache_{4096};
  LruCache<std::string, std::shared_ptr<rime::hash_set<uint32_t>>> set_cache_{
      64};
};

// 读文件到内存字符串（二进制）
bool ReadBinaryFile(const fs::path& p, std::string* out);

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_STORE_H_