PocketProbe — GE vertex pipeline v7

Richiede la v6. Modifica soltanto platforms/psp/gpu/ge.cpp e ge.hpp.
Conserva stick destro, backend vitaGL, salvataggi, timer test e sorgenti generati.
Non include codice o dati del gioco.

MODIFICHE
1. Il decoder verifica una volta se tutti i record del vertice e dei suoi morph
   rientrano in memoria contigua. In quel caso legge direttamente i componenti
   con memcpy per i dati non allineati. Se il blocco attraversa un limite o non
   è mappato, conserva il vecchio comportamento di lettura componente per componente.
2. Cache di 256 elementi per gli indici ripetuti, limitata a UN comando di disegno.
   Ogni nuovo comando cambia epoca: dati, animazioni, matrici e formati dei disegni
   precedenti non vengono riutilizzati. Le collisioni ricalcolano il vertice.
   La cache occupa circa 22 KiB aggiuntivi per GE.
3. Evita trasformazioni delle normali per skinning/world quando l'illuminazione
   è spenta; la generazione delle coordinate texture mantiene i dati originali.
4. I timer vertici campionano con una sequenza pseudo-casuale invece di scegliere
   sempre il comando numero 64. Restano stime, non tempi hardware GPU.
5. Nuovi contatori periodici [ge-vertices]: decoded, reused, indexed_draws.

INSTALLAZIONE
Estrarre pocket-ge-pipeline nella cartella ~/mhfu-vita.
Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-ge-pipeline/apply_pipeline.py

SOLO dopo "V7 applicata", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Aprire la cartella del VPK:
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk tramite VitaShell.
Il nuovo boot.log deve contenere:
[ge] vertex pipeline v7 active; vertex timers estimated 1/64 draws
La riga vitaGL può continuare a dire v5-state-generation: il backend non cambia.

TEST SULLA VITA
Mantenere gli stessi clock del precedente test. Provare casa, villaggio e
la stessa area della quest per circa 30 secondi ciascuno. Inviare boot.log
prima di riavviare l'app, indicando eventuali difetti grafici nuovi.
I contatori vengono controllati ogni 256 liste, quindi possono apparire con
ritardo. Se reused resta basso, il beneficio del riuso indici sarà limitato.

VERIFICHE SVOLTE SUL PC DI SVILUPPO
- Confronto v6/v7: 3844 comandi, 115212 vertici prodotti, hash bit per bit identico.
  Formati variabili, indici a 8/16/32 bit, collisioni, cambi dei dati tra disegni,
  morph, pesi, illuminazione, triangoli/strisce/ventagli/sprite, confini RAM/VRAM.
- Test dei registri stato e hash integrale texture/palette superati.
- Controllo sintassi del ramo Vita e compatibilità sorgente del backend v5.
- AddressSanitizer/UndefinedBehaviorSanitizer: nessun errore nel confronto esteso.
  LeakSanitizer escluso perché incompatibile con il tracciamento del processo
  in questo ambiente: non è stato eseguito un controllo delle perdite.
- Installer: verifica preventiva, backup, rifiuto senza modifiche di sorgenti
  inattesi e seconda applicazione.
- Benchmark sintetico x86, -O3 senza fast-math, mediana di tre prove:
  120 vertici per comando, indici ripetuti su 16 vertici: v6 390.92 ms / v7 165.87 ms;
  non indicizzato: v6 386.22 ms / v7 389.12 ms (sostanzialmente invariato).
  Questi tempi non rappresentano MHFU, ARM o gli FPS sulla Vita.

La compilazione ARM e il test hardware sono ancora da eseguire sul tuo PC/Vita.
Nessun aumento di FPS sul gioco è stato misurato per questa patch.
Backup: ge.cpp.before-pipeline-v7 e ge.hpp.before-pipeline-v7.
