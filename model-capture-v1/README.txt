PocketProbe - cattura mirata di un modello statico / v1

Richiede la base v12 e model-entry-probe-v1 ancora installata nei sorgenti.
La cattura è diagnostica: NON è ancora il renderer nativo o una patch FPS.

1. INSTALLAZIONE SU UBUNTU
Estrarre model-capture-v1 dentro ~/mhfu-vita, poi eseguire:

cd ~/mhfu-vita
python3 model-capture-v1/apply_capture.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell. Se l'installer si ferma,
inviare il messaggio: non forzare le sostituzioni.

2. PROVA SULLA VITA
Avviare il gioco, caricare il salvataggio ed entrare nel villaggio.
Restarci circa 20 secondi. Non è necessario andare anche in quest.
Il primo PMO statico idoneo (Hierarchy nullo, almeno 8 mesh) viene catturato
automaticamente. Potrebbe essere il primo ambiente disponibile, non
necessariamente un particolare edificio. Può esserci una pausa durante
la scrittura su scheda. Gli FPS di questa prova non sono un benchmark.

La cattura salva UN modello e fino a 32 disegni GE, massimo 12 MiB:
ux0:data/pocketprobe/model-capture.bin
Il log rimane:
ux0:data/pocketprobe/boot.log

Righe attese:
[model-capture-v1] model saved: ... waiting for matching GE commands
[model-capture-v1] complete: 32 GE draws, ... bytes
Se il numero di disegni non arriva a 32, un file parziale con chunk completi
può essere utile comunque: lo strumento lo segnala. In caso di failed,
mandare prima il messaggio del log. Se non compare model saved, non assumere
che un vecchio model-capture.bin sia il risultato della nuova esecuzione.

3. COPIA DEI FILE SUL PC
PRIMA di riavviare l'app, copiare con VitaShell entrambi i file nella
cartella Ubuntu ~/mhfu-vita/user_data/incoming.
Per aprirla da Ubuntu:

cd ~/mhfu-vita
mkdir -p user_data/incoming
explorer.exe "$(wslpath -w "$PWD/user_data/incoming")"

La cartella contiene già eventualmente EBOOT.BIN e DATA.BIN: non includerli
nell'archivio. Il comando seguente prende solo model-capture.bin e boot.log.

4. CREAZIONE DELL'ARCHIVIO DA INVIARE

python3 model-capture-v1/package_capture.py
explorer.exe "$(wslpath -w "$PWD/build")"

Inviare build/model-capture.zip. Lo script verifica formato, dimensioni,
checksum di ciascun chunk e integrità ZIP. Controlla che il boot.log
confermi gli indirizzi del modello catturato. Il riepilogo indica anche
se la geometria copiata all'ingresso coincide con quella osservata sul GE.
Un valore False è un dato da analizzare: le due copie vengono conservate.

COSA CONTIENE
- Oggetto PMO e tabelle header/materiali/illuminazione, quando mappati.
- Geometria dal puntatore runtime (anche VRAM), non da un indirizzo dedotto
  assumendo che tutta la geometria segua l'header del file.
- Oggetto TMH, header e frammenti, se leggibili entro i limiti.
- Sul worker GE: registri, matrici world/view/projection/texgen e ossa,
  CLUT, VTYPE/stride, indici, intervallo dei vertici e parole al PC corrente.
- Fino a quattro contenuti texture distinti, ciascuno al massimo 1 MiB,
  con formato, dimensioni, stride, swizzle e checksum associati.

LIMITI
È la cattura di un modello e di un sottoinsieme di disegni, NON un frame
atomico dell'intero gioco. Il thread del gioco può avanzare mentre il GE
lavora: per questo si conservano due copie della geometria. Le singole
matrici/registri sono letti sul worker GE prima del disegno corrispondente.
L'associazione richiede che il PC del comando PRIM cada nel blob GE di quel
PMO; nel tuo ELF la routine drawMesh emette CALL verso quei listati.
Intervalli di vertici oltre 256 KiB, indici non mappati, texture oltre 1 MiB
o ulteriori texture oltre le prime quattro vengono segnalati/omessi.
I tag di dimensione zero significano assenti/non mappati o non necessari
(per esempio un disegno senza indici), non memoria piena di zeri.
Questo file contiene dati estratti dalla tua copia del gioco: conservarlo
per l'analisi del progetto, senza includerlo in una release pubblica.

SICUREZZA DEL RIPRISTINO
L'installer non modifica asset o salvataggi. Aggiunge un header e modifica
ge.cpp e il solo header della traccia PMO; crea backup .before-model-capture-v1.
I corpi generati continuano a svolgere il loro lavoro originale.
Non rigenerare il codice mentre questi strumenti sono applicati.
Ogni nuovo avvio che trovi un modello idoneo RISCRIVE model-capture.bin.

Dopo aver copiato la cattura, rimuovere prima la cattura e poi la traccia:
python3 model-capture-v1/restore_capture.py
python3 model-entry-probe-v1/restore_probe.py
Ricompilare e reinstallare il VPK per rimuoverle anche dall'app installata.
Gli script verificano gli hash e si fermano se trovano modifiche successive.

VERIFICHE ESEGUITE QUI
- Ge reale su host con memoria PSP/VRAM e flusso sintetico di comandi U8:
  32 record, integrità, geometrie confrontate, nessuna scrittura guest.
- Una modifica tra ingresso e GE viene rilevata; file corrotti e troncati
  vengono rifiutati dal verificatore; ZIP controllato con CRC.
- Regressione geometrica: stesso hash su 3840 comandi e 115200 vertici.
- Installazione/ripristino concatenati con la traccia precedente, include
  relativo compilato, rifiuto di file incompatibili o modifiche concorrenti.
La compilazione ARM/VPK e la prova sulla Vita restano da eseguire sul PC
dell'utente: qui non è disponibile VitaSDK.
