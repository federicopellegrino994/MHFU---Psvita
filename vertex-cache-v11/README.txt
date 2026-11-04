PocketProbe v11 - riutilizzo dei controlli geometrici tra disegni
Richiede v10 applicata sopra v9.

ESTRAZIONE E BUILD
Estrarre vertex-cache-v11 dentro ~/mhfu-vita, quindi da Ubuntu:

cd ~/mhfu-vita
python3 vertex-cache-v11/apply_cache.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell.

CAMBIAMENTO
Cache fissa da 4096 voci (circa 48 KiB più lo stato della trasformazione).
Memorizza solo il risultato dei controlli geometrici del formato 00111A.
Legge sempre i sei byte XYZ correnti, non usa l'indirizzo guest come prova
che i dati siano ancora validi. Una collisione hash viene verificata con
confronto esatto delle coordinate. Confronta tutti i valori clip/bound/
viewport usati dal controllo prima di riutilizzare la cache con una matrice
ricostruita. Colori e UV sono sempre ricalcolati dai dati correnti.
I disegni completamente esclusi e quelli inviati al fallback non vengono
più convertiti anche in vertici GPU prima di essere scartati.

TEST VITA
Mantenere lo stesso overclock. Confrontare FPS nello stesso punto e con la
stessa visuale nel villaggio e in quest, per almeno 30 secondi per scena.
Inviare boot.log completo prima di riavviare l'app.
La riga iniziale deve contenere [gpu-vertex-cache] v11 enabled.
Le righe periodiche hits/misses mostrano se il gioco riutilizza davvero i
risultati, ma non dimostrano da sole un miglioramento FPS.
I timer fini v10 restano stime influenzate dal costo dell'orologio: non
interpretarli come tempi esatti né sommare categorie annidate.

CONFRONTO FACOLTATIVO
Un file vuoto ux0:data/pocketprobe/no-vertex-cache.flag disabilita la cache
al successivo avvio. Rimane la separazione controllo-posizioni/conversione.
Questo permette un confronto della sola cache senza ricompilare.
Non serve per il primo test. Rimuovere il file e riavviare per riattivarla.

VERIFICHE E LIMITI
Test host su un milione di controlli: risultato identico alla classificazione
originale per tutte le quattro categorie, comprese mutazioni della memoria,
indirizzi disallineati, collisioni e cambi di matrici/viewport.
Test del backend con API GL simulate: batch, clipping/fallback, cambi di
matrice, colori e UV aggiornati, render-to-texture.
La cache riguarda solo il percorso rigido 00111A, non tutti i formati.
Non è stato misurato un guadagno FPS su Vita: la build ARM e la verifica
sull'hardware sono ancora necessarie. Non sono promessi 30 o 60 FPS.

BACKUP
L'installer salva vita/runtime/vita_gpu.cpp.before-vertex-cache-v11 e non
sovrascrive un backup precedente. Non modifica gli asset o i salvataggi.
Per tornare alla v10, PRIMA di applicare altre patch, copiare il backup
sopra vita/runtime/vita_gpu.cpp, eliminare rigid_visibility_cache.hpp,
ricompilare e reinstallare il VPK.
