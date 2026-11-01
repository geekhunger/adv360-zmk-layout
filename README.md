# geekhunger's Advantage 360 Pro layout

ZMK-Portierung des langjährig verwendeten Advantage-2-QMK-Layouts aus
[`geekhunger/qmk_firmware`](https://github.com/geekhunger/qmk_firmware/tree/kint36/keyboards/kinesis/keymaps/herrsch).
Zielgerät ist eine **unveränderte Kinesis Advantage 360 Pro**. Auf dem Host
muss wie bisher das **deutsche Tastaturlayout** aktiv sein.

## Eigenschaften

- macOS-Grundebene, getrennte sparsame Windows- und Linux-Deltas und momentane NUM-Ebene
- Umschalten macOS → Windows → Linux → macOS durch `MOD`, dann `Esc tippen`
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

## Rechte Ctrl-Taste

Die kleine mittlere Taste im rechten Daumenblock, direkt über Cmd/Esc und
spiegelbildlich zur linken Ctrl-Taste, ist jetzt eine eigene rechte Ctrl-Taste
(Position 53; ursprünglich PgUp). Sie war im ursprünglichen QMK-Layout und im
bisherigen ZMK-Port unbelegt. Gehalten zusammen mit F/A sendet sie Ctrl+F/Ctrl+A.
Sie funktioniert unter macOS, Windows und mit gehaltener NUM-Taste.

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
- `Mod` kurz drücken und loslassen, dann innerhalb von 1,5 Sekunden eine der
  beiden **Cmd/Esc-Daumentasten** tippen: macOS → Windows → Linux → macOS.
  Der Wechsel wird für das aktive Profil gemerkt und MOD sofort verlassen.
  Die ursprüngliche äußere linke Position direkt neben A bleibt als Alias
  verfügbar. Ohne MOD behalten beide Daumentasten ihr normales Esc/Cmd-Verhalten.
  NUM an der ursprünglichen OS-Umschaltposition bleibt ohne Funktion.
- `Mod`, dann eine andere Taste: Bluetooth-Steuerung verwerfen und die Taste
  normal ausführen.
- `Mod` zweimal oder 1,5 Sekunden ohne Befehl: Bluetooth-Steuerung verwerfen.

Die Profilzahlen liegen absichtlich auf den physischen NUM-Positionen `1`–`5`;
die obere Zahlenreihe löst keine Bluetooth-Aktion aus.

### macOS-/Windows-/Linux-Auswahl je Bluetooth-Profil

`MOD`, dann eine der beiden `Esc-Daumentasten tippen` schaltet macOS → Windows → Linux → macOS um. Die Auswahl
wird jetzt dem aktuell ausgewählten Bluetooth-Profil (1–5) zugeordnet. Beim
Profilwechsel und nach einem Neustart wird dessen Auswahl wiederhergestellt.
Noch nicht konfigurierte Profile verwenden macOS.

Windows und Linux sind getrennte Ebenen mit identischen physischen Positionen.
Die Cmd-Daumentasten sind unter Linux Super und unter Windows Win. Bei
Textaktionen übersetzt die Firmware die Geste: Cmd+C/V/X/A/S/F usw. senden
Ctrl+C/V/X/A/S/F; Shift bleibt erhalten. Cmd+Shift+Z erzeugt unter Windows
Ctrl+Y und unter Linux Ctrl+Shift+Z. Cmd+Pfeile sowie Cmd+Backspace/Delete
übersetzen sich in die passenden Zeilen-/Dokumentaktionen. Cmd+Alt+Buchstaben
bleiben native Win-/Super+Alt-Desktopkombinationen. Die echte Ctrl-Taste bleibt
Ctrl; Linux hat auch links Ctrl an der entsprechenden Basisposition.
Linux verwendet deutsches XKB/Compose statt Windows-Alt-Zahlencodes. Die Alt-
Daumentasten senden unter Linux normales Alt; AltGr wird nur zur Zeichenausgabe
gezielt erzeugt. Alt+Shift+Links/Rechts markiert auf beiden PC-Systemen wortweise.

Layer-Anzeige bei wacher Tastatur: macOS aus, Windows weiß, Linux violett,
NUM blau, MOD grün. Die höhere NUM-/MOD-Ebene überdeckt die OS-Anzeige.
Die bestehenden gespeicherten Werte 0=macOS und 1=Windows bleiben gültig;
Linux ergänzt Wert 2. Bestehende Profilzuordnungen werden nicht zurückgesetzt.

#### Linux-Desktop-Einrichtung

Geprüfte Zielsysteme: Ubuntu 24.04/GNOME/X11 (`nas`) und Omarchy 4.0.1rc1/
Hyprland (`omarchy`), jeweils mit deutschem Layout `de` ohne Variante.
`tools/setup_linux_host.py gnome --apply` bzw. `omarchy --apply` richtet als
Sitzungsbenutzer die erforderlichen Ergänzungen ein. Ohne `--apply` zeigt es
nur eine Vorschau. Vorhandene Dateien und GSettings werden unter
`~/.local/state/adv360-linux/<Zeitstempel>/` gesichert.

- Menu wird zusätzlich Compose-Taste; die Firmware nutzt sie intern für
  `π`, `‰` und `¨`. Die einzige eigene Compose-Sequenz ist Menu, p, i → π.
- Akzent-Holds schließen tote Tasten ab, damit `^`, `´` und Backtick unmittelbar
  als Zeichen erscheinen und nicht erst den nächsten Buchstaben verändern.
- Physisches Cmd+H sendet F20: GNOME minimiert das Fenster; Omarchy verschiebt
  es ins vorhandene Scratchpad (Cmd+Alt+H zeigt es wieder an).
- Physisches Cmd+K halten sendet F21: das aktive Fenster schließen.
- Cmd+Space öffnet unter GNOME die Anwendungsübersicht, unter Omarchy den
  vorhandenen Launcher und unter Windows die Suche. Normales Space und
  NUM+Space bleiben unverändert.
- Physisches Alt+Cmd+Esc öffnet GNOME-Systemmonitor bzw. btop auf Omarchy.
- Omarchys Fcitx-Latinquelle wird an das bereits aktive deutsche XKB-Layout
  angeglichen. Alle vorhandenen Omarchy-Compose-Erweiterungen bleiben erhalten.

Textprogramme nach der Einrichtung neu öffnen, damit sie die Compose-Tabelle
neu laden. Programme, die Compose/Dead Keys oder Desktop-Shortcuts selbst
abfangen (z. B. Remote-Sitzungen, Spiele oder einzelne Terminals), benötigen
gegebenenfalls eigene Anpassungen. 34 Symbolfälle sind mit den installierten
XKB-/Compose-Bibliotheken beider Hosts offline geprüft; das ersetzt keinen
Test in den konkret verwendeten Anwendungen mit der geflashten Tastatur.

Auf ausdrücklichen Wunsch gilt die Zuordnung **auch beim Tippen über USB**:
MOD → ESC ändert auch dann das aktuell ausgewählte Bluetooth-Profil. Es gibt
keine eigene USB-Auswahl und keine automatische Betriebssystemerkennung.
Ein Wechsel des Bluetooth-Profils ändert daher auch bei USB-Ausgabe die Ebene.

Die Auswahl wirkt sofort im RAM. Dauerhaft gespeichert wird im Hintergrund
nach der üblichen ZMK-Speicherverzögerung (29,5 Sekunden), nicht während aktiver
Eingabe. Bei laufender Eingabe wird der Speicherversuch erneut verschoben.
Nach dem Einstellen deshalb mindestens eine Minute ohne Eingaben eingeschaltet
lassen, bevor die Tastatur ausgeschaltet wird. Ein sofortiges Ausschalten kann
die letzte Auswahl verlieren. Profilwechsel und Wiederverbinden schreiben
selbst nichts in den Flash. Das Löschen einer Bluetooth-Kopplung lässt die
OS-Auswahl des Slots bestehen; beim neuen Gerät gegebenenfalls MOD → ESC nutzen.

Das getrennte Modul `src/adv360_profile_os.c` beobachtet ausschließlich
Profil- und Layerwechsel. Die OS-Auswahl wird über MOD gewechselt. Linux ergänzt ausschließlich
OS-spezifische Ausgaben und die gemeinsame Ebenennummerierung; Windows ergänzt
die Cmd-Aktionsübersetzung. Modifier-Entscheidungen, Tap/Hold-Zeiten,
Ereigniswarteschlangen, USB-/Split-Patches, NUM-Medienfunktionen und Entprellung
bleiben unverändert. Kurze Ctrl-Schutzereignisse um die Win-/Super-Übersetzung
verhindern, dass ein Modifierwechsel als alleiniger Start-/Overview-Aufruf gilt. Die vorhandene
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
2. Das Artefakt `firmware-mod-os` herunterladen.
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
