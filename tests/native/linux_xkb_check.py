"""Decode real resolver HID snapshots with the target's libxkbcommon.

No injected input, no key logging, no graphical-session changes.
Usage: python3 linux_xkb_check.py resolver-symbol-dump.jsonl
"""
import ctypes as c
import json
import sys

x = c.CDLL("libxkbcommon.so.0")
def api(name, result, *args):
    f = getattr(x, name)
    f.restype, f.argtypes = result, args
    return f
ptr, uint, text = c.c_void_p, c.c_uint32, c.c_char_p
ctx = api("xkb_context_new", ptr, c.c_int)(0)
class Names(c.Structure):
    _fields_ = [(n, text) for n in ("rules", "model", "layout", "variant", "options")]
keymap = api("xkb_keymap_new_from_names", ptr, ptr, c.POINTER(Names), c.c_int)(
    ctx, c.byref(Names(b"evdev", b"pc105", b"de", b"", b"compose:menu")), 0)
assert keymap
state = api("xkb_state_new", ptr, ptr)(keymap)
index = api("xkb_keymap_mod_get_index", uint, ptr, text)
update = api("xkb_state_update_mask", uint, ptr, uint, uint, uint, uint, uint, uint)
key_by_name = api("xkb_keymap_key_by_name", uint, ptr, text)
sym = api("xkb_state_key_get_one_sym", uint, ptr, uint)
utf8 = api("xkb_state_key_get_utf8", c.c_int, ptr, uint, ptr, c.c_size_t)
compose_src = b'include "%L"\n<Multi_key> <p> <i> : "\xcf\x80" U03C0\n'
table = api("xkb_compose_table_new_from_buffer", ptr, ptr, text, c.c_size_t, text, c.c_int, c.c_int)(
    ctx, compose_src, len(compose_src), b"en_US.UTF-8", 1, 0)
assert table
compose = api("xkb_compose_state_new", ptr, ptr, c.c_int)(table, 0)
feed = api("xkb_compose_state_feed", c.c_int, ptr, uint)
status = api("xkb_compose_state_get_status", c.c_int, ptr)
composed_utf8 = api("xkb_compose_state_get_utf8", c.c_int, ptr, ptr, c.c_size_t)
reset = api("xkb_compose_state_reset", None, ptr)

# USB HID usage -> XKB physical key name. No keycap/layout-name assumptions.
names = dict(zip(range(4,30), ["AC01","AB05","AB03","AC03","AD03","AC04",
    "AC05","AC06","AD08","AC07","AC08","AC09","AB07","AB06","AD09",
    "AD10","AD01","AD04","AC02","AD05","AD07","AB04","AD02","AB02","AD06","AB01"]))
names.update({30+i: f"AE{i+1:02}" for i in range(10)})
names.update({44:"SPCE",45:"AE11",46:"AE12",47:"AD11",48:"AD12",49:"BKSL",
    51:"AC10",52:"AC11",53:"TLDE",54:"AB08",55:"AB09",56:"AB10",87:"KPAD",100:"LSGT",101:"MENU"})
mask_names = ["Control","Shift","Mod1","Mod4","Control","Shift","Mod5","Mod4"]
cases = [json.loads(line) for line in open(sys.argv[1]) if line.startswith('{"expected"')]
assert len(cases) >= 34
for case in cases:
    reset(compose)
    result = ""
    for usage, mods in case["events"]:
        mask = 0
        for bit, name in enumerate(mask_names):
            if mods & (1 << bit): mask |= 1 << index(keymap, name.encode())
        update(state, mask, 0, 0, 0, 0, 0)
        key = key_by_name(keymap, names[usage].encode())
        assert key, usage
        feed(compose, sym(state, key))
        mode = status(compose)
        buffer = c.create_string_buffer(64)
        if mode == 2:  # COMPOSED
            composed_utf8(compose, buffer, len(buffer)); reset(compose)
        elif mode == 0:
            utf8(state, key, buffer, len(buffer))
        elif mode == 3:
            raise AssertionError((case, "Compose cancelled"))
        result += buffer.value.decode()
    assert result == case["expected"], (case, result)
    assert status(compose) == 0, (case, "unfinished dead key/Compose")
    print("PASS", repr(result))
print(f"PASS {len(cases)} Linux symbols decoded by target XKB/Compose")
