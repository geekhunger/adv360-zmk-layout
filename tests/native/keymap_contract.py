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
