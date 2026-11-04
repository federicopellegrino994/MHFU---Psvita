Pocket GPU v2: profilazione e combinatore texture

Richiede la v1 GPU già funzionante con pool immediate-mode da 1 MiB.
Estrarre pocket-gpu-profile dentro ~/mhfu-vita, quindi:
  cd ~/mhfu-vita
  python3 pocket-gpu-profile/apply_profile.py
  cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Installare il nuovo VPK con VitaShell. Non occorre rigenerare i sorgenti del gioco.

Il cambio mantiene timer di un'ora, controlli, salvataggi, audio pacing e fallback.
Modifica solo vita/runtime/vita_gpu.cpp e ne salva una copia .before-profile-v2.

Le righe gpu-cost riportano millisecondi per fotogramma presentato nel periodo:
setup_total include hash, decode, upload (NON sommare questi ultimi a setup_total).
submit misura le chiamate CPU di invio draw, non il tempo di esecuzione GPU puro.
flush e swap misurano il tempo host trascorso in quelle chiamate, inclusi eventuali wait.
Questi contatori non sono timestamp hardware GPU e non misurano il carico totale dei core.

Le righe gpu-count riportano le richieste texture, hit/miss, draw e triangoli per periodo.
I target e le texture in cache sono ora stampati con %u, compatibile con il logger Vita.

Corregge le formule texture RGB/RGBA rispetto al renderer software, compreso il
mantenimento dell'alpha del vertice nei modi RGB e decal e il raddoppio RGB.
Non risolve automaticamente tutti i difetti dei testi. Restano i limiti v1:
readback/copie dalla GPU, stencil, fog e altri stati non implementati fedelmente.
Non contiene un'ottimizzazione della cache, non cambia la risoluzione del gioco,
non aggiunge audio e non promette un miglioramento degli FPS.

Validazione locale: sintassi C++20 senza warning con header vitaGL ufficiale e
tipi SDK sostitutivi; 20 combinazioni degli stati texture confrontate con le
formule di riferimento RGB/alpha; installer controllato sulla v1 con pool corretto.
Compilazione ARM, collegamento e test sulla console restano da eseguire sul PC/Vita.

Inviare boot.log della stessa prova del video: menu, caricamento e 20 secondi
nella casa o nel villaggio bastano per confrontare le misure.
Per tornare al software: file vuoto ux0:data/pocketprobe/software.flag, poi riavvio app.
