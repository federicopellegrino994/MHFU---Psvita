Pocket GPU performance v3 — XXH3 e batching degli stati

Richiede GPU profile v2 e correzione stack GE a 2 MiB già applicate.
Basata sulle misure ricevute nella casa/villaggio: circa 103 ms/fotogramma di
hash texture, 79 ms di invio draw, circa 1900 draw/fotogramma e nessun cache miss
nei campioni stabili. Nessuna garanzia di un determinato numero di FPS.

Installazione: estrarre pocket-gpu-performance dentro ~/mhfu-vita, poi:
  cd ~/mhfu-vita
  python3 pocket-gpu-performance/apply_performance.py
Se conferma l'applicazione:
  cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Reinstallare il VPK con VitaShell. Non occorre rigenerare il gioco.

Cambiamenti
1. Ge::texture_hash usa XXH3 header-only già incluso nel progetto al posto della
   catena FNV con moltiplicazioni a 64 bit. Legge ancora TUTTI i byte della texture
   prima di ogni comando di disegno guest che la usa, con formato, dimensioni,
   indirizzo, stride, swizzling e l'intera palette per i formati indicizzati.
   Non salta controlli e non introduce scadenze temporali della cache.
2. I vertici di comandi consecutivi con stato equivalente vengono raggruppati.
   Il confronto include registri di disegno e impronta texture. Prima di cambiare
   target/stato, caricare o eliminare texture, le primitive precedenti vengono
   inviate con il loro stato. Nessun riordino delle primitive.
   Copie, fine lista e presentazione svuotano il batch e invalidano lo stato.
3. I contatori v2 restano presenti, con reused_states per periodo.

Misure da confrontare
- hash, submit e setup_total in gpu-cost;
- draws e reused_states in gpu-count;
- FPS in gpu presents nella STESSA zona e con la stessa inquadratura.
setup_total comprende hash/decode/upload, ma esclude l'invio del batch precedente.
Non sommare le sue sottovoci a setup_total. I tempi sono misure host, non timer
hardware della GPU. Numero di triangoli e carico 3D possono cambiare muovendosi.

Il tempo di un'ora, lo stack, i salvataggi, input, audio pacing e fallback non cambiano.
Risoluzione PSP invariata. Restano i limiti di stencil, fog, copie GPU/readback
ed effetti già documentati nella v1/v2. Non è un renderer PSP completo.

Backup creati:
 vita/runtime/vita_gpu.cpp.before-performance-v3
 platforms/psp/gpu/ge.cpp.before-performance-v3
Per annullare ripristinare entrambi e ricompilare il VPK. Non riapplicare poi
l'installer senza prima gestire questi backup: non vengono sovrascritti.

Verifiche locali
- sintassi C++20 del backend con header vitaGL ufficiale e tipi SDK sostitutivi;
- sintassi GE con l'header xxHash del progetto;
- esecuzione del GE reale con memoria guest: modifica di ciascuno dei 256 byte
  di una texture 8x8 RGBA e ciascuno dei 1024 byte CLUT rilevata; controlli di
  stabilità dell'hash, swizzle e formato palette;
- applicazione installer e rifiuto della seconda applicazione senza modifiche.
Compilazione ARM, correttezza visuale e prestazioni hardware da verificare sulla Vita.
Non contiene dati del gioco o librerie di terzi da installare.
