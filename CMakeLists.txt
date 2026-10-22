# SPDX-License-Identifier: MIT

target_include_directories(app PRIVATE include)

target_sources_ifdef(CONFIG_ADV360_SCOPED_TRACE app PRIVATE src/diagnostics/adv360_trace.c)

if(CONFIG_ZMK_BEHAVIOR_ADV2_RESOLVER)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/behaviors/behavior_adv2_resolver.c)
  endif()
endif()

if(CONFIG_ZMK_BEHAVIOR_ADV360_BT_CONTROL)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/behaviors/behavior_adv360_bt_control.c)
  endif()
endif()
