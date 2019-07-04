# TonUINO - Woody
Die DIY Musikbox (nicht nur) für Kinder - Mobile LED Edition

# Special
  - mobile
    - all HALT
    - switch on by right-Button / switch off by MID-Long+Left-Short
    - timer ..
  - LED-Ring
    - different style for default, play, setup
  - Playmode
    - FREE_MODE - access every folder, track w/o tag
    - shuffle with previous-track 
    - tag continous detection - stop playing when tag is removed
    - delay Volume-up 
    - standbyTimer - stop if not playing for a while
    - sleepTimer - to stop if playing
  menue
    - clustered options (volume menu for min+init+max setting)
    - von-bis track selection as generial option
    - shortcut / 3vs.5-Button / Navigation-vs.-Vol-Switch
    - shortcut - button click to play (if not playing) folder (use settings-mapping)
  admin
    - data migration to current version
  prog
    - tighten algorithm
    - shortcut (simulate tag and so have all functions as tags)
    - checkTimer can handle retryCount




# Change Log

## Version 2.01 (01.11.2018)
- kleiner Fix um die Probleme beim Anlernen von Karten zu reduzieren

## Version 2.0 (26.08.2018)

- Lautstärke wird nun über einen langen Tastendruck geändert
- bei kurzem Tastendruck wird der nächste / vorherige Track abgespielt (je nach Wiedergabemodus nicht verfügbar)
- Während der Wiedergabe wird bei langem Tastendruck auf Play/Pause die Nummer des aktuellen Tracks angesagt
- Neuer Wiedergabemodus: **Einzelmodus**
  Eine Karte kann mit einer einzelnen Datei aus einem Ordner verknüpft werden. Dadurch sind theoretisch 25000 verschiedene Karten für je eine Datei möglich
- Neuer Wiedergabemodus: **Hörbuch-Modus**
  Funktioniert genau wie der Album-Modus. Zusätzlich wir der Fortschritt im EEPROM des Arduinos gespeichert und beim nächsten mal wird bei der jeweils letzten Datei neu gestartet. Leider kann nur der Track, nicht die Stelle im Track gespeichert werden
- Um mehr als 100 Karten zu unterstützen wird die Konfiguration der Karten nicht mehr im EEPROM gespeichert sondern direkt auf den Karten - die Karte muss daher beim Anlernen aufgelegt bleiben!
- Durch einen langen Druck auf Play/Pause kann **eine Karte neu konfiguriert** werden
- In den Auswahldialogen kann durch langen Druck auf die Lautstärketasten jeweils um 10 Ordner oder Dateien vor und zurück gesprungen werden
- Reset des MP3 Moduls beim Start entfernt - war nicht nötig und hat "Krach" gemacht
