//
// ids_store.cc — IDS3 单文件数据加载与寻址（mmap 惰性映射）。
//
#include "ids_store.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ids_search {

const rime::hash_map<std::string, std::string>& CanonMap() {
  static const rime::hash_map<std::string, std::string> kMap = {
      {"氵", "⺡"}, {"水", "⺡"}, {"⺡", "⺡"}, {"氺", "⺡"}, {"灬", "⺣"},
      {"火", "⺣"}, {"⺣", "⺣"}, {"亻", "⺅"}, {"人", "⺅"}, {"𠆢", "⺅"},
      {"⺅", "⺅"}, {"扌", "龵"}, {"手", "龵"}, {"龵", "龵"}, {"忄", "⺖"},
      {"心", "⺖"}, {"⺖", "⺖"}, {"⺗", "⺖"}, {"⺼", "肉"}, {"月", "肉"},
      {"肉", "肉"}, {"⺝", "肉"}, {"艹", "⺾"}, {"艸", "⺾"}, {"⺾", "⺾"},
      {"⻀", "⺾"}, {"草", "⺾"}, {"礻", "⺬"}, {"示", "⺬"}, {"⺬", "⺬"},
      {"衤", "⺭"}, {"衣", "⺭"}, {"⺭", "⺭"}, {"𧘇", "⺭"}, {"釒", "⻐"},
      {"金", "⻐"}, {"钅", "⻐"}, {"⻐", "⻐"}, {"⻑", "⻐"}, {"飠", "⻠"},
      {"食", "⻠"}, {"饣", "⻠"}, {"⻠", "⻠"}, {"⻡", "⻠"}, {"𩙿", "⻠"},
      {"糹", "⺰"}, {"糸", "⺰"}, {"纟", "⺰"}, {"⺰", "⺰"}, {"⺱", "⺰"},
      {"犭", "⺲"}, {"犬", "⺲"}, {"⺲", "⺲"}, {"辶", "⻌"}, {"辵", "⻌"},
      {"⻌", "⻌"}, {"⻍", "⻌"}, {"阝", "⻖"}, {"阜", "⻖"}, {"邑", "⻖"},
      {"⻖", "⻖"}, {"⻗", "⻖"}, {"王", "⺩"}, {"⺩", "⺩"}, {"𤣩", "⺩"},
      {"玉", "⺩"}, {"⺆", "冂"}, {"冂", "冂"}, {"攵", "⺙"}, {"攴", "⺙"},
      {"⺙", "⺙"}, {"耂", "⺹"}, {"老", "⺹"}, {"⺹", "⺹"}, {"足", "⻊"},
      {"⻊", "⻊"}, {"𧾷", "⻊"}, {"四", "⺫"}, {"罒", "⺫"}, {"网", "⺫"},
      {"⺫", "⺫"}, {"竹", "⺮"}, {"⺮", "⺮"}, {"𥫗", "⺮"}, {"刀", "⺉"},
      {"刂", "⺉"}, {"⺈", "⺉"}, {"⺉", "⺉"}, {"羊", "⺶"}, {"⺶", "⺶"},
      {"⺷", "⺶"}, {"𦍌", "⺶"}, {"言", "讠"}, {"讠", "讠"}, {"訁", "讠"},
      {"号", "号"}, {"號", "号"}, {"牛", "牜"}, {"牜", "牜"}, {"⺧", "牜"},
      {"龍", "龙"}, {"龙", "龙"}, {"馬", "马"}, {"马", "马"}, {"鳥", "鸟"},
      {"鸟", "鸟"}, {"魚", "鱼"}, {"鱼", "鱼"}, {"車", "车"}, {"车", "车"},
      {"門", "门"}, {"门", "门"}, {"見", "见"}, {"见", "见"}, {"貝", "贝"},
      {"贝", "贝"}, {"頁", "页"}, {"页", "页"}, {"風", "风"}, {"风", "风"},
      {"飛", "飞"}, {"飞", "飞"}, {"黽", "黾"}, {"黾", "黾"}, {"齊", "齐"},
      {"齐", "齐"}, {"齒", "齿"}, {"齿", "齿"}, {"長", "长"}, {"长", "长"},
      {"撇", "㇒"}, {"丿", "㇒"}, {"㇒", "㇒"}, {"横", "㇐"}, {"一", "㇐"},
      {"㇐", "㇐"}, {"竖", "㇑"}, {"丨", "㇑"}, {"㇑", "㇑"}, {"点", "㇔"},
      {"丶", "㇔"}, {"㇔", "㇔"}, {"捺", "㇏"}, {"乀", "㇏"}, {"乁", "㇏"},
      {"㇏", "㇏"}, {"提", "㇀"}, {"㇀", "㇀"}, {"折", "乛"}, {"乛", "乛"},
      {"钩", "亅"}, {"鉤", "亅"}, {"亅", "亅"},
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

const rime::hash_map<std::string, int>& BlockAliasMap() {
  static const rime::hash_map<std::string, int> kMap = {
      {"基本", 0},      {"基本区", 0},      {"basic", 0},     {"base", 0},
      {"cjk", 0},       {"扩a", 1},         {"exta", 1},      {"ext-a", 1},
      {"a", 1},         {"扩b", 2},         {"extb", 2},      {"ext-b", 2},
      {"b", 2},         {"扩c", 3},         {"extc", 3},      {"ext-c", 3},
      {"c", 3},         {"扩d", 4},         {"extd", 4},      {"ext-d", 4},
      {"d", 4},         {"扩e", 5},         {"exte", 5},      {"ext-e", 5},
      {"e", 5},         {"扩f", 6},         {"extf", 6},      {"ext-f", 6},
      {"f", 6},         {"扩g", 7},         {"extg", 7},      {"ext-g", 7},
      {"g", 7},         {"扩h", 8},         {"exth", 8},      {"ext-h", 8},
      {"h", 8},         {"扩i", 9},         {"exti", 9},      {"ext-i", 9},
      {"i", 9},         {"扩j", 10},        {"extj", 10},     {"ext-j", 10},
      {"j", 10},        {"兼容区", 11},     {"兼容", 11},     {"compat", 11},
      {"兼容补充", 12}, {"compat-sup", 12}, {"部首补充", 13}, {"rad-sup", 13},
      {"康熙部首", 14}, {"kangxi", 14},     {"笔画", 15},     {"strokes", 15},
  };
  return kMap;
}

// ---------------------------------------------------------------------------
// MappedFile：POSIX mmap；Windows 退化为整读缓冲。
// ---------------------------------------------------------------------------

struct IdsStore::MappedFile {
  const uint8_t* data = nullptr;
  size_t size = 0;
#if !defined(_WIN32)
  int fd = -1;
  void* addr = nullptr;
  ~MappedFile() {
    if (addr)
      ::munmap(addr, size);
    if (fd >= 0)
      ::close(fd);
  }
#else
  std::string buf;
  ~MappedFile() = default;
#endif
};

namespace {

inline uint64_t Rd64(const uint8_t* p) {
  return static_cast<uint64_t>(Rd32(p)) |
         (static_cast<uint64_t>(Rd32(p + 4)) << 32);
}

std::string ToLowerAscii(const std::string& s) {
  std::string r = s;
  for (auto& ch : r) {
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch + 32);
  }
  return r;
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

IdsStore::~IdsStore() = default;

bool IdsStore::Parse() {
  if (size_ < 32 || std::memcmp(base_, "IDS3", 4) != 0)
    return false;
  max_cp_ = Rd32(base_ + 8);
  num_chars_ = Rd32(base_ + 12);
  uint32_t nkeys = Rd32(base_ + 16);
  uint32_t nsec = Rd32(base_ + 20);
  const uint8_t* st = base_ + 32;
  if (size_ < 32 + static_cast<size_t>(nsec) * 24)
    return false;

  for (uint32_t i = 0; i < nsec; ++i) {
    const uint8_t* e = st + static_cast<size_t>(i) * 24;
    uint32_t type = Rd32(e);
    uint64_t off = Rd64(e + 8);
    uint64_t len = Rd64(e + 16);
    if (off + len > size_)
      return false;
    const uint8_t* p = base_ + off;
    switch (type) {
      case kSecCp2Id:
        cp2id_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecId2Cp:
        id2cp_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecHot:
        hot_ = reinterpret_cast<const HotRec*>(p);
        break;
      case kSecAltsOff:
        alts_off_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecAlts:
        alts_ = p;
        break;
      case kSecCompSetOff:
        compset_off_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecCompSet:
        compset_ = p;
        break;
      case kSecColdOff:
        cold_off_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecCold:
        cold_ = p;
        break;
      case kSecPostHead:
        post_head_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecPostSmall:
        post_small_ = reinterpret_cast<const uint32_t*>(p);
        break;
      case kSecPostBitset:
        post_bitset_ = reinterpret_cast<const uint64_t*>(p);
        break;
      default:
        break;
    }
  }
  if (!cp2id_ || !id2cp_ || !hot_ || !alts_off_ || !alts_ || !compset_off_ ||
      !compset_ || !cold_off_ || !cold_ || !post_head_ || !post_small_ ||
      !post_bitset_)
    return false;

  // TOKENS
  for (uint32_t i = 0; i < nsec; ++i) {
    const uint8_t* e = st + static_cast<size_t>(i) * 24;
    if (Rd32(e) != kSecTokens)
      continue;
    const uint8_t* p = base_ + Rd64(e + 8);
    const uint8_t* end = p + Rd64(e + 16);
    if (p + 2 > end)
      return false;
    num_tokens_ = Rd16(p);
    p += 2;
    token_str_.reserve(num_tokens_);
    token_cp_.reserve(num_tokens_);
    for (uint16_t t = 0; t < num_tokens_; ++t) {
      if (p + 2 > end)
        return false;
      uint16_t len = Rd16(p);
      p += 2;
      if (p + len + 4 > end)
        return false;
      std::string s(reinterpret_cast<const char*>(p), len);
      p += len;
      uint32_t cp = Rd32(p);
      p += 4;
      token_index_[s] = t;
      token_str_.push_back(std::move(s));
      token_cp_.push_back(cp);
    }
    break;
  }

  // PY
  for (uint32_t i = 0; i < nsec; ++i) {
    const uint8_t* e = st + static_cast<size_t>(i) * 24;
    if (Rd32(e) != kSecPy)
      continue;
    const uint8_t* p = base_ + Rd64(e + 8);
    const uint8_t* end = p + Rd64(e + 16);
    if (p + 2 > end)
      return false;
    uint16_t np = Rd16(p);
    p += 2;
    for (uint16_t k = 0; k < np; ++k) {
      if (p + 2 > end)
        return false;
      uint16_t len = Rd16(p);
      p += 2;
      if (p + len + 2 > end)
        return false;
      std::string py(reinterpret_cast<const char*>(p), len);
      p += len;
      uint16_t m = Rd16(p);
      p += 2;
      if (p + static_cast<size_t>(m) * 2 > end)
        return false;
      std::vector<uint16_t> ids;
      ids.reserve(m);
      for (uint16_t j = 0; j < m; ++j) {
        ids.push_back(Rd16(p));
        p += 2;
      }
      valid_pinyins_.insert(py);
      py_map_[py] = std::move(ids);
    }
    break;
  }

  // KEYS
  for (uint32_t i = 0; i < nsec; ++i) {
    const uint8_t* e = st + static_cast<size_t>(i) * 24;
    if (Rd32(e) != kSecKeys)
      continue;
    const uint8_t* p = base_ + Rd64(e + 8);
    const uint8_t* end = p + Rd64(e + 16);
    if (p + 4 > end)
      return false;
    uint32_t n = Rd32(p);
    p += 4;
    keys_.reserve(n);
    for (uint32_t k = 0; k < n; ++k) {
      if (p + 2 > end)
        return false;
      uint16_t klen = Rd16(p);
      p += 2;
      if (p + klen + 17 > end)
        return false;
      std::string key(reinterpret_cast<const char*>(p), klen);
      p += klen;
      KeyEntry ke;
      ke.head_off = Rd32(p);
      ke.head_cnt = Rd32(p + 4);
      ke.full_off = Rd32(p + 8);
      ke.full_cnt = Rd32(p + 12);
      ke.kind = p[16];
      p += 17;
      keys_[std::move(key)] = ke;
    }
    break;
  }
  (void)nkeys;
  return true;
}

bool IdsStore::SmallContains(const KeyEntry& e, uint32_t id) const {
  const uint32_t* a = post_small_ + e.full_off;
  return std::binary_search(a, a + e.full_cnt, id);
}

bool IdsStore::Load(const fs::path& data_path, const fs::path& custom_path) {
  if (loaded_)
    return true;

  auto file = std::make_shared<MappedFile>();
#if !defined(_WIN32)
  file->fd = ::open(data_path.c_str(), O_RDONLY);
  if (file->fd < 0)
    return false;
  struct stat sb;
  if (::fstat(file->fd, &sb) != 0 || sb.st_size <= 0) {
    ::close(file->fd);
    return false;
  }
  file->size = static_cast<size_t>(sb.st_size);
  file->addr = ::mmap(nullptr, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
  if (file->addr == MAP_FAILED) {
    file->addr = nullptr;
    ::close(file->fd);
    return false;
  }
  file->data = static_cast<const uint8_t*>(file->addr);
#else
  {
    std::ifstream f(data_path, std::ios::binary);
    if (!f)
      return false;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz <= 0)
      return false;
    file->buf.resize(static_cast<size_t>(sz));
    f.seekg(0, std::ios::beg);
    f.read(&file->buf[0], sz);
    file->data = reinterpret_cast<const uint8_t*>(file->buf.data());
    file->size = static_cast<size_t>(sz);
  }
#endif

  file_ = file;
  base_ = file->data;
  size_ = file->size;

  if (!Parse()) {
    file_.reset();
    base_ = nullptr;
    size_ = 0;
    return false;
  }
  if (!custom_path.empty())
    ApplyCustomRules(custom_path);
  loaded_ = true;
  return true;
}

void IdsStore::ApplyCustomRules(const fs::path& path) {
  std::ifstream in(path);
  if (!in)
    return;

  auto add_token_pair = [&](std::vector<uint16_t>* v, const std::string& comp) {
    uint16_t t;
    if (FindToken(comp, &t) && std::find(v->begin(), v->end(), t) == v->end())
      v->push_back(t);
    std::string c = Canon(comp);
    if (c != comp && FindToken(c, &t) &&
        std::find(v->begin(), v->end(), t) == v->end())
      v->push_back(t);
  };
  auto remove_token_pair = [&](std::vector<uint16_t>* v,
                               const std::string& comp) {
    uint16_t t;
    if (FindToken(comp, &t))
      v->erase(std::remove(v->begin(), v->end(), t), v->end());
    std::string c = Canon(comp);
    if (c != comp && FindToken(c, &t))
      v->erase(std::remove(v->begin(), v->end(), t), v->end());
  };

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
    if (tokens.size() >= 2 &&
        (tokens[0] == "-block" || tokens[0] == "-区" || tokens[0] == "-分区")) {
      for (size_t k = 1; k < tokens.size(); ++k) {
        auto it = BlockAliasMap().find(ToLowerAscii(tokens[k]));
        if (it != BlockAliasMap().end())
          custom_disabled_blocks_.insert(it->second);
      }
    } else if (tokens.size() == 1 && !tokens[0].empty() &&
               tokens[0][0] == '-') {
      std::string del_py = ToLowerAscii(tokens[0].substr(1));
      py_map_.erase(del_py);
      valid_pinyins_.erase(del_py);
    } else if (tokens.size() >= 2) {
      const std::string& comp = tokens[0];
      bool has_del = false;
      bool has_override = false;
      for (size_t k = 1; k < tokens.size(); ++k) {
        if (tokens[k].empty())
          continue;
        if (tokens[k][0] == '-')
          has_del = true;
        else if (tokens[k][0] == '=')
          has_override = true;
      }
      if (has_del) {
        for (size_t k = 1; k < tokens.size(); ++k) {
          if (!tokens[k].empty() && tokens[k][0] == '-') {
            std::string del_py = ToLowerAscii(tokens[k].substr(1));
            auto it = py_map_.find(del_py);
            if (it != py_map_.end()) {
              remove_token_pair(&it->second, comp);
              if (it->second.empty())
                py_map_.erase(it);
            }
          }
        }
      } else if (has_override) {
        for (auto& kv : py_map_)
          remove_token_pair(&kv.second, comp);
        for (size_t k = 1; k < tokens.size(); ++k) {
          std::string py =
              (tokens[k][0] == '=') ? tokens[k].substr(1) : tokens[k];
          py = ToLowerAscii(py);
          if (py.empty())
            continue;
          add_token_pair(&py_map_[py], comp);
          valid_pinyins_.insert(py);
        }
      } else {
        for (size_t k = 1; k < tokens.size(); ++k) {
          std::string py = ToLowerAscii(tokens[k]);
          if (py.empty())
            continue;
          add_token_pair(&py_map_[py], comp);
          valid_pinyins_.insert(py);
        }
      }
    }
  }
  in.close();
}

}  // namespace ids_search
