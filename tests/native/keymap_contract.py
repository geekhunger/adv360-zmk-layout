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
    changes = []
    if name == "config/adv360.keymap":
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
