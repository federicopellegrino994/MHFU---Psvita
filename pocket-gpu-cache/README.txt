Pocket GPU cache v4 — richiede v3 e stack GE da 2 MiB

Installazione
Estrarre pocket-gpu-cache dentro ~/mhfu-vita. Da ~/mhfu-vita:
  python3 pocket-gpu-cache/apply_cache.py
Se conferma l'applicazione:
  cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Copiare e reinstallare il VPK con VitaShell. Non rigenerare il gioco.
Test: stessa inquadratura nel villaggio per 20–30 secondi, poi inviare boot.log.

Motivazione
Nel nuovo log la v3 è attiva. Nella scena finale circa 7000 richieste texture
per fotogramma, nessuna decodifica nuova, ma 313–372 ms per fotogramma di hash.
La v3 ha ridotto i piccoli draw, ma continuava a controllare l'intero contenuto
di una texture prima di ogni comando guest. La scena finale è anche più pesante
della casa: non confrontare direttamente FPS di zone diverse.

Cosa cambia
La prima richiesta legge l'intera texture e ne calcola l'hash. Le richieste
identiche riutilizzano quell'hash fino a uno di questi confini/eventi:
- TEX_FLUSH (0xCB), TEX_SYNC (0xCC), LOAD_CLUT o avvio copia GE;
- cambiamento dei parametri texture/palette;
- fine o stallo della lista, presentazione o copia notificata al backend.
Vengono contati anche comandi ripetuti con argomento identico.
La mappa dei controlli contiene al massimo 512 descrittori per intervallo.

Questo cambia la politica di validazione: non è più una lettura completa a
ogni draw. Si basa sulle invalidazioni della cache texture PSP e aggiunge
confini conservativi. Scritture CPU nella stessa texture durante una lista,
senza un flush/sync/cambio palette o altro confine, possono non comparire fino
al confine successivo. I percorsi GPU readback/copie non ancora implementati
restano limitati come nella v1. Verificare soprattutto testo e texture dinamiche.
PSPSDK documenta sceGuTexFlush come svuotamento della texture page-cache dopo
una copia o rendering in quell'area:
https://pspdev.github.io/pspsdk/group__GU.html

La v4 riduce anche il costo delle misurazioni: setup, hash e invio draw sono
campionati una volta ogni 64 eventi e scalati. Le righe gpu-cost-est sono STIME,
non misure hardware né valori esatti per ogni fotogramma. I contatori sono reali.
La fase iniziale di compilazione shader può alterare le prime stime.

Log da controllare
  native rasterization test v4-cache-epochs
  texture validation: GE cache epochs + list boundaries
  [gpu-validation] period: full_checks=... reuses=... epochs=...
full_checks dovrebbe diminuire rispetto alle richieste se le invalidazioni
permettono riutilizzo. Se epochs è molto alto la cache potrebbe aiutare poco.
L'aumento effettivo degli FPS va misurato sulla Vita, non è garantito.

Confronto con validazione completa
Creare un file vuoto sulla Vita:
  ux0:data/pocketprobe/strict-textures.flag
Al successivo avvio la v4 controllerà di nuovo ogni draw. Il log indica:
  texture validation: strict every draw
Per tornare alla cache rimuovere il file e riavviare. Nessuna ricompilazione.
Il file software.flag continua invece a scegliere l'intero renderer software.

Restano invariati stack da 2 MiB, pool immediate-mode da 1 MiB, salvataggi,
input, audio pacing diagnostico, risoluzione PSP e limite test di un'ora.

Backup prima della patch
 vita/runtime/vita_gpu.cpp.before-cache-v4
 platforms/psp/gpu/ge.cpp.before-cache-v4
 platforms/psp/gpu/ge.hpp.before-cache-v4
Per annullare ripristinare tutti e tre e ricompilare/reinstallare il VPK.

Verifiche locali
- sintassi C++20 del backend e del GE; header vitaGL ufficiale con tipi SDK
  sostitutivi sul sistema host, non compilazione ARM;
- esecuzione del GE reale: invalidazione per flush ripetuti, sync, CLUT e copia;
- variazioni di ogni byte texture/palette ancora rilevate dall'hash completo;
- helper di validazione estratto dal nuovo sorgente: riuso per 1000 richieste,
  nuova lettura dopo invalidazione, descrittore, palette, fine lista e modalità strict;
- installer e protezione dalla riapplicazione, senza sovrascrivere backup.
Compilazione ARM, collegamento e test visivo/FPS restano da eseguire dall'utente.
