PocketProbe GE vertices v6 — patch successiva alla v5

Modifica solo platforms/psp/gpu/ge.cpp e ge.hpp. Richiede v5 e fix stack GE.
Conserva vitaGL, cache texture, salvataggi e durata test già configurati.

Il buffer temporaneo dei vertici viene riutilizzato tra i disegni. Il layout
VTYPE viene calcolato quando cambia il formato, anziché per ogni vertice.
I dati dei vertici, i pesi, il morphing e le trasformazioni vengono ancora letti
ed eseguiti ad ogni disegno. Il buffer mantiene la capacità del disegno più grande.

Aggiunge misure campionate di lettura/trasformazione e assemblaggio/disegno:
1 comando ogni 64, estrapolato. Sono stime CPU e possono oscillare; non sono
tempi hardware GPU. La riga [perf] vertices/primitives diventa informativa.
La misura non comprende tutta la preparazione fatta in draw_started.

Installazione, nel terminale Ubuntu dalla cartella ~/mhfu-vita:
python3 pocket-ge-vertices/apply_vertices.py

Solo se appare "V6 applicata", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Reinstallare il nuovo pocket-game-boot.vpk con VitaShell. Nel boot.log deve
apparire: [ge] vertex reuse v6 active; vertex timers estimated 1/64 draws
Provare casa e villaggio per almeno 20 secondi ciascuno, mantenendo gli stessi
clock, quindi inviare boot.log prima del successivo avvio.

Backup creati: ge.cpp.before-vertices-v6 e ge.hpp.before-vertices-v6.
Lo script rifiuta sorgenti inattesi e una seconda applicazione.

Verifiche sul PC di sviluppo: sintassi C++ GE con ramo Vita e backend v5;
confronto prima/dopo su 1440 comandi, 18360 vertici prodotti (formati variabili,
indicizzazione, morphing, triangoli/strisce/ventagli/sprite): hash identico.
Regressioni registri e hash texture/palette. Non compilato ARM né provato su Vita
in questo ambiente. Nessun aumento di FPS è ancora misurato per questa patch.
