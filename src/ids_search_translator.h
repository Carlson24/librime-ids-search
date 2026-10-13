//
// ids_search_translator.h — ids_search 组件（部件检字翻译器）。
//
#ifndef RIME_IDS_SEARCH_TRANSLATOR_H_
#define RIME_IDS_SEARCH_TRANSLATOR_H_

#include <string>
#include <vector>

#include <rime/translator.h>

namespace rime {
struct Segment;
}

namespace ids_search {

class IdsSearchTranslator : public rime::Translator {
 public:
  explicit IdsSearchTranslator(const rime::Ticket& ticket);

  rime::an<rime::Translation> Query(const std::string& input,
                                    const rime::Segment& segment) override;

 private:
  void Initialize();
  bool EnsureLoaded();
  std::string PinyinMode();
  rime::an<rime::Translation> BuildCandidates(
      const std::vector<uint32_t>& results,
      const std::string& query_raw,
      size_t start_pos,
      size_t end_pos);
  std::vector<uint32_t> Search(const std::string& query_raw,
                               const std::string& mode);

  std::string tag_;
  std::string prefix_;
  size_t prefix_len_ = 0;
  char prefix_byte_ = 0;
  int max_results_ = 60;
  std::string shuangpin_scheme_ = "flypy";
  bool show_pinyin_ = true;
  bool show_ids_ = true;
  bool show_unicode_ = true;
  bool show_block_ = true;
  bool has_sw_pinyin_ = false;
  bool has_sw_ids_ = false;
  bool has_sw_unicode_ = false;
  bool has_sw_block_ = false;
  std::string tips_;
  std::string index_path_;
  std::string meta_path_;
  std::string custom_path_;
  rime::hash_set<int> disabled_blocks_;
  bool disabled_blocks_enabled_ = false;
  bool initialized_ = false;
};

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_TRANSLATOR_H_