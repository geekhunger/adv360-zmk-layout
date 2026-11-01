"""Permit the requested right Ctrl position while guarding every old binding."""
import re

RIGHT_CONTROL_POSITION = 53

def layer_entries(text, layer):
    match = re.search(layer + r"_layer\s*\{.*?bindings\s*=\s*<(.*?)>", text, re.S)
    assert match, layer
    tokens = list(re.finditer(r"\S+", match[1]))
    entries = []
    index = 0
    while index < len(tokens):
        first = tokens[index]
        count = 2 if first[0] in ("&kp", "&adv2", "&mo", "&tog") else 1
        last = tokens[index + count - 1]
        entries.append((" ".join(token[0] for token in tokens[index:index+count]),
                        match.start(1)+first.start(), match.start(1)+last.end()))
        index += count
    assert len(entries) == 76, (layer, len(entries))
    return entries

def restore_right_control_baseline(text):
    replacements = []
    for layer in ("macos", "numpad"):
        binding, start, end = layer_entries(text, layer)[RIGHT_CONTROL_POSITION]
        assert binding == "&kp RCTRL", (layer, binding)
        replacements.append((start, end, "&none"))
    assert layer_entries(text, "windows")[RIGHT_CONTROL_POSITION][0] == "&trans"
    assert layer_entries(text, "macos")[52][0] == "&kp LCTRL"
    for start, end, original in sorted(replacements, reverse=True):
        text = text[:start] + original + text[end:]
    return text


def undo_authorized_os_trigger_move(name, data):
    data = undo_linux_extension(name, data)
    changes = []
    if name == "config/adv360.keymap":
        text = data.decode()
        for pos in (68, 67):
            binding, start, end = layer_entries(text, "bluetooth")[pos]
            assert binding == "BTOS", "Both Cmd/Esc thumbs must switch OS under MOD"
            assert layer_entries(text, "macos")[pos][0] == "&adv2 ADV2_DUAL_CMD_ESCAPE"
            text = text[:start] + "BTC" + text[end:]
        data = text.encode()
        changes = [
            (b"#define BTOS &btctl ADV360_BT_TOGGLE_OS\n", b""),
            (b"&none &kp N1 &kp N2 &kp N3 &kp N4 &kp N5 &none   &none &none",
             b"&tog WINDOWS &kp N1 &kp N2 &kp N3 &kp N4 &kp N5 &none   &none &none"),
            (b"BTOS BTS1 BTS2 BTS3 BTS4 BTS5 BTC", b"BTC BTS1 BTS2 BTS3 BTS4 BTS5 BTC"),
        ]
    elif name == "include/dt-bindings/zmk/adv360_bt_control.h":
        changes = [(b"#define ADV360_BT_TOGGLE_OS 8\n", b"")]
    elif name == "src/behaviors/behavior_adv360_bt_control.c":
        changes = [(b"    case ADV360_BT_TOGGLE_OS:\n"
                    b"        deactivate_bt_control();\n"
                    b"        return zmk_keymap_layer_toggle(1); // Existing Windows delta; profile memory observes it.\n\n", b"")]
    for new, old in changes:
        assert data.count(new) == 1, "Expected isolated OS trigger delta: " + name
        data = data.replace(new, old)
    return data


def undo_linux_extension(name, data):
    """Keep old hash gates: normalize ONLY explicitly tested Linux additions."""
    text = data.decode()
    if name == "config/adv360.keymap":
        linux = [e[0] for e in layer_entries(text, "linux")]
        windows = [e[0] for e in layer_entries(text, "windows")]
        assert linux[52] == "&kp LCTRL" and windows[52] == "&kp LGUI"
        linux[52] = windows[52]
        assert linux == windows
        assert windows[65] == "&adv2 ADV2_PC_SPACE"
        _, a, b = layer_entries(text, "windows")[65]
        text = text[:a] + "&trans" + text[b:]
        text, n = re.subn(r'        linux_layer \{.*?        \};\n\n', '', text, flags=re.S)
        assert n == 1
        text = text.replace('#include <dt-bindings/zmk/adv360_layers.h>\n', '')
        text = text.replace('#define MACOS ADV360_MACOS\n#define WINDOWS ADV360_WINDOWS\n#define LINUX ADV360_LINUX\n#define NUMPAD ADV360_NUMPAD\n#define BLUETOOTH ADV360_BLUETOOTH', '#define MACOS 0\n#define WINDOWS 1\n#define NUMPAD 2\n#define BLUETOOTH 3')
    elif name == "include/dt-bindings/zmk/adv2_layout.h":
        text = text.replace("\n/* Direct Space passthrough; only PC Cmd+Space selects the OS launcher. */\n#define ADV2_PC_SPACE 0x800\n", "")
    elif name == "src/behaviors/behavior_adv360_bt_control.c":
        text = text.replace('#include <dt-bindings/zmk/adv360_layers.h>\n#include <adv360_profile_os.h>\n', '')
        text = text.replace('#define NUMPAD_LAYER ADV360_NUMPAD\n#define BLUETOOTH_LAYER ADV360_BLUETOOTH', '#define NUMPAD_LAYER 2\n#define BLUETOOTH_LAYER 3')
        text = text.replace('return adv360_profile_os_cycle();', 'return zmk_keymap_layer_toggle(1); // Existing Windows delta; profile memory observes it.')
    elif name == "src/behaviors/behavior_adv2_resolver.c":
        text = text.replace('#include <dt-bindings/zmk/adv360_layers.h>\n', '')
        text = text.replace('#define WINDOWS_LAYER ADV360_WINDOWS\n#define LINUX_LAYER ADV360_LINUX', '#define WINDOWS_LAYER 1')
        text = text.replace('#define NUMPAD_LAYER ADV360_NUMPAD', '#define NUMPAD_LAYER 2')
        text = text.replace('static bool is_linux(void) { return zmk_keymap_layer_active(LINUX_LAYER); }\n/* Linux inherits Windows shortcut roles, but overrides OS-specific emitters. */\nstatic bool is_windows(void) { return zmk_keymap_layer_active(WINDOWS_LAYER) || is_linux(); }', 'static bool is_windows(void) { return zmk_keymap_layer_active(WINDOWS_LAYER); }')
        text = text.replace('static bool pc_space_shortcut[ADV2_MAX_POSITIONS];\n', '')
        text = text.replace('    for (size_t i = 0; i < ARRAY_SIZE(pc_space_shortcut); i++) pc_space_shortcut[i] = false;\n', '')
        text = text.replace('        const bool pc_gui_guard = is_windows() && state->hold_keycode == LGUI &&\n                                  !has_any(zmk_hid_get_explicit_mods(), MOD_CTRL);\n        if (pc_gui_guard) emit_code(RCTRL, true);\n', '')
        text = text.replace('        if (pc_gui_guard) emit_code(RCTRL, false);\n', '')
        start = text.index('/* Action-level OS translation.')
        end = text.index('static uint32_t base_keycode(', start)
        text = text[:start] + text[end:]
        start = text.index('    /* Native Cmd+Alt desktop shortcuts')
        end = text.index('    if (is_linux() && resolve_linux_semantic', start)
        text = text[:start] + text[end:]
        start = text.index('/* Linux de(basic):')
        end = text.index('static void resolve_semantic(', start)
        text = text[:start] + text[end:]
        text = text.replace('    if (is_linux() && resolve_linux_semantic(id, held, count)) return;\n', '')
        text = text.replace('return is_linux() ? LALT : (is_windows() ? RALT : LALT);', 'return is_windows() ? RALT : LALT;')
        text = text.replace('    case 8:\n        return LGUI;', '    case 8:\n        return is_windows() ? LCTRL : LGUI;')
        text = text.replace('((!is_windows() || is_linux()) && has_any(mods, MOD_GUI))', '(!is_windows() && has_any(mods, MOD_GUI))')
        start = text.index('static bool resolve_linux_navigation(')
        end = text.index('static bool resolve_windows_navigation(', start)
        text = text[:start] + text[end:]
        text = text.replace('    if (is_linux() || (is_windows() && has_any(zmk_hid_get_explicit_mods(), MOD_GUI)))\n        return resolve_linux_navigation(action);\n', '')
        text = text.replace('    const bool alt_mode = has_any(mods, MOD_ALT) && !has_any(mods, MOD_CG);', '    const bool alt_mode = has_any(mods, MOD_ALT) && !has_any(mods, MOD_CSG);')
        text, n = re.subn(r'    if \(action == ADV2_PC_SPACE\) \{.*?        return ZMK_BEHAVIOR_OPAQUE;\n    \}\n\n', '', text, flags=re.S)
        assert n == 2
        text, n = re.subn(r'            if \(is_windows\(\) && !is_linux\(\) && keycode == ESCAPE &&.*?            \} else \{\n                tap_code\(keycode\);\n            \}', '            tap_code(keycode);', text, flags=re.S)
        assert n == 1
    return text.encode()
