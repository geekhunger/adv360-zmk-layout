#!/usr/bin/env python3
"""Install only the companion settings required by the Advantage360 Linux mode.

Run as the graphical-session user. Without --apply this only prints the plan.
Backups and previous GSettings values are kept under ~/.local/state/adv360-linux/.
No root access, key logging or injected key events.
"""
import argparse
import ast
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("desktop", choices=("gnome", "omarchy"))
p.add_argument("--apply", action="store_true")
a = p.parse_args()
home = Path.home()
env = dict(os.environ)
env["XDG_RUNTIME_DIR"] = f"/run/user/{os.getuid()}"
env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={env['XDG_RUNTIME_DIR']}/bus")
def run(*args):
    return subprocess.check_output(args, env=env, text=True).strip()
def get(schema, key):
    raw = run("gsettings", "get", schema, key)
    return ast.literal_eval(raw.removeprefix("@as "))

changes = {}
settings = []
compose = home / ".XCompose"
original = compose.read_text() if compose.exists() else 'include "%L"\n'
line = '<Multi_key> <p> <i> : "π" U03C0'
if line not in original:
    changes[compose] = original.rstrip() + '\n\n# Advantage360 Linux: held P\n' + line + '\n'

if a.desktop == "gnome":
    for schema, key, extra in [
        ("org.gnome.desktop.input-sources", "xkb-options", "compose:menu"),
        ("org.gnome.desktop.wm.keybindings", "minimize", "F20"),
        ("org.gnome.desktop.wm.keybindings", "close", "F21"),
        ("org.gnome.shell.keybindings", "toggle-application-view", "<Super>space"),
    ]:
        old = get(schema, key)
        new = old if extra in old else old + [extra]
        settings.append((schema, key, old, new))
    schema = "org.gnome.desktop.wm.keybindings"
    old = get(schema, "switch-input-source")
    settings.append((schema, "switch-input-source", old,
                     [key for key in old if key != "<Super>space"]))
    base = "org.gnome.settings-daemon.plugins.media-keys"
    path = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/adv360-system-monitor/"
    schema = base + ".custom-keybinding:" + path
    for key, value in [("name", "Advantage360 process monitor"),
                       ("command", "gnome-system-monitor"), ("binding", "<Super><Alt>Escape")]:
        settings.append((schema, key, get(schema, key), value))
    old = get(base, "custom-keybindings")
    settings.append((base, "custom-keybindings", old, old if path in old else old+[path]))
else:
    # Import only the compositor socket identifier, never the full environment.
    for line_env in run("systemctl", "--user", "show-environment").splitlines():
        if line_env.startswith("HYPRLAND_INSTANCE_SIGNATURE="):
            env["HYPRLAND_INSTANCE_SIGNATURE"] = line_env.split("=", 1)[1]
    options = json.loads(run("hyprctl", "getoption", "input:kb_options", "-j"))["str"].split(",")
    if "compose:menu" not in options: options.append("compose:menu")
    lua = home / ".config/hypr/adv360.lua"
    changes[lua] = '''-- Advantage360 Linux companion. Existing Omarchy shortcuts remain available.
hl.config({ input = { kb_layout = "de", kb_options = %s } })
o.bind("F20", "Advantage360 hide window (Cmd+Alt+H restores scratchpad)", hl.dsp.window.move({ workspace = "special:scratchpad", follow = false }))
o.bind("SUPER + ALT + H", "Advantage360 restore hidden windows", "hyprctl dispatch togglespecialworkspace scratchpad")
o.bind("F21", "Advantage360 close window", hl.dsp.window.close())
o.bind("SUPER + ALT + Escape", "Advantage360 process monitor", { tui = "btop" })
''' % json.dumps(",".join(filter(None, options)))
    bindings = home / ".config/hypr/bindings.lua"
    old = bindings.read_text()
    if 'require("hypr.adv360")' not in old:
        changes[bindings] = old.rstrip() + '\n\nrequire("hypr.adv360")\n'
    # Match the already configured de XKB layout in the Fcitx Latin input source.
    profile = home / ".config/fcitx5/profile"
    if profile.exists():
        old = profile.read_text()
        if "Default Layout=us" in old and "Name=keyboard-us" in old:
            changes[profile] = old.replace("Default Layout=us", "Default Layout=de").replace("DefaultIM=keyboard-us", "DefaultIM=keyboard-de").replace("Name=keyboard-us", "Name=keyboard-de")

print(json.dumps({"desktop": a.desktop, "files": [str(f) for f in changes],
                  "settings": settings}, ensure_ascii=False, indent=2))
if not a.apply: raise SystemExit(0)
backup = home / ".local/state/adv360-linux" / datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
backup.mkdir(parents=True)
manifest = {"files": {}, "settings": settings}
for i, (path, content) in enumerate(changes.items()):
    saved = backup / f"file-{i}"
    if path.exists(): shutil.copy2(path, saved)
    manifest["files"][str(path)] = str(saved) if saved.exists() else None
(backup / "restore.json").write_text(json.dumps(manifest, indent=2))
if a.desktop == "omarchy":
    # Let Fcitx flush its old profile before replacing the Latin source.
    run("systemctl", "--user", "stop", "omarchy-fcitx5.service")
try:
    for path, content in changes.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
finally:
    if a.desktop == "omarchy":
        run("systemctl", "--user", "start", "omarchy-fcitx5.service")
for schema, key, old, new in settings:
    if old != new: run("gsettings", "set", schema, key, repr(new))
if a.desktop == "omarchy":
    print(run("hyprctl", "reload"))
    errors = run("hyprctl", "configerrors")
    if errors: raise RuntimeError(f"Hyprland config errors; backups in {backup}: {errors}")
    print(run("omarchy-restart-xcompose"))
    print("Fcitx:", run("systemctl", "--user", "is-active", "omarchy-fcitx5.service"))
else:
    for schema, key, old, new in settings:
        assert get(schema, key) == new, (schema, key)
print("Applied; backups:", backup)
print("Reopen text applications so their Compose tables include the new pi sequence.")
