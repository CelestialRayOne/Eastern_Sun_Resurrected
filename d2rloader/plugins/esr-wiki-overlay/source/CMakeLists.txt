cmake_minimum_required(VERSION 3.29)
project(ESRWikiOverlay VERSION 1.5.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# PluginSDK-master lives beside Downloads/Work in the original setup; override if not.
set(ESR_PLUGIN_SDK_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../../PluginSDK-master/include"
    CACHE PATH "PluginSDK include folder (contains D2RLPlugin/api.h)")
option(ESR_WIKI_BUILD_TESTS "Build the portable model tests and register them with CTest" ON)

# Portable model: parser, search, paging, color codes. Builds on any host.
add_library(esr_wiki_model STATIC src/wiki_model.cpp src/cube_live.cpp src/web_class_live.cpp src/web_uniques_live.cpp src/web_runes_live.cpp src/web_words_live.cpp src/web_bases_live.cpp src/web_affixes_live.cpp src/web_ascendancy_live.cpp src/web_sets_live.cpp src/web_endgame_live.cpp src/web_special_live.cpp src/web_skills_live.cpp src/web_vessel_live.cpp src/web_kill_live.cpp src/web_mastery_live.cpp src/ias_calc.cpp src/locale_align.cpp src/locale_live.cpp)
target_include_directories(esr_wiki_model PUBLIC src)
if(NOT MSVC)
  target_compile_options(esr_wiki_model PRIVATE -Wall -Wextra -Werror)
else()
  # Every MSVC target must use the same C runtime, or the link fails (LNK2038).
  # The item database plugin uses the static runtime, so match it here.
  set_property(TARGET esr_wiki_model PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ESR_WIKI_BUILD_TESTS)
  enable_testing()
  add_executable(esr_wiki_model_test tests/wiki_model_test.cpp)
  target_link_libraries(esr_wiki_model_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(esr_wiki_model_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET esr_wiki_model_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME wiki_model
           COMMAND esr_wiki_model_test "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The cube page reader must give exactly what the Python tools write (tests/cube_live/).
  add_executable(cube_live_test tests/cube_live_test.cpp)
  target_link_libraries(cube_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(cube_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET cube_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME cube_live
           COMMAND cube_live_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/cube_live/cube-recipes-page.html"
                                  "${CMAKE_CURRENT_SOURCE_DIR}/tests/cube_live/cube-wiki-data.expected.txt")

  # The website uniques builder must give exactly what tools/web_uniques.py writes (data/website-uniques/).
  add_executable(web_uniques_live_test tests/web_uniques_live_test.cpp)
  target_link_libraries(web_uniques_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_uniques_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_uniques_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_uniques_live
           COMMAND web_uniques_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-uniques"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/web-unique-art.txt")

  # The website runes builder must give exactly what tools/web_runes.py writes (data/website-runes/gems.htm).
  add_executable(web_runes_live_test tests/web_runes_live_test.cpp)
  target_link_libraries(web_runes_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_runes_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_runes_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_runes_live
           COMMAND web_runes_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-runes/gems.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/tests/cube_live/cube-recipes-page.html"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website runewords/gemwords builders must give exactly what tools/web_words.py writes (data/website-words/).
  add_executable(web_words_live_test tests/web_words_live_test.cpp)
  target_link_libraries(web_words_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_words_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_words_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_words_live
           COMMAND web_words_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-words/runewords.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/website-words/gemwords.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/website-runes/gems.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/web-word-art.txt"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website item bases builder must give exactly what tools/web_bases.py writes (data/website-bases/).
  add_executable(web_bases_live_test tests/web_bases_live_test.cpp)
  target_link_libraries(web_bases_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_bases_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_bases_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_bases_live
           COMMAND web_bases_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-bases/armors.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/website-bases/weapons.htm"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/web-base-art.txt"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website item affixes builder must give exactly what tools/web_affixes.py writes (data/website-affixes/).
  add_executable(web_affixes_live_test tests/web_affixes_live_test.cpp)
  target_link_libraries(web_affixes_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_affixes_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_affixes_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_affixes_live
           COMMAND web_affixes_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-affixes/prefixes.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/website-affixes/suffixes.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/website-affixes/quality_affixes.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website ascendancy builder must give exactly what tools/web_ascendancy.py writes (data/website-ascendancy/).
  add_executable(web_ascendancy_live_test tests/web_ascendancy_live_test.cpp)
  target_link_libraries(web_ascendancy_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_ascendancy_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_ascendancy_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_ascendancy_live
           COMMAND web_ascendancy_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-ascendancy/ascendancies.htm"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website sets builder must give exactly what tools/web_sets.py writes (data/website-sets/).
  add_executable(web_sets_live_test tests/web_sets_live_test.cpp)
  target_link_libraries(web_sets_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_sets_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_sets_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_sets_live
           COMMAND web_sets_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-sets/sets.htm"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website endgame maps builder must give exactly what tools/web_endgame.py writes (data/website-endgame/).
  add_executable(web_endgame_live_test tests/web_endgame_live_test.cpp)
  target_link_libraries(web_endgame_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_endgame_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_endgame_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_endgame_live
           COMMAND web_endgame_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-endgame/endgame_maps.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website special crafting builder must give exactly what tools/web_special.py writes (data/website-special/).
  add_executable(web_special_live_test tests/web_special_live_test.cpp)
  target_link_libraries(web_special_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_special_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_special_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_special_live
           COMMAND web_special_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-special/special_crafting_outcomes.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website skill information builder must give exactly what tools/web_skills.py writes (data/website-skills/).
  add_executable(web_skills_live_test tests/web_skills_live_test.cpp)
  target_link_libraries(web_skills_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_skills_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_skills_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_skills_live
           COMMAND web_skills_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-skills/skill_information.htm"
                                        "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website vessel of souls builder must give exactly what tools/web_vessel.py writes (data/website-vessel/).
  add_executable(web_vessel_live_test tests/web_vessel_live_test.cpp)
  target_link_libraries(web_vessel_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_vessel_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_vessel_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_vessel_live
           COMMAND web_vessel_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-vessel/vessel_of_souls.htm"
                                        "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website kill ledger builder must give exactly what tools/web_kill.py writes (data/website-kill/).
  add_executable(web_kill_live_test tests/web_kill_live_test.cpp)
  target_link_libraries(web_kill_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_kill_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_kill_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_kill_live
           COMMAND web_kill_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-kill/kill_ledger.htm"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The website weapon mastery builder must give exactly what tools/web_mastery.py writes (data/website-mastery/).
  add_executable(web_mastery_live_test tests/web_mastery_live_test.cpp)
  target_link_libraries(web_mastery_live_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(web_mastery_live_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET web_mastery_live_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME web_mastery_live
           COMMAND web_mastery_live_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-mastery/weapon_mastery.htm"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The Class Crafting page built from the cube recipes page must match the shipped page's items and art.
  add_executable(class_crafting_test tests/class_crafting_test.cpp)
  target_link_libraries(class_crafting_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(class_crafting_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET class_crafting_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME class_crafting
           COMMAND class_crafting_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/cube_live/cube-recipes-page.html"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/data/wiki-data.txt")

  # The localized pages' text, lined up with the English pages, translates the wiki data.
  add_executable(locale_align_test tests/locale_align_test.cpp)
  target_link_libraries(locale_align_test PRIVATE esr_wiki_model)
  if(MSVC)
    set_property(TARGET locale_align_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    target_compile_options(locale_align_test PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME locale_align COMMAND locale_align_test "${CMAKE_CURRENT_SOURCE_DIR}/data")

  # The calculator engine must give what the website's own script gives (tests/gen_ias_cases.js writes the cases).
  add_executable(ias_calc_test tests/ias_calc_test.cpp)
  target_link_libraries(ias_calc_test PRIVATE esr_wiki_model)
  if(NOT MSVC)
    target_compile_options(ias_calc_test PRIVATE -Wall -Wextra -Werror)
  else()
    set_property(TARGET ias_calc_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME ias_calc
           COMMAND ias_calc_test "${CMAKE_CURRENT_SOURCE_DIR}/data/website-ias/ias-calculator.html"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/data/website-ias/ias-calculator-data.js"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/ias_cases.tsv")
endif()

# Win32 overlay and D2RLoader plugin (Windows only).
if(WIN32)
  enable_language(RC)
  add_library(esr_wiki_overlay SHARED src/plugin.cpp src/overlay.cpp src/cube_updater.cpp src/web_fetch.cpp src/web_pages_updater.cpp src/plugin.rc)
  target_include_directories(esr_wiki_overlay PRIVATE "${ESR_PLUGIN_SDK_INCLUDE}")
  target_link_libraries(esr_wiki_overlay PRIVATE esr_wiki_model user32 gdi32 gdiplus msimg32 winhttp)
  set_target_properties(esr_wiki_overlay PROPERTIES OUTPUT_NAME "d2rl-esr-wiki-overlay" PREFIX "")

  # After each successful build, copy the DLL and data into the live mod (see deploy.cmake).
  # Turn off with -DESR_WIKI_DEPLOY_LIVE=OFF, or point elsewhere with -DESR_WIKI_LIVE_PLUGINS_DIR=...
  option(ESR_WIKI_DEPLOY_LIVE "Copy the built overlay into the live mod after each build" ON)
  set(ESR_WIKI_LIVE_PLUGINS_DIR
      "C:/Program Files (x86)/Diablo II Resurrected/mods/Eastern_Sun_Resurrected/d2rloader/plugins"
      CACHE PATH "Live D2RLoader plugins folder for the ESR mod")
  if(ESR_WIKI_DEPLOY_LIVE)
    add_custom_command(TARGET esr_wiki_overlay POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
              "-DSRC_DLL=$<TARGET_FILE:esr_wiki_overlay>"
              "-DSRC_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data"
              "-DDEST=${ESR_WIKI_LIVE_PLUGINS_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/deploy.cmake"
      VERBATIM)
  endif()
  if(MSVC)
    target_compile_options(esr_wiki_overlay PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET esr_wiki_overlay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  # Install layout matches the D2RLoader plugins folder:
  #   plugins/d2rl-esr-wiki-overlay.dll
  #   plugins/esr-wiki-overlay/wiki-data.txt
  #   plugins/esr-wiki-overlay/images/*.png
  install(TARGETS esr_wiki_overlay RUNTIME DESTINATION d2rloader/plugins)
  install(FILES data/wiki-data.txt DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(FILES data/cube-wiki-data.txt DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(FILES data/web-unique-art.txt DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(FILES data/web-word-art.txt DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(FILES data/web-base-art.txt DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(FILES data/website-ias/ias-calculator.html data/website-ias/ias-calculator-data.js
          DESTINATION d2rloader/plugins/esr-wiki-overlay)
  install(DIRECTORY data/images/ DESTINATION d2rloader/plugins/esr-wiki-overlay/images)
  install(DIRECTORY data/locales/ DESTINATION d2rloader/plugins/esr-wiki-overlay/locales FILES_MATCHING PATTERN "*.txt")
endif()
