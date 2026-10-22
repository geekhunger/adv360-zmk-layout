#!/bin/sh
set -eu
elf=${1:?Usage: check_firmware_symbols.sh ELF NM}
nm_tool=${2:?Usage: check_firmware_symbols.sh ELF NM}
symbols=$("$nm_tool" -n "$elf")
subscriptions=$(printf '%s\n' "$symbols" | awk '$3 ~ /^zmk_event_sub_adv2_/ {print $3}')
expected=zmk_event_sub_adv2_inputzmk_position_state_changed
if [ "$subscriptions" != "$expected" ]; then
    printf 'Unexpected resolver subscriptions: %s\n' "$subscriptions" >&2
    exit 1
fi
resolver_address=$(printf '%s\n' "$symbols" | awk -v symbol="$expected" '$3 == symbol {print $1}')
keymap_address=$(printf '%s\n' "$symbols" | awk '$3 == "zmk_event_sub_keymapzmk_position_state_changed" {print $1}')
test -n "$resolver_address"
test -n "$keymap_address"
if [ "$((0x$resolver_address))" -ge "$((0x$keymap_address))" ]; then
    printf 'Resolver must precede keymap: resolver=%s keymap=%s\n' "$resolver_address" "$keymap_address" >&2
    exit 1
fi
printf 'PASS linked firmware: one resolver subscription at %s before keymap at %s\n' "$resolver_address" "$keymap_address"
