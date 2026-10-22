"""Check source layout boundaries and optionally the actual compiled Devicetree."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE_HASHES = {
    "macos": "7c3a9f50fbd8022811ff5f4b9fd2ec763210847b624f62ddbfaf3955307cd46c",
    "windows": "a36a3249dfb8aad29ab5a259e476d7b582fa0bd6eb9e6a24e412b2c934f8069c",
    "bluetooth": "25269a90a57e182810ce389eb09993df39329af446b362ab8af1411557ba7a45",
    "numpad": "9f6c02e693bdfebc46845921c558d378c44611a8eee5108d0b585bea1429d6e4",
}
MORPHS = {
    "left": ("kp C_PREV", "bl BL_DEC"),
    "down": ("kp C_VOL_DN", "kp C_BRI_DN"),
    "up": ("kp C_VOL_UP", "kp C_BRI_UP"),
    "right": ("kp C_NEXT", "bl BL_INC"),
}
CHANGES = {65: "&kp C_PP", 66: "&kp LSHIFT", 71: "&num_left",
           72: "&num_down", 73: "&num_up", 74: "&num_right"}


def digest(text):
    return hashlib.sha256(" ".join(text.split()).encode()).hexdigest()


def check_source():
    text = (ROOT / "config/adv360.keymap").read_text()
    for layer, expected in BASE_HASHES.items():
        body = re.search(layer + r"_layer\s*\{.*?bindings\s*=\s*<(.*?)>", text, re.S).group(1)
        if layer == "numpad":
            tokens = body.split()
            entries = []
            while tokens:
                token = tokens.pop(0)
                n = 1 if token in ("&kp", "&adv2", "&mo", "&tog") else 0
                entries.append(" ".join([token] + [tokens.pop(0) for _ in range(n)]))
            assert len(entries) == 76
            for pos, binding in CHANGES.items():
                assert entries[pos] == binding, (pos, entries[pos])
                entries[pos] = "&trans" if pos == 65 else "&none"
            body = " ".join(entries)
        assert digest(body) == expected, "Unexpected binding change on " + layer
    for name, bindings in MORPHS.items():
        body = re.search(r"num_" + name + r":\s*\w+\s*\{(.*?)\};", text, re.S).group(1)
        assert 'compatible = "zmk,behavior-mod-morph";' in body
        actual = re.search(r"bindings\s*=\s*(.*?);", body, re.S).group(1)
        assert re.findall(r"<&([^>]+)>", actual) == list(bindings)
        assert re.search(r"mods\s*=\s*<\(MOD_LSFT\s*\|\s*MOD_RSFT\)>;", body)
        assert "keep-mods" not in body
    print("PASS NUM source: only six authorized positions changed; base, Windows and Bluetooth unchanged")


def check_compiled(filename, module_path):
    sys.path.insert(0, module_path)
    from devicetree import dtlib
    dt = dtlib.DT(filename)

    def bindings(node):
        value = node.props["bindings"].value
        values = list(struct.unpack(">" + "I" * (len(value) // 4), value))
        result = []
        while values:
            target = dt.phandle2node[values.pop(0)]
            n = target.props["#binding-cells"].to_num()
            params = tuple(values[:n])
            del values[:n]
            result.append((target, params))
        return result

    keymap = dt.get_node("/keymap")
    layers = list(keymap.nodes.values())
    assert [node.name for node in layers] == ["macos_layer", "windows_layer", "numpad_layer", "bluetooth_layer"]
    num = bindings(layers[2])
    assert len(num) == 76
    assert num[65][0].props["compatible"].to_string() == "zmk,behavior-key-press"
    assert num[65][1] == (0xC00CD,)
    assert num[66][1] == (0x700E1,) and num[69][1] == (0x700E5,)
    # Actual HID consumer usages and native backlight commands, after CPP/DT.
    expected = [(0xC00B6, "zmk,behavior-backlight", (4, 0)),
                (0xC00EA, "zmk,behavior-key-press", (0xC0070,)),
                (0xC00E9, "zmk,behavior-key-press", (0xC006F,)),
                (0xC00B5, "zmk,behavior-backlight", (3, 0))]
    for pos, (normal_code, shifted_type, shifted_params) in enumerate(expected, 71):
        node, args = num[pos]
        assert not args
        assert node.props["compatible"].to_string() == "zmk,behavior-mod-morph"
        assert node.props["mods"].to_num() == 0x22
        normal, shifted = bindings(node)
        assert normal[0].props["compatible"].to_string() == "zmk,behavior-key-press"
        assert normal[1] == (normal_code,)
        assert shifted[0].props["compatible"].to_string() == shifted_type
        assert shifted[1] == shifted_params
    print("PASS compiled NUM: all nine actions, both Shift keys, shared above Windows and macOS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiled")
    parser.add_argument("--dtlib-source")
    args = parser.parse_args()
    check_source()
    if args.compiled:
        assert args.dtlib_source
        check_compiled(args.compiled, args.dtlib_source)
