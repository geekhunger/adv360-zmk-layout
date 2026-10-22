"""Protect all tracked input/board/config sources of the user's working firmware."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Installed reference: 7fc24060f439241154ae71cc6b044a9730f3afd1.
# config/version.dtsi is generated build provenance, not input behavior.
BASE_HASHES = {
    "config/adv360.conf": "27134d9acff8fbd0970babca6ec3d6577a98dd2d8918869370e54f5517b0cccd",
    "config/adv360.keymap": "6bdc6f7de7224c84cb28aea9a0cea9478da7d98517eacdcb5f5d4e35ba9854c9",
    "config/adv360_left.keymap": "2a6c4d7fc4c5525eecfbb7b1ac0ad96ea548bf9260d7be30255fc627010a0a58",
    "config/adv360_right.keymap": "2a6c4d7fc4c5525eecfbb7b1ac0ad96ea548bf9260d7be30255fc627010a0a58",
    "config/boards/arm/adv360/Kconfig": "a4c1ec6b36cdfa453b6b77e3ac306287b7a848cf6cfe58b089227be4fc3f5718",
    "config/boards/arm/adv360/Kconfig.board": "5b373d475cfdf0696bb583f40c94404a931b4c1906464e2480559f3663c9582c",
    "config/boards/arm/adv360/Kconfig.defconfig": "0eab5bd6c4ac06b6a063927bdef331a4ef7597accd301ed93e04585c0933231a",
    "config/boards/arm/adv360/adv360-layouts.dtsi": "01cfc0cab9c7339d185dc7c762bf294d4e6ed1cf88a2cc5ff794c9f4b2c8e6ab",
    "config/boards/arm/adv360/adv360.dtsi": "4cedca294eb3e2bb9aa691da38e0a30e5e130ad692a3ce91eb3e53e4e38ace60",
    "config/boards/arm/adv360/adv360.yaml": "590bdcc62137717aa5d8a29fbabd027513790df5aabaa72b4162e79a40db220e",
    "config/boards/arm/adv360/adv360.zmk.yml": "d3b983ca1fbdee2d15c729fc1e2fc65349fa4d93bad8762314e780c96930e305",
    "config/boards/arm/adv360/adv360_left.dts": "3cf34117a201a16eb8d070ab5ed308b03193081fa210d233922159ecd1a4f476",
    "config/boards/arm/adv360/adv360_left_defconfig": "0fc5658f1d5eb49ad3be641609e6223bdc32d57fcb54fae78870493f8d16dd0f",
    "config/boards/arm/adv360/adv360_pinctrl.dtsi": "a77fdd0be7d3a0350981835a7b35f3a997e124ac9c738d4b7b4be09c6c44f5c0",
    "config/boards/arm/adv360/adv360_right.dts": "4c758ecdab011291321129d2be0c0be9c9be30a2f98f6e82fb5b3ff69d17d395",
    "config/boards/arm/adv360/adv360_right_defconfig": "91128c057e216875753591c570c99a99f31b378d5b8c695057823ef9a2429037",
    "config/boards/arm/adv360/board.cmake": "d5a3a091774b8dd71d76ce0240649b4d23f5c6a91c4b0d6b089497256d00bfed",
    "config/boards/arm/adv360/macros.dtsi": "3b1b8b22281265ae17842168793893f1a238cd192b87ea6920c8abd164abb588",
    "config/info.json": "2d2e2289898871fa1ee3d477702318a11fe9e1752cc9b6926ecef5c266fd6dcd",
    "config/keymap.json": "2c0d10a6843fdf48b49f2f1e936dc9b8cc303c0cc25fa728c92a570f4b127464",
    "config/macros.dtsi": "a1a469cef334a3446a24025655d0ad3a628c0f33b222d3f7baf3507350df146a",
    "config/west.yml": "4d487bc9df5a8091c5e01ad86bf641cd41142169584604d65f9be3f3be8f2c98",
    "diagnostic-config/scoped-trace.conf": "e14fe0619ba128d7deba9158dece1340c21bc4c9f042745139112c72532bd6ec",
    "diagnostic-config/scoped-trace.overlay": "29b4934d94b332b3ca3c440c1e054cfbcb9ff2a4513ff9ea4c46fc5ced5525a4",
    "include/adv360_trace.h": "da7998ebb649a6d505fd7c855004bfbcdeba0f2d12744666116d60fed9af0ea8",
    "include/dt-bindings/zmk/adv2_layout.h": "363bffb6c34820b07188675c88e1597f4cddbb5e56b1e0f3748b9be711b355b5",
    "include/dt-bindings/zmk/adv360_bt_control.h": "34c8dcfe15ac6ae4f6db86b591bb3a8b306079fa82cf1f5dff42ebca6e2817d9",
    "patches/zmk-scoped-timing-trace.patch": "8140fa56a79a97752ee39bd2d6ee209dc856cebf680b52829358fbfe5354a1df",
    "patches/zmk-split-notify-backpressure.patch": "d4c2f4b2adf8039bc7eddc60d4b284d0fe4aaf521e09df7112487c4534b6975d",
    "patches/zmk-usb-hid-stable-buffer.patch": "2c3606e4b07ff873855940403cb204f044d9817444e1c78fe7bde494976d1995",
    "src/behaviors/behavior_adv2_resolver.c": "428446e13f4d1c3e209ba0896cada0dc607e9ab633d857927a51be58e6b1bb0e",
    "src/behaviors/behavior_adv360_bt_control.c": "2703ccf249c9a757ceb06e8493556759ab5fb6300cc0a0e8d522f97681f3f758",
    "src/diagnostics/adv360_trace.c": "3f04c2e3b6dcdf1586cb37488d29cca00ec4a989200e3879c7ee8eded14cf97b"
}

for name, expected in BASE_HASHES.items():
    actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    assert actual == expected, "Input/configuration drift: " + name
source = (ROOT / "src/adv360_profile_os.c").read_text()
subscriptions = [line for line in source.splitlines() if line.startswith("ZMK_SUBSCRIPTION")]
assert subscriptions == [
    "ZMK_SUBSCRIPTION(adv360_profile_os, zmk_ble_active_profile_changed);",
    "ZMK_SUBSCRIPTION(adv360_profile_os, zmk_layer_state_changed);",
]
assert "zmk_endpoints" not in source, "USB must not create a separate OS choice"
assert "k_work_reschedule_for_queue(&profile_os_queue" in source
print(f"PASS input boundary: {len(BASE_HASHES)} original files byte-identical; only profile/layer subscriptions")
