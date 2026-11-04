PocketProbe — percorso GPU rigido v8, prototipo sperimentale

Richiede v7, censimento renderer v1 e backend GPU v5 attualmente installati.
Non include codice o dati del gioco. Non è un VPK già compilato.

COSA CAMBIA
Per VTYPE 0x00111A (UV16, RGBA4444, posizione16, indici16), triangoli,
strisce e ventagli 3D senza illuminazione o skinning, la trasformazione finale
usa il vertex shader della pipeline vitaGL tramite la matrice di proiezione.
Il percorso entra PRIMA di Ge::decode_vertex/transform/DrawInfo e ne evita
l'esecuzione per i disegni accettati.

La CPU legge e copia indici, posizioni, UV e colori; prepara una matrice
combinata quando cambia lo stato di trasformazione; esegue inoltre un controllo
conservativo delle posizioni per near plane, guard band PSP e limiti profondità.
Questo controllo conserva lavoro CPU: NON è una pipeline interamente GPU.
I disegni rischiosi o non supportati usano il vecchio percorso completo.

Restano raggruppati i triangoli con stesso materiale, texture e matrice.
I cambi fra CPU/GPU o fra matrici diverse inviano prima i vertici già in coda.
Il vecchio percorso resta per i personaggi skinned e gli altri formati.
Il censimento dettagliato è disattivato in questa build, per evitare il costo
delle misure. Input, salvataggi e durata della sessione restano invariati.
Le limitazioni già esistenti (fog, stencil, trasferimenti/readback ecc.) restano.

INSTALLAZIONE
Estrarre pocket-rigid-gpu dentro ~/mhfu-vita. Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-rigid-gpu/apply_rigid_gpu.py

Solo dopo "Percorso GPU rigido v8 applicato", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Aprire la cartella del VPK:
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell.
Non occorre rigenerare le sorgenti del gioco o cambiare la ISO.

TEST
Provare casa, villaggio e la stessa area di quest agli stessi clock.
Muovere personaggio e visuale; controllare nuove deformazioni, triangoli estesi,
texture capovolte o elementi scomparsi. Inviare boot.log prima di riavviare.
Non è ancora stato misurato un aumento di FPS del gioco su Vita per questa patch.

Nel log:
[gpu-rigid] v8 enabled; VTYPE 00111A; ...
[gpu-rigid] period: attempts=... draws=... vertices=... fallback_state=...
fallback_memory=... fallback_clip=... batches=... matrix_splits=...

- draws/vertices: comandi e vertici gestiti dal nuovo percorso;
- fallback_state: modalità non supportata, conteggio fuori 3..4096, UV non finite;
- fallback_memory: intervallo non interamente mappato;
- fallback_clip: trasformazione invalida o controllo conservativo fallito;
- batches: invii GPU dei gruppi di geometria rigida; matrix_splits: separazioni
  di gruppi causate da una matrice diversa, non tutti i cambi di stato.
Le righe vitaGL v5 restano corrette: il percorso è aggiunto al backend v5.
I timer dettagliati [perf] dei vertici potranno restare a zero; non indicano
che il lavoro sia assente. Usare FPS e contatori [gpu-rigid].

CONFRONTO / DISATTIVAZIONE
Se ci sono problemi, copiare il file vuoto optional/no-rigid-gpu.flag sulla Vita
come ux0:data/pocketprobe/no-rigid-gpu.flag e riavviare l'app.
Questo disattiva il nuovo percorso senza ricompilare. Rimuoverlo per riattivarlo.
Permette anche il confronto abilitato/disabilitato nella stessa build, con
censimento spento in entrambi i casi. Non copiare il flag se vuoi attivare il test.

VERIFICHE SU HOST
- Sintassi del backend con header ufficiale vitaGL; sintassi GE con ramo Vita.
- 2400 vertici proiettati confrontati con il GE reale: tolleranze <0,025 pixel
  per XY e <0,03 unità per profondità nei casi di prova; non identità bit per bit.
- Tutti i 65536 colori RGBA4444; UV, letture non allineate, generazioni matrici,
  rifiuto clipping, avanzamento indirizzi indice nei percorsi accettato/fallback.
- Sorgente VitaGpu reale con API GL simulate: raggruppamento, cambio matrice,
  passaggi CPU/GPU, ripristino identità, nessun disegno parziale al fallback,
  colore piatto, UV di render target, limiti indirizzi e numero vertici.
- Test host con AddressSanitizer/UndefinedBehaviorSanitizer senza errori;
  LeakSanitizer disattivato per incompatibilità con ptrace dell'ambiente.
- Regressione vecchio percorso: 3840 comandi, 115200 vertici, hash identico.
- Installer: controllo preventivo, backup e rifiuto di riapplicazione/sorgenti
  inattesi senza modifiche.

Questi controlli NON eseguono un driver GPU reale. Compilazione ARM, precisione
shader e comportamento hardware vanno verificati sul PC/Vita dell'utente.
Nessuna previsione di FPS deriva da questi test.

FILE MODIFICATI
platforms/psp/gpu/ge.cpp, ge.hpp, backend.hpp, pocket_geometry_probe.hpp;
vita/runtime/vita_gpu.cpp; nuovo vita/runtime/rigid_gpu_math.hpp.
Backup dei file modificati con suffisso .before-rigid-v8.
