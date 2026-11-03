# -*- coding: utf-8 -*-
"""The guide's words in German (set 'dlc3.de'), written natively (stage 2,
06.10.2026). Meaning: tools/guide_lang/en.py and tools/guide_brief.md. Check:
    py -3 tools/guide_check.py de tools/guide_lang/de.py --budget
"""

TEXT = dict(
    titles=['LAUFEN UND SCHIESSEN', 'STICK-TYPEN', 'TOUCH-MODI', 'DOPPELTIPPEN', 'TASTEN', 'SPEZIALTASTEN',
            'KARTE UND ITEMS', 'ITEM-BESCHREIBUNGEN', 'EIGENES LAYOUT', 'GAMEPAD & TASTATUR'],
    move_shoot=['LAUFEN', 'SCHIESSEN'],
    types=['BLEIBT AN SEINEM PLATZ', 'SPRINGT ZUM DAUMEN', 'FOLGT DEM DAUMEN'],
    modes=['STUFENLOSES TEMPO', 'STOPP ODER VOLLGAS', 'D-PADS STATT STICKS'],
    dtap=['zweimal auf eine Seite tippen', 'tippen, dann tippen und wischen', 'wie bei DYNAMISCH',
          'zweimal auf einen Pfeil tippen'],
    buttons=['TIPP: PAUSE', 'HALTEN: NEUSTART', 'TIPP: BOMBE', 'TIPP: NUTZEN', 'TIPP: NUTZEN',
             'HALTEN: ABLEGEN', 'HALTEN: ABLEGEN'],
    special=[('ZWILLINGE:', 'ANDEREN ANHALTEN'), ('MARKIERT:', 'FEUER STOPPEN'), ('2. ITEM:', 'WECHSELN'),
             ('W UNENDLICH:', 'HALTEN: WÜRFEL')],
    map=[('TIPPEN', 'große Karte bleibt offen'), ('HALTEN', 'zeigt sie, solange du hältst'),
         ('LÄNGER', 'deine Items mit Beschreibung'), ('WISCHEN', 'mit zweitem Finger scrollen')],
    desc=[['LAUF ZU EINEM ITEM', 'UND SIEH, WAS ES TUT'],
          ['TASCHE HOCHHALTEN', 'UND REZEPTE SEHEN', 'OBERE HÄLFTE:', 'WISCHEN: BLÄTTERN', 'HALTEN: SPERREN']],
    pad=['halten: Inventar (wenn EID an)', 'Rezepte, Beschreibungen: weiter',
         'zurück; halten: Rezepte sperren', 'alle vier halten: Neustart', 'halten: Inventar (wenn EID an)',
         'Tasche: blättern, links sperrt', 'Pause', 'halten: Neustart'],
    shoot_key='TAB + SCHIESSEN',
    tweaks=['feuert nach Loslassen weiter', 'wie weit, bis du losläufst',
            ['wie weit für Vollgas', 'nur bei ANALOG'], 'wie weit, bis du schießt',
            ['schärferes Bild oder', 'flüssigeres Spiel'], 'Stärke: jetzt in Optionen'],
    aimlock_release='stoppen: Mitte kurz antippen',
    aimlock_note="ZIELSPERRE gibt's nur für Touch. Stoppen: Schuss-Stick oder Steuerkreuz kurz mittig "
                 'antippen, nicht wischen. Neuer Tipp sperrt.',
    tweaks2_title='WEITERE TWEAKS',
    tweaks2=['Spawner im Pausenmenü', ['an: Runs mit Spawner', 'schalten trotzdem frei'],
             'öffnet den Ordner des Ports', ['Spielstände aus anderen', 'Versionen des Spiels'],
             'in einen Ordner kopieren'],
    tweaks_notes=[
        'Optionen > TWEAKS.',
        'SPIELSTÄNDE LADEN holt sie aus anderen Versionen des Spiels, gesicherte liest auch die '
        'PC-Version. Zum Spawner: die nächsten zwei Seiten.'],
    mods2_title='WEITERE MODS',
    mods=[['ein Q4-Item im Raum:', 'alle tanzen zur Musik'],
          ['Planetarium-Chance der', 'nächsten Etage, bei Werten'],
          ['zeigt die aktuelle Welle', 'im Boss Rush'],
          ['eine Lebensleiste pro Boss,', 'mit Symbol und Status'],
          ['fehlende Marken blass', 'auf dem Abschlusszettel'],
          ['zeigt Schadensfaktor und', 'Tränenlimit bei den Werten'],
          ['Nahkampf trifft auch nahe', 'Gegner bei viel Reichweite'],
          ['zeigt, was ein Item tut,', 'wenn du hingehst (EID)']],
    mods_notes=[
        'Hauptmenü > MODS: PC-Mods, für den Port nachgebaut, nichts zu installieren. Zeile antippen: '
        'an oder aus. Ein Pfeil öffnet die Einstellungen.',
        'REICHWEITEN-FIX: für Nahkampf mit Knochen, Schwertern, Axt und Handwerkstasche. Sprache der '
        'ITEM-BESCHREIBUNGEN: hinter ihrem Pfeil.'],
    user_key='MOD-NAME',
    user=[['eine oder mehrere .zip wählen;', 'PC-Mods werden umgewandelt'],
          ['kopiert die Spieldateien raus,', 'als Basis für eigene Mods'],
          ['tippen: an oder aus;', 'Pfeil: Seite des Mods']],
    page_title='MOD-SEITE',
    page=['Mod an oder aus', ['ändern zwei Mods eine Datei,', 'gewinnt der obere'],
          'speichert den Mod als .zip', 'zweimal tippen zum Löschen',
          'eigene Optionen des Mods', 'startet neu, damit es wirkt'],
    user_notes=[
        'Workshop-Mods vom PC laufen, wenn sie Bilder, Sounds oder Daten ändern; Lua-Skripte nicht. '
        'Mod schon drauf? Dann aktualisieren oder beide behalten.',
        'Roter Name: Der Mod wird nicht ganz genutzt. Rote Zeilen über dem Titel seiner Seite sagen, '
        'warum, oder mit welchem Mod er sich beißt. Antippen: Details.'],
    coop_title='LOKALER KOOP',
    coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
    coop=['auf freiem Pad 1,5 s: einsteigen', 'dasselbe auf der Tastatur', 'Charakter wählen',
          'bestätigen / abbrechen', 'bestätigen / abbrechen (Tastatur)'],
    coop_note='Zu Beginn eines Runs auf einem freien Pad {VIEW} und {MENU} anderthalb Sekunden halten, dann '
              'mit den Pfeilen einen Charakter wählen. Bis zu vier Spieler.',
    spawn_title='SPAWNER-STEUERUNG',
    spawn_tabs=['wählen, nochmal: nehmen', 'wählen, nochmal: spawnen', 'Raum: 1x tippen, Etage: 2x',
                'Zahl eingeben oder Pfeile'],
    spawn_rows=['Koop: wessen Werte du änderst', 'einstellen; Werte: Zeile oder {A}', 'nochmal tippen: Zahl eingeben',
                'macht deine Änderungen rückgängig', 'Tab wechseln', 'Tab wechseln (Tastatur)',
                'bestätigen / schließen'],
    spawn_notes=[
        'Unter TWEAKS > ITEM-SPAWN einschalten, im Run dann im Pausenmenü auf ITEM-SPAWN tippen. Ohne '
        'SPAWN-TROPHÄEN schaltet so ein Run nichts frei.',
        'ITEMS hat alles, auch Pickups und Beute. AUF DEN BODEN legt es neben dich, Pfeile: Anzahl. '
        'Suche nach Name oder Nummer. Werte gelten bis Run-Ende.'],
    notes=[
        'Linker Stick läuft, rechter schießt. Typ, Größe und Touch-Modus stellst du in den Optionen ein; '
        'die nächsten zwei Seiten erklären sie.',
        'Optionen > TYP. STATISCH bleibt, wo du ihn hinsetzt, DYNAMISCH springt dahin, wo dein Daumen '
        'landet, SCHWEBEND folgt deinem Daumen.',
        'Optionen > TOUCH MODE. ANALOG: je weiter, desto schneller. DIGITAL: Stillstand oder Vollgas ab '
        'der TOTZONE. TASTEN: Steuerkreuze statt Sticks.',
        'Doppeltippen in eine Richtung: Mars sprintet, Hämoptysis niest. Geht mit beiden Sticks; der '
        'zweite Tipp muss innerhalb einer halben Sekunde kommen.',
        'Pause halten startet den Run neu; vor dem Abdunkeln loslassen bricht ab. Dein Schmuckstück ist '
        'auch eine Taste: halten legt es ab, ohne Pille oder Karte.',
        'Runde Taste: hält den anderen still (Jakob und Esau, Strohmann), tauscht beim Vergessenen. '
        'Schulranzen: tippen. W unendlich: halten, Würfel wählen.',
        'Minikarte antippen: große Karte bleibt offen. Mit ITEM-BESCHREIBUNGEN länger halten: deine Items '
        'mit Beschreibung. Mit zweitem Finger scrollen.',
        'Lange Beschreibungen erscheinen seitenweise: oben über das Feld wischen oder {LS} / {RS} '
        'am Controller. Einstellungen: MODS > ITEM-BESCHREIBUNGEN.',
        'Optionen > Steuerung > Touch: zieh Sticks, Tasten und Pause, wohin du willst. DECKKRAFT: wie '
        'durchsichtig sie sind. RASTER richtet sie aus.',
        'Der Rest: Optionen > Steuerung. Jakob und Esau: {RT} halten, dann nutzt {LT} Jakobs Karte oder Pille, {RB} Esaus (Tastatur: Strg + Leertaste / Q).',
    ],
)
