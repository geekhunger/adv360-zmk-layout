# geekhunger's Advantage 360 Pro layout

ZMK-Portierung des langjährig verwendeten Advantage-2-QMK-Layouts aus
[`geekhunger/qmk_firmware`](https://github.com/geekhunger/qmk_firmware/tree/kint36/keyboards/kinesis/keymaps/herrsch).
Zielgerät ist eine **unveränderte Kinesis Advantage 360 Pro**. Auf dem Host
muss wie bisher das **deutsche Tastaturlayout** aktiv sein.

## Eigenschaften

- macOS-Grundebene, sparsames dauerhaftes Windows-Delta und momentane NUM-Ebene
- Umschalten macOS ↔ Windows durch `NUM halten` + `Esc tippen`
- Home-Row-Numpad mit absichtlich gesperrten übrigen Positionen
- Tap/Hold-Symbole und kombinierbare Modifier-Regeln aus dem QMK-Original
- getrennte macOS-/Windows-Sequenzen für semantisch gleiche Sonderzeichen
- Windows-Navigation als Gegenstück zum macOS-Verhalten
- originale QMK-Timings: 90 ms für Modifier, 170 ms für Buchstaben
- ein gemeinsamer paralleler 90-ms-Resolver für alle sechs Dual-Role-Tasten
- Buchstabentaps werden beim Loslassen sofort ausgegeben; 170 ms gelten nur für Holds
- 120-ms-Fenster für „tippen, erneut tippen und halten“ bei Delete/Backspace
- saubere Serialisierung sich überlappender Pfeile derselben Achse
- zusätzliche Advantage-360-Tasten auf allen Ebenen ohne Funktion

Die vollständige Portierungsvereinbarung und jede technische Abweichung stehen
in [`PORTING_NOTES.md`](PORTING_NOTES.md). Der eigene, eng begrenzte Resolver
liegt in
[`src/behaviors/behavior_adv2_resolver.c`](src/behaviors/behavior_adv2_resolver.c).

> `config/adv360.keymap` nicht durch einen generischen Keymap-Editor-Export
> ersetzen. Ein visueller Editor kann die kombinierte Resolver-Logik nicht
> verlustfrei darstellen.

## Firmware über GitHub Actions bauen

Jeder Push startet den Workflow **Build**. Nach erfolgreichem Lauf:

1. Den neuesten erfolgreichen Lauf unter **Actions → Build** öffnen.
2. Das Artefakt `firmware-no-clique` herunterladen.
3. Das ZIP entpacken. Es enthält zusammengehörige `left.uf2` und `right.uf2`.

Der Workflow baut beide Hälften auf Basis von Kinesis V3.0 und dessen
festgelegtem ZMK-Fork.

## Lokal bauen

Docker oder Podman und `make` werden benötigt. Auf Apple Silicon muss die
Container-Engine gegebenenfalls als x86_64 laufen.

```sh
make
```

Die UF2-Dateien erscheinen anschließend in `firmware/`.

## Flashen

Die normalen Kinesis-Bootloader-Tastenkombinationen sind absichtlich nicht
belegt. Verwende deshalb die physischen Reset-/Bootloader-Taster:

1. Beide Hälften vom Rechner trennen und ausschalten.
2. Linke Hälfte per USB direkt anschließen.
3. Den Reset-Taster der linken Hälfte mit einer Büroklammer schnell doppelt
   drücken. Das Laufwerk `ADV360PRO` erscheint.
4. Die Datei mit `left.uf2` auf dieses Laufwerk kopieren. Warten, bis es sich
   selbst aushängt und die blauen Installations-LEDs erlöschen.
5. Linke Hälfte eingeschaltet und wach lassen.
6. Rechte Hälfte per USB anschließen und deren Reset-Taster doppelt drücken.
7. Die zugehörige Datei mit `right.uf2` kopieren und erneut bis zum Ende warten.
8. Beide Hälften aus- und wieder einschalten; zuerst links, danach rechts.

Niemals die linke und rechte Datei vertauschen oder Firmware aus zwei
verschiedenen Builds mischen. Die offizielle bebilderte Anleitung ist bei
[Kinesis](https://kinesis-ergo.com/wp-content/uploads/Advantage360-Professional-Firmware-Update-Instructions-9.5.24-KB360-PRO.pdf)
verfügbar.

Bei einem Wechsel von einer anderen Hauptversion auf V3.0 kann vorher ein
passendes Settings Reset nötig sein. Dazu die
[Kinesis-V3-Hinweise](https://kinesis-ergo.com/support/kb360pro/#firmware-updates)
beachten; nicht vorsorglich resetten, wenn bereits eine V3-Firmware läuft.

## Herkunft

Hardwaredefinition, Split-/Bluetooth-Code und Build-Basis stammen aus
[`KinesisCorporation/Adv360-Pro-ZMK`](https://github.com/KinesisCorporation/Adv360-Pro-ZMK),
Branch `V3.0`. Die ursprüngliche MIT-Lizenz bleibt erhalten.
