PocketProbe v13 - geometria preparata riutilizzabile e batch VBO persistenti

Richiede vita_gpu.cpp della v12. Sorgenti sperimentali, non un VPK.
Compilazione ARM, collegamento, immagine finale e FPS su Vita NON verificati qui.

INSTALLAZIONE
Estrarre resident-geometry-v13 dentro ~/mhfu-vita. Da Ubuntu:

cd ~/mhfu-vita
python3 resident-geometry-v13/apply_resident.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Aspettare le righe di creazione VPK e il ritorno al prompt senza errori.
Reinstallare pocket-game-boot.vpk con VitaShell.
L'installer controlla gli hash prima di scrivere. Se presenti, rimuove dai
sorgenti le diagnostiche model-capture-v1 e model-entry-probe-v1, solo dopo
averne verificato tutti i backup. Non serve rimuoverle a mano. File diversi
o modificati fermano tutto prima di scrivere: inviare il messaggio ricevuto.

COSA CAMBIA
Il backend riconosce geometrie rigide 00091A/00111A già preparate, conservando
vertici nativi e triangoli. Confronta esattamente i byte di vertici e indici:
riutilizzare un indirizzo RAM/VRAM non riutilizza automaticamente il modello.
Anche cambiamenti di colori, UV, topologia, shading flat e scala UV invalidano
il pacchetto corrispondente.

Un box conservativo sostituisce i controlli di trasformazione per vertice
quando l'intero box è sicuramente sicuro. Un box incerto NON elimina il
modello: rimanda ai controlli e al clipping precedenti. Il risultato del
controllo è riutilizzabile solo con identiche matrici e viewport.

Le sequenze ordinate di pacchetti vengono unite nei batch e caricate in VBO.
Alle ricorrenze della stessa sequenza si riusa il VBO già caricato; camera e
matrice vengono aggiornate separatamente. Non viene introdotta una draw call
per ogni striscia PSP. Cambi di materiale/matrice e passaggi al percorso
precedente possono dividere i batch. Un caricamento VBO fallito torna al
batch in memoria CPU. Budget nominali: pacchetti CPU 4 MiB, VBO 8 MiB,
più metadati e batch temporanei. Tutte le chiamate GL restano sul GE worker.

LIMITE DI QUESTA INTEGRAZIONE
Questa è una prima integrazione nel renderer dei pacchetti riutilizzabili.
NON sostituisce ancora pmo::drawMesh e NON salta l'interpretazione dei comandi
GE. Non legge PMO nuovi tramite ModKit e non converte automaticamente i
modelli animati. La sostituzione completa del produttore PMO è un lavoro
ulteriore. Il risparmio qui è nella preparazione e nel caricamento ripetuti,
con il costo residuo dei confronti di memoria. Potrebbe non migliorare gli
FPS se questi costi o altre parti dominano: non sono promessi 30/60 FPS.

PROVA CONFRONTABILE
Nel boot.log deve comparire: [gpu-resident] v13 enabled.
Attendere qualche secondo, poi restare circa 30 secondi nella stessa
posizione e con la stessa visuale del villaggio. Ripetere in quest, con lo
stesso overclock dei test precedenti. Controllare anche colori e geometria.
Conservare boot.log prima del prossimo avvio.

Le righe [gpu-resident] period riportano:
packet_hits/builds/changed: riuso, preparazione o cambiamenti della geometria;
bounds_safe/bounds_fallback: uso del box o ritorno ai controlli precedenti;
vbo_hits/uploads/draws: riuso dei batch residenti, caricamenti e invii;
upload_errors: errori di allocazione/caricamento;
cpu_bytes/vbo_bytes: dati conservati nei budget principali.
I contatori non dimostrano un aumento FPS: confrontare FPS e tempo totale
delle liste, mantenendo la stessa scena.

CONFRONTO SENZA RICOMPILARE
Creare un file vuoto ux0:data/pocketprobe/no-resident-geometry.flag.
Riavviare: il log riporterà disabled by flag e tornerà il percorso v12.
Provare lo stesso punto e salvare anche quel log. Rimuovere la flag e
riavviare per riattivare v13. Se gli FPS restano uguali o peggiorano, lasciare
la flag e confrontare i log prima di aggiungere altre ottimizzazioni.

RIPRISTINO DEI SORGENTI
cd ~/mhfu-vita
python3 resident-geometry-v13/restore_resident.py

Ricompilare e reinstallare. Il ripristino ricrea esattamente anche le eventuali
diagnostiche presenti prima dell'installazione. Rifiuta sorgenti modificati
successivamente. I backup sono in build/pocket-resident-v13-backup.

VERIFICHE HOST ESEGUITE
Output prima/dopo identico byte per byte per i 32 comandi della cattura,
ripetuti due volte: 630 vertici triangolari inviati al rasterizzatore mock.
27 VBO caricati al primo passaggio; nessun nuovo upload al secondo.
Confronto dopo assemblaggio/clipping del backend esistente, prima della
rasterizzazione effettiva: non è una verifica di immagine sulla GPU Vita.
Regressione U8/U16, strip/fan/triangoli, flat shading, RTT UV, cambi matrice,
memoria riscritta, ownership dei pacchetti in coda, fallimento upload,
1746 disegni eliminati confrontati col GE reale, fallback near/depth.
6110 box accettati, 611000 punti interni coerenti col controllo conservativo.
AddressSanitizer/UndefinedBehaviorSanitizer sul backend simulato: passati
(rilevamento leak disabilitato). Installazione e ripristino verificati sulla
catena completa probe -> capture -> v13, inclusi backup e sorgenti modificati.

Nessun asset, ISO, salvataggio o dato della cattura incluso.
