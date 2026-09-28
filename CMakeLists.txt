# SPDX-License-Identifier: MIT

target_include_directories(app PRIVATE include)

if(CONFIG_ZMK_BEHAVIOR_ADV2_RESOLVER)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/behaviors/behavior_adv2_resolver.c)
  endif()
endif()
