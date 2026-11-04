PocketProbe — stick destro come croce direzionale PSP

Estrarre pocket-right-stick dentro ~/mhfu-vita.
Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-right-stick/apply_right_stick.py

Solo dopo il messaggio "Stick destro configurato", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Aprire la cartella del VPK:
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell.

Stick destro: alto/basso/sinistra/destra = croce direzionale PSP;
sono consentite le diagonali. I pulsanti fisici e lo stick sinistro restano
attivi. La mappatura vale anche nei menu, come richiesto per l'equivalenza
con le frecce. Non cambia la velocità di ripetizione dei comandi del gioco.

Zona morta con isteresi: attivazione a <=80 o >=176, rilascio a 104/152.
Nel log compare una volta:
[input] right stick -> PSP D-pad; deadzone and hysteresis active

Modifica solo vita/runtime/vita_present.cpp, conservando il resto del file.
Backup: vita/runtime/vita_present.cpp.before-right-stick-v1.
Richiede l'attuale funzione pocket_update_input; rifiuta sorgenti inattesi.

Verificato su host con il corpo reale della funzione modificata e API simulate:
4 direzioni, diagonali, rilascio, isteresi, centro, pulsanti e stick sinistro.
Compilazione ARM e prova hardware restano da eseguire sul PC e sulla Vita.
Questa patch riguarda i comandi e non aumenta gli FPS.
