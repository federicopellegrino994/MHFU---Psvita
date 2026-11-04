PocketProbe - PMO live v1 (ULES01213 1.01)

COLLEGAMENTO IMPLEMENTATO NEI SORGENTI
La funzione generata EU pmo::drawMesh 0886171C chiama il produttore C++.
Il produttore scrive gli stessi comandi e aggiorna lo stesso cursore guest.
Comunica al GE worker soltanto metadati protetti da mutex: nessuna chiamata
GL sul thread del gioco. Il GE riconosce i gruppi rigidi registrati e ne
riusa una decodifica da snapshot immutabile, verificato byte per byte.

All'interno dei gruppi accettati il GE salta il ciclo per piccoli PRIM.
Il backend Vita usa direttamente la geometria preparata con trasformazione
GPU quando il box o tutti i vertici distinti risultano sicuramente compatibili
col clipping PSP. Gruppi interamente fuori vista sono scartati con la stessa
regola conservativa della v9. Nei casi misti/incerti il GE trasforma una volta
ogni vertice distinto e usa il clipping originale. Stati con illuminazione, formati animati o liste non riconosciute
continuano sul percorso precedente. Non è la sostituzione di tutto il GE.

La v13 e i suoi VBO vengono rimossi dal renderer. Qui si usa il batching
esistente, senza introdurre una cache di sequenze VBO per piccoli disegni.
La geometria preparata ha un budget di 8 MiB (capacità dei vettori inclusa),
più metadati e scratch CPU riutilizzabile. A saturazione si mantengono i
vecchi gruppi e si negano nuove ammissioni: nessun ciclo di espulsione e
ricostruzione. I gruppi ripetutamente riscritti tornano al percorso normale.
In caso di allocazione fallita la cache opzionale viene liberata e disattivata.

Questo archivio contiene sorgenti. Non è un VPK precompilato.
Build ARM, avvio e FPS su Vita restano da verificare sul dispositivo.

INSTALLAZIONE
Estrarre la cartella pmo-live-v1 dentro ~/mhfu-vita. Da Ubuntu:

cd ~/mhfu-vita
python3 pmo-live-v1/apply_live.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

L'installer accetta solo i sorgenti v12 o v13 verificati e l'ELF EU noto.
Controlla tutti i file prima di scrivere, modifica una sola funzione generata
e conserva backup completi in build/pocket-pmo-live-v1-backup.
Rifiuta versioni sconosciute, diagnostiche ancora presenti o applicazioni
ripetute. Se si ferma, inviare il messaggio; non forzare gli hash.
Non serve copiare a mano i file dentro payload.

Attendere il completamento senza errori di compilazione, collegamento,
conversione SELF e creazione VPK. Reinstallare pocket-game-boot.vpk con
VitaShell: copiarlo sulla scheda non aggiorna da solo l'app installata.
La flag no-resident-geometry.flag della v13 può rimanere sulla Vita.

PROVA
Nel log devono comparire:
[pmo-native] live-v1 enabled; GPU groups enabled
[pmo-native] producer calls=...
[pmo-native-ge] totals: ...

Attendere circa 10 secondi nel villaggio per la prima preparazione. Poi
restare 30 secondi nella stessa posizione e con la stessa visuale dei test
precedenti, mantenendo lo stesso overclock. Ripetere in quest se possibile.
Inviare boot.log completo e FPS. Controllare anche geometria, colori,
texture e comportamento nei menu. I salvataggi non sono modificati dalla patch.

I totali PMO riportano: producer/fallback sul thread del gioco; groups,
hits/builds/changed/denied/unsupported per la geometria; gpu/cpu per il
percorso scelto; bytes per la cache. I numeri dimostrano l'uso del percorso,
non un aumento FPS. I timer fini render-v10 non coprono tutti i nuovi passi:
confrontare FPS e tempo totale delle liste. Un modello non coincide con un
singolo gruppo e il batching esistente già unisce molti triangoli.

DISATTIVAZIONE SENZA RICOMPILARE
Creare il file vuoto ux0:data/pocketprobe/no-native-pmo.flag e riavviare l'app.
Disattiva sia il produttore sia il trattamento dei gruppi: torna il percorso
v12. Per creare il file sul PC da Ubuntu:

cd ~/mhfu-vita
touch build/no-native-pmo.flag
explorer.exe "$(wslpath -w "$PWD/build")"

Copiare quella flag in ux0:data/pocketprobe/. Rimuoverla e riavviare per
riattivare PMO live. Salvare ciascun boot.log prima dell'avvio successivo.
Per sviluppo soltanto: no-pmo-group-gpu.flag forza il clipping CPU dei gruppi,
lasciando attivi il produttore e la decodifica condivisa.

RIPRISTINO DEI SORGENTI
cd ~/mhfu-vita
python3 pmo-live-v1/restore_live.py

Ricompilare e reinstallare. Il ripristino controlla gli hash e ricrea anche
la v13 se era presente prima (la sua flag disattivante resta disponibile).
Rifiuta modifiche successive. Ripristinare PMO live prima di eventuali
ripristini di patch precedenti.

VERIFICHE HOST
- Produttore offline: 265 casi confrontati con la routine MIPS EU originale.
- Pipeline collegata: 264 confronti mesh/pose, 135012 triangoli; attributi
  prima e dopo clipping, sorgenti dei vertici, tutti i 256 registri GE e
  contatori comandi/primitive/vertici identici al percorso originale.
- Callback del vero VitaGpu con GL simulato: 264 confronti, 166737 vertici;
  topologia/colori/UV identici, posizioni entro arrotondamento conservativo
  (errore assoluto massimo misurato 0.000000417). Sia GPU sia fallback CPU
  usati; non è una verifica dell'immagine rasterizzata sulla GPU Vita.
- 79 gruppi reali compilati; nel test renderer 869 riusi, 79 preparazioni,
  zero espulsioni; 390 gruppi disegnati, 555 scartati in modo conservativo,
  3 gruppi con clipping CPU. Cache circa 1.6 MiB.
- Riscritture della stessa VRAM e pause/riprese dentro un gruppo preservate.
- Saturazione negata prima di letture/allocazioni; fallimento allocazione
  gestito. Metadati coerenti con 50000 scritture su un thread concorrente.
- ASan/UBSan sul confronto GE collegato: passati (leak detection disabilitata).
- Installazione/ripristino su entrambe le basi, protezione sorgenti modificati;
  wrapper generato compilato ed eseguito su ramo nativo e fallback.

Nessun aumento FPS, 30/60 FPS o correttezza visiva su Vita dimostrati qui.
Nessuna ISO, asset, salvataggio o cattura inclusa.

CONTROLLI HOST FACOLTATIVI
Non sono necessari per il prossimo test: sono già stati eseguiti qui.
Dopo l'installazione nei sorgenti, dalla radice del progetto:

bash pmo-live-v1/run_checks.sh user_data/incoming/model-capture.bin

La fixture creata in build/pmo-live-host-check contiene dati derivati dalla
propria cattura: conservarla privatamente. Non è inclusa nell'archivio.
