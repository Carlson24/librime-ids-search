//
// ids_store.cc — IDX2 / MET2 二进制数据加载与寻址。
//
#include "ids_store.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

namespace ids_search {

const rime::hash_map<std::string, std::string>& CanonMap() {
  static const rime::hash_map<std::string, std::string> kMap = {
      {"氵", "⺡"},
      {"水", "⺡"},
      {"⺡", "⺡"},
      {"氺", "⺡"},
      {"灬", "⺣"},
      {"火", "⺣"},
      {"⺣", "⺣"},
      {"亻", "⺅"},
      {"人", "⺅"},
      {"𠆢", "⺅"},
      {"⺅", "⺅"},
      {"扌", "龵"},
      {"手", "龵"},
      {"龵", "龵"},
      {"忄", "⺖"},
      {"心", "⺖"},
      {"⺖", "⺖"},
      {"⺗", "⺖"},
      {"⺼", "肉"},
      {"月", "肉"},
      {"肉", "肉"},
      {"⺝", "肉"},
      {"艹", "⺾"},
      {"艸", "⺾"},
      {"⺾", "⺾"},
      {"⻀", "⺾"},
      {"草", "⺾"},
      {"礻", "⺬"},
      {"示", "⺬"},
      {"⺬", "⺬"},
      {"衤", "⺭"},
      {"衣", "⺭"},
      {"⺭", "⺭"},
      {"𧘇", "⺭"},
      {"釒", "⻐"},
      {"金", "⻐"},
      {"钅", "⻐"},
      {"⻐", "⻐"},
      {"⻑", "⻐"},
      {"飠", "⻠"},
      {"食", "⻠"},
      {"饣", "⻠"},
      {"⻠", "⻠"},
      {"⻡", "⻠"},
      {"𩙿", "⻠"},
      {"糹", "⺰"},
      {"糸", "⺰"},
      {"纟", "⺰"},
      {"⺰", "⺰"},
      {"⺱", "⺰"},
      {"犭", "⺲"},
      {"犬", "⺲"},
      {"⺲", "⺲"},
      {"辶", "⻌"},
      {"辵", "⻌"},
      {"⻌", "⻌"},
      {"⻍", "⻌"},
      {"阝", "⻖"},
      {"阜", "⻖"},
      {"邑", "⻖"},
      {"⻖", "⻖"},
      {"⻗", "⻖"},
      {"王", "⺩"},
      {"⺩", "⺩"},
      {"𤣩", "⺩"},
      {"玉", "⺩"},
      {"⺆", "冂"},
      {"冂", "冂"},
      {"攵", "⺙"},
      {"攴", "⺙"},
      {"⺙", "⺙"},
      {"耂", "⺹"},
      {"老", "⺹"},
      {"⺹", "⺹"},
      {"足", "⻊"},
      {"⻊", "⻊"},
      {"𧾷", "⻊"},
      {"四", "⺫"},
      {"罒", "⺫"},
      {"网", "⺫"},
      {"⺫", "⺫"},
      {"竹", "⺮"},
      {"⺮", "⺮"},
      {"𥫗", "⺮"},
      {"刀", "⺉"},
      {"刂", "⺉"},
      {"⺈", "⺉"},
      {"⺉", "⺉"},
      {"羊", "⺶"},
      {"⺶", "⺶"},
      {"⺷", "⺶"},
      {"𦍌", "⺶"},
      {"言", "讠"},
      {"讠", "讠"},
      {"訁", "讠"},
      {"号", "号"},
      {"號", "号"},
      {"牛", "牜"},
      {"牜", "牜"},
      {"⺧", "牜"},
      {"龍", "龙"},
      {"龙", "龙"},
      {"馬", "马"},
      {"马", "马"},
      {"鳥", "鸟"},
      {"鸟", "鸟"},
      {"魚", "鱼"},
      {"鱼", "鱼"},
      {"車", "车"},
      {"车", "车"},
      {"門", "门"},
      {"门", "门"},
      {"見", "见"},
      {"见", "见"},
      {"貝", "贝"},
      {"贝", "贝"},
      {"頁", "页"},
      {"页", "页"},
      {"風", "风"},
      {"风", "风"},
      {"飛", "飞"},
      {"飞", "飞"},
      {"饣", "食"},
      {"黽", "黾"},
      {"黾", "黾"},
      {"齊", "齐"},
      {"齐", "齐"},
      {"齒", "齿"},
      {"齿", "齿"},
      {"長", "长"},
      {"长", "长"},
      // 笔画名称与标准笔画字符等价归一
      {"撇", "㇒"},
      {"丿", "㇒"},
      {"㇒", "㇒"},
      {"横", "㇐"},
      {"一", "㇐"},
      {"㇐", "㇐"},
      {"竖", "㇑"},
      {"丨", "㇑"},
      {"㇑", "㇑"},
      {"点", "㇔"},
      {"丶", "㇔"},
      {"㇔", "㇔"},
      {"捺", "㇏"},
      {"乀", "㇏"},
      {"乁", "㇏"},
      {"㇏", "㇏"},
      {"提", "㇀"},
      {"㇀", "㇀"},
      {"折", "乛"},
      {"乛", "乛"},
      {"钩", "亅"},
      {"鉤", "亅"},
      {"亅", "亅"},
  };
  return kMap;
}

std::string Canon(const std::string& c) {
  if (c.empty())
    return c;
  if (c[0] == '{' && c[c.size() - 1] == '}') {
    std::string inner = c.substr(1, c.size() - 2);
    auto it = CanonMap().find(inner);
    return it != CanonMap().end() ? it->second : inner;
  }
  auto it = CanonMap().find(c);
  return it != CanonMap().end() ? it->second : c;
}

bool ReadBinaryFile(const fs::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return false;
  f.seekg(0, std::ios::end);
  std::streamoff sz = f.tellg();
  if (sz <= 0) {
    f.close();
    return false;
  }
  out->resize(static_cast<size_t>(sz));
  f.seekg(0, std::ios::beg);
  if (sz > 0)
    f.read(&(*out)[0], sz);
  bool ok = f.good() || f.eof();
  f.close();
  return ok;
}

namespace {

inline uint32_t rd_le32(const char* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

inline uint16_t rd_le16(const char* p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return v;
}

// 拆 alts_str（'|' 分隔变体，',' 分隔部件），部件按 canon 归一。
void ParseAlts(const std::string& s,
               std::vector<std::vector<std::string>>* out) {
  size_t i = 0, n = s.size();
  while (i <= n) {
    size_t j = s.find('|', i);
    if (j == std::string::npos)
      j = n;
    std::vector<std::string> comps;
    size_t k = i;
    while (k <= j) {
      size_t m = s.find(',', k);
      if (m == std::string::npos || m > j)
        m = j;
      if (m > k)
        comps.push_back(Canon(s.substr(k, m - k)));
      k = m + 1;
      if (m == j)
        break;
    }
    if (!comps.empty())
      out->push_back(std::move(comps));
    i = j + 1;
    if (j == n)
      break;
  }
}

void Trim(std::string* s) {
  size_t b = 0, e = s->size();
  while (b < e && (s->at(b) == ' ' || s->at(b) == '\t' || s->at(b) == '\r'))
    ++b;
  while (e > b && (s->at(e - 1) == ' ' || s->at(e - 1) == '\t' ||
                   s->at(e - 1) == '\r' || s->at(e - 1) == '\n'))
    --e;
  *s = s->substr(b, e - b);
}

}  // namespace

IdsStore& IdsStore::Instance() {
  static IdsStore kInstance;
  return kInstance;
}

bool IdsStore::ParseIndex() {
  if (index_blob_.size() < 16)
    return false;
  if (index_blob_.compare(0, 4, "IDX2") != 0)
    return false;
  const char* base = index_blob_.data();
  num_keys_ = rd_le32(base + 4);
  uint32_t keys_dir_len = rd_le32(base + 8);
  uint32_t py_block_len = rd_le32(base + 12);
  uint32_t region = 16u + py_block_len + keys_dir_len;
  if (index_blob_.size() < region)
    return false;
  postings_base_ = region;

  // 解析 @py 拼音块
  size_t py_beg = 16;
  const char* pb = base + py_beg;
  size_t pos = 0;
  while (pos < py_block_len) {
    size_t nl = pos;
    while (nl < py_block_len && pb[nl] != '\n')
      ++nl;
    size_t line_end = nl;
    if (line_end > pos && pb[line_end - 1] == '\r')
      line_end -= 1;
    if (line_end > pos) {
      std::string line(pb + pos, line_end - pos);
      size_t t1 = line.find('\t');
      if (t1 != std::string::npos) {
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 != std::string::npos) {
          std::string py = line.substr(t1 + 1, t2 - t1 - 1);
          std::string comps_str = line.substr(t2 + 1);
          std::vector<std::string> comps;
          size_t k = 0;
          while (k < comps_str.size()) {
            size_t m = k;
            while (m < comps_str.size() && comps_str[m] != ' ' &&
                   comps_str[m] != '\t')
              ++m;
            if (m > k)
              comps.push_back(comps_str.substr(k, m - k));
            k = m + 1;
          }
          py_map_[py] = std::move(comps);
          valid_pinyins_.insert(py);
        }
      }
    }
    pos = nl + 1;
  }

  // 解析 keys_dir
  size_t dir_beg = region - keys_dir_len;
  const char* kb = base + dir_beg;
  size_t dpos = 0;
  while (dpos + 10 <= keys_dir_len) {
    uint16_t klen = rd_le16(kb + dpos);
    dpos += 2;
    if (dpos + klen + 8 > keys_dir_len)
      break;
    std::string key(kb + dpos, klen);
    dpos += klen;
    uint32_t off = rd_le32(kb + dpos);
    uint32_t count = rd_le32(kb + dpos + 4);
    dpos += 8;
    keys_[key] = PostingsInfo{off, count};
  }
  return true;
}

bool IdsStore::ParseMeta() {
  if (meta_blob_.size() < 12)
    return false;
  if (meta_blob_.compare(0, 4, "MET2") != 0)
    return false;
  const char* base = meta_blob_.data();
  meta_max_cp_ = rd_le32(base + 4);
  uint32_t data_len = rd_le32(base + 8);
  size_t array_len = static_cast<size_t>(meta_max_cp_) * 4;
  if (meta_blob_.size() < 12 + array_len + data_len)
    return false;
  meta_offsets_.resize(meta_max_cp_);
  std::memcpy(meta_offsets_.data(), base + 12, array_len);
  return true;
}

MetaRecord IdsStore::ParseMetaRecord(size_t pos) const {
  MetaRecord m;
  const char* base = meta_blob_.data();
  size_t size = meta_blob_.size();
  if (pos + 8 > size)
    return m;
  m.freq = rd_le32(base + pos);
  m.block_id = static_cast<uint8_t>(base[pos + 4]);
  m.depth = static_cast<uint8_t>(base[pos + 5]);
  uint8_t py_len = static_cast<uint8_t>(base[pos + 6]);
  uint8_t ids_len = static_cast<uint8_t>(base[pos + 7]);
  size_t off = pos + 8;
  if (py_len > 0) {
    if (off + py_len > size)
      return m;
    m.py.assign(base + off, py_len);
    off += py_len;
  }
  if (ids_len > 0) {
    if (off + ids_len > size)
      return m;
    m.ids.assign(base + off, ids_len);
    off += ids_len;
  }
  if (off + 2 > size)
    return m;
  uint16_t alts_len = rd_le16(base + off);
  off += 2;
  if (alts_len > 0) {
    if (off + alts_len > size)
      return m;
    m.alts_str.assign(base + off, alts_len);
    ParseAlts(m.alts_str, &m.alts);
  }
  m.valid = true;
  return m;
}

const MetaRecord* IdsStore::GetMeta(uint32_t cp) {
  static const MetaRecord kEmpty;
  if (cp == 0 || cp >= meta_max_cp_)
    return &kEmpty;
  if (auto* hit = meta_cache_.Find(cp))
    return hit->get();
  uint32_t rec_pos = meta_offsets_[cp];
  if (rec_pos == 0)
    return &kEmpty;
  size_t pos = static_cast<size_t>(rec_pos) - 1;
  auto& slot = meta_cache_.Ensure(cp);
  if (!slot) {
    auto parsed = std::make_shared<MetaRecord>(ParseMetaRecord(pos));
    if (!parsed->valid) {
      meta_cache_.Erase(cp);
      return &kEmpty;
    }
    slot = std::move(parsed);
  }
  return slot.get();
}

size_t IdsStore::GetPostings(const std::string& key,
                             uint32_t limit,
                             std::vector<uint32_t>* out) {
  auto it = keys_.find(key);
  if (it == keys_.end() || it->second.count == 0)
    return 0;
  uint32_t n = std::min(limit, it->second.count);
  size_t start = static_cast<size_t>(postings_base_) + it->second.off - 1;
  size_t need = start + static_cast<size_t>(n) * 4;
  if (need > index_blob_.size())
    return 0;
  const char* p = index_blob_.data() + start;
  out->reserve(out->size() + n);
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t c;
    std::memcpy(&c, p + i * 4, 4);
    out->push_back(c);
  }
  return n;
}

const std::shared_ptr<rime::hash_set<uint32_t>>& IdsStore::GetPostingsSet(
    const std::string& key) {
  auto& slot = set_cache_.Ensure(key);
  if (!slot) {
    auto it = keys_.find(key);
    auto s = std::make_shared<rime::hash_set<uint32_t>>();
    if (it != keys_.end() && it->second.count > 0) {
      size_t start = static_cast<size_t>(postings_base_) + it->second.off - 1;
      size_t need = start + static_cast<size_t>(it->second.count) * 4;
      if (need <= index_blob_.size()) {
        const char* p = index_blob_.data() + start;
        s->reserve(it->second.count);
        for (uint32_t i = 0; i < it->second.count; ++i) {
          uint32_t c;
          std::memcpy(&c, p + i * 4, 4);
          s->insert(c);
        }
      }
    }
    slot = std::move(s);
  }
  return slot;
}

bool IdsStore::Load(const fs::path& index_path,
                    const fs::path& meta_path,
                    const fs::path& custom_path) {
  if (loaded_)
    return true;
  std::string idx, meta;
  if (!ReadBinaryFile(index_path, &idx) || !ReadBinaryFile(meta_path, &meta))
    return false;
  index_blob_ = std::move(idx);
  meta_blob_ = std::move(meta);
  if (!ParseIndex() || !ParseMeta()) {
    index_blob_.clear();
    meta_blob_.clear();
    keys_.clear();
    py_map_.clear();
    valid_pinyins_.clear();
    meta_offsets_.clear();
    return false;
  }
  if (!custom_path.empty())
    ApplyCustomRules(custom_path);
  meta_cache_.Clear();
  set_cache_.Clear();
  loaded_ = true;
  return true;
}

void IdsStore::ApplyCustomRules(const fs::path& path) {
  std::ifstream in(path);
  if (!in)
    return;
  std::string line;
  while (std::getline(in, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos)
      line.erase(hash);
    Trim(&line);
    if (line.empty())
      continue;
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        ++i;
      size_t j = i;
      while (j < line.size() && line[j] != ' ' && line[j] != '\t')
        ++j;
      if (j > i)
        tokens.push_back(line.substr(i, j - i));
      i = j;
    }
    if (tokens.size() == 1 && !tokens[0].empty() && tokens[0][0] == '-') {
      // 全局禁用某读音: -er
      std::string del_py = tokens[0].substr(1);
      py_map_.erase(del_py);
      valid_pinyins_.erase(del_py);
    } else if (tokens.size() >= 2) {
      const std::string& comp = tokens[0];
      bool has_del = false;
      for (size_t i = 1; i < tokens.size(); ++i) {
        if (!tokens[i].empty() && tokens[i][0] == '-')
          has_del = true;
      }
      if (has_del) {
        // 移除 comp 的若干读音: 阝 -er
        for (size_t i = 1; i < tokens.size(); ++i) {
          if (!tokens[i].empty() && tokens[i][0] == '-') {
            std::string del_py = tokens[i].substr(1);
            auto it = py_map_.find(del_py);
            if (it != py_map_.end()) {
              auto& list = it->second;
              list.erase(std::remove(list.begin(), list.end(), comp),
                         list.end());
              if (list.empty())
                py_map_.erase(it);
            }
          }
        }
      } else if (tokens[1][0] == '=') {
        // 覆盖重设读音: 阝 =fu yi
        std::string first_py = tokens[1].substr(1);
        std::vector<std::string> all_keys;
        for (auto& kv : py_map_)
          all_keys.push_back(kv.first);
        for (auto& p : all_keys) {
          auto& list = py_map_[p];
          list.erase(std::remove(list.begin(), list.end(), comp), list.end());
          if (list.empty())
            py_map_.erase(p);
        }
        if (!first_py.empty()) {
          py_map_[first_py].push_back(comp);
          valid_pinyins_.insert(first_py);
        }
        for (size_t i = 2; i < tokens.size(); ++i) {
          py_map_[tokens[i]].push_back(comp);
          valid_pinyins_.insert(tokens[i]);
        }
      } else {
        // 追加别名/读音: 氵 sandianshui
        for (size_t i = 1; i < tokens.size(); ++i) {
          auto& list = py_map_[tokens[i]];
          if (std::find(list.begin(), list.end(), comp) == list.end())
            list.push_back(comp);
          valid_pinyins_.insert(tokens[i]);
        }
      }
    }
  }
  in.close();
}

}  // namespace ids_search