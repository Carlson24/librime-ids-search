project(rime-ids-search)
cmake_minimum_required(VERSION 3.10)

aux_source_directory(src ids_search_src)

add_library(rime-ids-search-objs OBJECT ${ids_search_src})
if(BUILD_SHARED_LIBS)
  set_target_properties(rime-ids-search-objs
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

set(plugin_name rime-ids-search PARENT_SCOPE)
set(plugin_objs $<TARGET_OBJECTS:rime-ids-search-objs> PARENT_SCOPE)
set(plugin_deps ${rime_library} PARENT_SCOPE)
set(plugin_modules "ids_search" PARENT_SCOPE)
