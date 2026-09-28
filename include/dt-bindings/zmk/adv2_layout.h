/* SPDX-License-Identifier: MIT */
#pragma once

/* Semantic key IDs. Their behavior is implemented in behavior_adv2_resolver.c. */
#define ADV2_W 1
#define ADV2_E 2
#define ADV2_R 3
#define ADV2_F 4
#define ADV2_A 5
#define ADV2_S 6
#define ADV2_D 7
#define ADV2_T 8
#define ADV2_G 9
#define ADV2_Z 10
#define ADV2_X 11
#define ADV2_C 12
#define ADV2_V 13
#define ADV2_B 14
#define ADV2_Y 15
#define ADV2_U 16
#define ADV2_I 17
#define ADV2_O 18
#define ADV2_H 19
#define ADV2_N 20
#define ADV2_K 21
#define ADV2_L 22
#define ADV2_P 23
#define ADV2_J 24
#define ADV2_M 25
#define ADV2_COMMA 26
#define ADV2_DOT 27
#define ADV2_SLASH 28

#define ADV2_NAV_FLAG 0x200
#define ADV2_NAV_LEFT (ADV2_NAV_FLAG | 1)
#define ADV2_NAV_UP (ADV2_NAV_FLAG | 2)
#define ADV2_NAV_RIGHT (ADV2_NAV_FLAG | 3)
#define ADV2_NAV_DOWN (ADV2_NAV_FLAG | 4)
#define ADV2_NAV_DELETE (ADV2_NAV_FLAG | 5)
#define ADV2_NAV_BACKSPACE (ADV2_NAV_FLAG | 6)

/* Independent QMK-style 90 ms mod-taps; no side/chord heuristic. */
#define ADV2_DUAL_FLAG 0x400
#define ADV2_DUAL_DELETE (ADV2_DUAL_FLAG | 5)
#define ADV2_DUAL_BACKSPACE (ADV2_DUAL_FLAG | 6)
