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
- originale QMK-Timings: 90 ms für alle Dual-Role-Modifier, 170 ms für Buchstaben
- ein gemeinsamer paralleler 90-ms-Resolver für alle sechs Dual-Role-Tasten;
  unbenutzte lange Alt-Holds bleiben auch beim Loslassen ohne Ausgabe
- Roll-Erkennung ohne Seitenheuristiken; Alt vor einer überlappenden Zeichentaste
  bleibt Alt, auch wenn Alt zuerst losgelassen wird
- 12-ms-Toleranz für knapp zu früh losgelassene Mehrfach-Modifier
- originale QMK-Tap-Dance-Zählung für schnelle Doppelbuchstaben
- originale QMK-Wiederholungszahl auch für wiederholbare Modifier-Symbole
- 170 ms für Buchstaben-Holds und zum Abschluss einer ununterbrochenen Tap-Serie
- 120-ms-Fenster für unmittelbar aufeinanderfolgendes „tippen, erneut tippen und
  halten“ bei Delete/Backspace; eine Zwischentaste beendet diese Vormerkung
- gepaarte 12-ms-HID-Pulse mit auf die wiederholte Taste begrenzter
  Reihenfolge-Barriere über BLE, damit Space keinen Doppelbuchstaben überholt,
  ohne Backspace oder Modifier hinter anderen Buchstabenpulsen anzustauen
- mehrere parallele Modifier-Ziele für schnelle Pfeil- und Buchstaben-Rolls
- Weitergabe eines gelatchten Modifiers über überlappende Pfeil-Rollovers
- saubere Serialisierung sich überlappender Pfeile derselben Achse
- sequenzielle Bluetooth-Steuerung über die originale rechte `Mod`-Taste
- alle übrigen zusätzlichen Advantage-360-Tasten auf allen Ebenen ohne Funktion

## Medien und Beleuchtung

Diese NUM-Belegungen gelten gleichermaßen unter macOS und Windows:

| Gehaltene Tasten | Aktion |
| --- | --- |
| NUM + Pfeil links/rechts | Vorheriger/nächster Titel |
| NUM + Pfeil runter/hoch | Lautstärke verringern/erhöhen |
| NUM + Leertaste | Wiedergabe/Pause |
| NUM + Shift + Pfeil runter/hoch | Bildschirmhelligkeit verringern/erhöhen |
| NUM + Shift + Pfeil links/rechts | Tastaturbeleuchtung verringern/erhöhen |

Beide Shift-Tasten funktionieren in NUM als reine Modifier. Ohne NUM bleiben
ihre Tab-/Enter-Funktionen unverändert. Links/rechts entsprechen den bisherigen
QMK-Medientasten `KC_MEDIA_PREV_TRACK`/`KC_MEDIA_NEXT_TRACK`, nicht einem
Zeitsprung innerhalb eines Titels. Die Bildschirmhelligkeit wird als normaler
HID-Befehl gesendet; Unterstützung hängt von Rechner, Betriebssystem und Display
ab. Die Tastaturbeleuchtung steuert der verwendete ZMK-Fork auf beiden Hälften.

## Bluetooth-Profile

Die originale rechte `Mod`-Taste aktiviert für 1,5 Sekunden eine eigene
Bluetooth-Steuerebene. Sie erzeugt selbst keine Eingabe.

- `Mod`, dann `NUM halten` + `1`–`5`: Bluetooth-Profil 1–5 auswählen. Ist das
  Profil noch frei, beginnt ZMK anschließend mit dem Pairing.
- `Mod`, dann `Delete` oder `Backspace`: Bindung des aktuell ausgewählten
  Profils löschen, sodass es neu gekoppelt werden kann.
- `Mod`, dann eine andere Taste: Bluetooth-Steuerung verwerfen und die Taste
  normal ausführen.
- `Mod` zweimal oder 1,5 Sekunden ohne Befehl: Bluetooth-Steuerung verwerfen.

Die Profilzahlen liegen absichtlich auf den physischen NUM-Positionen `1`–`5`;
die obere Zahlenreihe löst keine Bluetooth-Aktion aus.

### Mac-/Windows-Auswahl je Bluetooth-Profil

`NUM halten` + `Esc tippen` schaltet wie bisher macOS ↔ Windows um. Die Auswahl
wird jetzt dem aktuell ausgewählten Bluetooth-Profil (1–5) zugeordnet. Beim
Profilwechsel und nach einem Neustart wird dessen Auswahl wiederhergestellt.
Noch nicht konfigurierte Profile verwenden macOS.

Auf ausdrücklichen Wunsch gilt die Zuordnung **auch beim Tippen über USB**:
NUM + ESC ändert auch dann das aktuell ausgewählte Bluetooth-Profil. Es gibt
keine eigene USB-Auswahl und keine automatische Betriebssystemerkennung.
Ein Wechsel des Bluetooth-Profils ändert daher auch bei USB-Ausgabe die Ebene.

Die Auswahl wirkt sofort im RAM. Dauerhaft gespeichert wird im Hintergrund
nach der üblichen ZMK-Speicherverzögerung (29,5 Sekunden), nicht während aktiver
Eingabe. Bei laufender Eingabe wird der Speicherversuch erneut verschoben.
Nach dem Einstellen deshalb mindestens eine Minute ohne Eingaben eingeschaltet
lassen, bevor die Tastatur ausgeschaltet wird. Ein sofortiges Ausschalten kann
die letzte Auswahl verlieren. Profilwechsel und Wiederverbinden schreiben
selbst nichts in den Flash. Das Löschen einer Bluetooth-Kopplung lässt die
OS-Auswahl des Slots bestehen; beim neuen Gerät gegebenenfalls NUM + ESC nutzen.

Das getrennte Modul `src/adv360_profile_os.c` beobachtet ausschließlich
Profil- und Layerwechsel. Keymap, Resolver, Tap/Hold-Zeiten, USB-/Split-Patches,
NUM-Medienfunktionen und Entprellung bleiben unverändert. Die vorhandene
Diagnosefunktion bleibt wie im vorherigen Build standardmäßig inaktiv.

Die vollständige Portierungsvereinbarung und jede technische Abweichung stehen
in [`PORTING_NOTES.md`](PORTING_NOTES.md). Der eigene, eng begrenzte Resolver liegt in
[`src/behaviors/behavior_adv2_resolver.c`](src/behaviors/behavior_adv2_resolver.c).
Die davon getrennte Bluetooth-Steuerung liegt in
[`src/behaviors/behavior_adv360_bt_control.c`](src/behaviors/behavior_adv360_bt_control.c).

> `config/adv360.keymap` nicht durch einen generischen Keymap-Editor-Export
> ersetzen. Ein visueller Editor kann die kombinierte Resolver-Logik nicht
> verlustfrei darstellen.

## Firmware über GitHub Actions bauen

Jeder Push startet den Workflow **Build**. Nach erfolgreichem Lauf:

1. Den neuesten erfolgreichen Lauf unter **Actions → Build** öffnen.
2. Das Artefakt `firmware-profile-os` herunterladen.
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
