//
// ids_search_module.cc — 插件模块注册入口。
//
#include <rime/component.h>
#include <rime/registry.h>
#include <rime_api.h>

#include "ids_search_translator.h"

using namespace rime;

static void rime_ids_search_initialize() {
  Registry& r = Registry::instance();
  r.Register("ids_search_translator",
             new Component<ids_search::IdsSearchTranslator>);
}

static void rime_ids_search_finalize() {}

RIME_REGISTER_MODULE(ids_search)