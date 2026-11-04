Pocket GPU state v5 — richiede v4 già funzionante

Scopo
La v4 elimina circa il 98% delle letture complete nei campioni del villaggio,
ma ricostruisce ancora la chiave di stato (51 registri più hash texture) per
migliaia di piccoli comandi di disegno. La v5 aggiunge un contatore nel GE che
cambia solo quando cambia uno dei registri consumati dal backend.
Se contatore di stato e generazione cache sono invariati, il backend riusa
lo stato senza ricostruire descrittori o interrogare la mappa delle texture.
La modalità strict-textures.flag esclude questo percorso rapido.

Installazione
Estrarre pocket-gpu-state dentro ~/mhfu-vita. Da ~/mhfu-vita:
  python3 pocket-gpu-state/apply_state.py
Se conferma l'applicazione:
  cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Copiare e reinstallare il VPK con VitaShell.
Non occorre rigenerare il gioco o ricompilare volontariamente tutti i suoi sorgenti.

Test
Raggiungere la stessa zona/inquadratura del villaggio per 20–30 secondi.
Inviare boot.log e FPS osservati. Marcatori attesi:
  native rasterization test v5-state-generation
  [gpu-validation] ... fast_states=...
fast_states è un contatore reale per periodo dei comandi che hanno saltato
la preparazione dello stato. Non equivale a fotogrammi né FPS.
Le misure gpu-cost-est restano stime campionate, non timestamp hardware.
Non è garantito un particolare guadagno di FPS.

Correttezza e limiti
Non cambia ordine delle primitive, risoluzione, shader, colori o geometria.
I vertici vengono comunque decodificati/trasformati dal GE. Matrici e indirizzi
dei vertici non fanno avanzare il contatore perché il backend riceve vertici
già trasformati. Non è una cache dei vertici e non salta il loro calcolo.
Qualunque cambiamento dei 51 registri di rasterizzazione fa avanzare il contatore.
Flush/sync/cache texture e CLUT continuano a invalidare tramite la generazione
separata della v4. Fine lista, stallo, copie e presentazione invalidano lo stato.
La modalità strict controlla ancora le texture prima di ogni draw.
Restano gli stessi limiti delle copie/readback GPU e della validazione v4:
scritture CPU a una texture nello stesso intervallo senza invalidazione PSP
possono apparire solo al confine successivo. Stencil/fog e altri stati ancora
non implementati non vengono resi fedeli da questa patch.
Salvataggi, input, audio pacing, stack da 2 MiB e limite di un'ora invariati.

Backup
 vita/runtime/vita_gpu.cpp.before-state-v5
 platforms/psp/gpu/ge.cpp.before-state-v5
 platforms/psp/gpu/ge.hpp.before-state-v5
Per annullare ripristinare tutti e tre e ricompilare/reinstallare il VPK.

Verifiche locali
- sintassi C++20 del backend con header vitaGL ufficiale e tipi SDK sostitutivi;
- sintassi ed esecuzione host del GE reale;
- corrispondenza esatta dei 51 registri della chiave e del contatore;
- ogni registro: scrittura diversa incrementa, scrittura identica non incrementa;
- matrici e indirizzi vertici non incrementano il contatore di rasterizzazione;
- test hash completo e invalidazioni texture precedenti ancora passati;
- installer e rifiuto di una seconda applicazione senza modifiche.
Non è stata effettuata una compilazione ARM o una prova della GPU fisica qui:
compilazione, collegamento, FPS e correttezza visuale da verificare sul PC/Vita.
