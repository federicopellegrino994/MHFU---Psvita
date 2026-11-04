Pocket GPU bring-up v1 — versione sperimentale, sorgenti soltanto

Scopo
Spostare la rasterizzazione di triangoli e rettangoli dal software a vitaGL/GXM.
Non è una promessa di 30/60 FPS, né un renderer PSP completo. Non è stato
compilato con VitaSDK o eseguito su una Vita in questo ambiente. La prima
compilazione ARM e la verifica del collegamento vanno effettuate sul PC dell'utente.
Non contiene ISO, dati del gioco, codice ricompilato o librerie proprietarie.

Prima fase: prerequisiti
1. Estrarre pocket-gpu-addon nella cartella ~/mhfu-vita.
2. Da ~/mhfu-vita eseguire:
   python3 pocket-gpu-addon/check_gpu_deps.py
3. Se stampa MANCANTE, inviare l'output. Non compilare con dipendenze mancanti.
   Non installare librerie ARM generiche: servono le build per VitaSDK.

vitaGL richiede anche il compilatore shader libshacccg.suprx sulla Vita.
Il file non è fornito. Le istruzioni ufficiali sono nel README di vitaGL:
https://github.com/Rinnegatamante/vitaGL#prerequisites
Il programma controlla ur0:data/libshacccg.suprx e
ur0:data/external/libshacccg.suprx. Se manca, usa il renderer software e lo scrive
nel boot.log: in questo caso non aspettarsi un aumento degli FPS.
Le librerie devono essere compatibili con l'ABI floating-point della toolchain.
Non mascherare errori di collegamento o warning ABI.

Seconda fase: applicazione e build, SOLO dopo il controllo dei prerequisiti
Da ~/mhfu-vita:
   python3 pocket-gpu-addon/apply_gpu.py
   cmake -S vita/runtime -B build/full-runtime-vita -DPOCKET_VITA_GPU=ON
   cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Usare la stessa directory di build preserva la cache, i percorsi FFmpeg e
l'archivio privato del gioco già compilato. Non occorre rigenerare il gioco.
Gli installer verificano esattamente i due sorgenti inviati da Federico prima
di modificarli e creano copie .before-gpu-v1. Se differiscono si fermano.

Installare nuovamente il VPK con VitaShell dopo la build.
Il test mantiene il limite di 3600 secondi e START+SELECT per uscire.
Il log è sempre ux0:data/pocketprobe/boot.log.

Prova
Verificare prima i menu e la creazione del personaggio. Mandare un video e boot.log.
La riga "[gpu] hardware backend active" conferma che vitaGL è stato inizializzato.
Le righe "[gpu] presents ... fps" misurano le presentazioni di questo backend.
La sola riga "factory installed" non conferma l'attivazione della GPU.
Il successo della build non prova avvio, correttezza grafica o prestazioni.
Se appaiono errori [gpu], non eliminarli dal log.

Fallback reversibile
Creare con VitaShell un file vuoto:
   ux0:data/pocketprobe/software.flag
Al prossimo avvio la stessa app userà il renderer software già funzionante.
Per riprovare la GPU eliminare soltanto quel file e riavviare l'app.
In alternativa, ricompilare con:
   cmake -S vita/runtime -B build/full-runtime-vita -DPOCKET_VITA_GPU=OFF
   cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Implementato
- inizializzazione e uso vitaGL sul thread GE;
- triangoli, rettangoli, prospettiva delle coordinate e batching per comando GE;
- conversione texture con decoder esistente e cache per contenuto limitata a 16 MiB;
- fino a sei render target 512x512 con profondità e presentazione 480x272;
- scissor, clear disegnati, test depth e alpha di base, blending comune;
- fallback per immagini prodotte dalla CPU e flag software;
- log esplicito delle approssimazioni e degli errori.

Limiti noti
- Nessun aumento della risoluzione del gioco: 480x272, visualizzata a 960x544.
- Copie GE da buffer GPU alla memoria PSP non implementate. Le copie attuali
  utilizzano la memoria guest e possono quindi leggere dati non aggiornati.
- Stencil, test colore, operazioni logiche, fog PSP, mask alpha parziali,
  doppio colore texture e alcuni fattori di blending non sono fedeli.
- Render-to-texture supportato solo alla base esatta di un target, formato
  colore diretto. Letture da regioni interne, feedback o formati indicizzati
  possono avere difetti. I target vengono conservati fino al limite di sei.
- Le texture da memoria guest richiedono ancora decodifica sulla CPU.
- I buffer colore sono RGBA8888 anche quando la destinazione PSP è 16 bit;
  quantizzazione e precisione non identiche alla PSP.
- Z buffer separato per target colore: la condivisione PSP non è riprodotta.
- I punti GE restano sul percorso software del core e possono non essere visibili.
- Nessun frontend audio aggiunto, nessuna modifica ai salvataggi.
- Nessun gameplay o salvataggio certificato: test sul dispositivo indispensabile.

Verifiche effettuate prima della consegna
- sintassi C++20 con header vitaGL ufficiale e dichiarazioni host sostitutive
  per tipi GXM/SDK; warning del nuovo C++ trattati come errori;
- installer sulla copia esatta dei sorgenti ricevuti, protezione da riapplicazione;
- conservazione input, timer, audio pacing diagnostico e collegamento pthread;
- controllo sintattico degli script Python e controllo pacchetto ZIP.
Queste verifiche NON sostituiscono una compilazione ARM, il collegamento del
VPK o il test della GPU fisica. Il pacchetto contiene sorgenti sperimentali.
