PocketProbe — censimento renderer v1, successivo alla v7

Serve a scegliere il primo percorso da portare su shader/GPU. Non aumenta gli
FPS e non implementa ancora un nuovo renderer. Non include dati del gioco.

INSTALLAZIONE
Estrarre pocket-render-census dentro ~/mhfu-vita. Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-render-census/apply_census.py

Solo se appare "Censimento grafico applicato", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Aprire la cartella:
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk tramite VitaShell. Provare villaggio e quest
per circa 30 secondi ciascuno agli stessi clock. Inviare boot.log prima del
successivo avvio. La cattura può rallentare il singolo fotogramma misurato:
questa build diagnostica non va usata per confrontare gli FPS con la v7.

COSA LEGGERE NEL LOG
[ge-census-time]: tempi CPU wall misurati su UN intervallo completo ogni 30
presentazioni, senza estrapolare le nuove righe. Non è necessariamente un frame
logico del gioco: misura i comandi nella coda GE tra due presentazioni.
- lists: esecuzione delle liste e loro flush, esclusa la presentazione;
- info: preparazione DrawInfo e callback del backend;
- vertices: decodifica, riuso, skinning, trasformazioni e illuminazione;
- assembly: assemblaggio, clipping e invio al backend fino a fine PRIM.
Le ultime tre fasi sono disgiunte e contenute in lists. Il resto comprende
altri comandi e costi dell'esecutore delle liste. Le misure comprendono overhead
e possibili attese interne: non sono tempi hardware GPU o occupazione del core.
I vecchi [perf] restano stime estrapolate. Per questa diagnosi usare [ge-census].

[ge-census-format]: VTYPE e stride, pesi, morph, illuminazione, texgen, fog,
numero di vertici e disegni per formato. Al massimo 64 combinazioni raccolte,
12 mostrate; overflow_draws segnala combinazioni ulteriori. simple3d_vertices
classifica triangoli 3D senza pesi, morph multipli, illuminazione, texgen speciale
o clear. Non significa che un nuovo shader sia già implementato.
La riga vitaGL può restare v5-state-generation: il backend GPU non cambia.

DISATTIVAZIONE
Creare sulla Vita un file vuoto ux0:data/pocketprobe/no-census.flag e riavviare
l'app. Rimuoverlo per riattivare la cattura. Niente ricompilazione per il flag.

PER ANALIZZARE LE FUNZIONI DEL GIOCO
Allegare anche il file sul PC:
~/mhfu-vita/user_data/prepared/ULES01213/prepared.elf
Pesa circa 2 MB. Per iniziare l'analisi del codice principale non occorrono ISO
o DATA.BIN interi. Gli overlay potrebbero servire in una fase successiva.

RISULTATO DEL CONFRONTO CON TRI
Il nostro Backend riceve vertici già trasformati, illuminati e ritagliati da
Ge::draw_primitive, Ge::transform e Ge::draw_triangle. Uno shader aggiunto dopo
questo punto non elimina quel lavoro CPU. Serve un ingresso prima di queste
trasformazioni o un hook nelle funzioni del gioco che producono i modelli.
La mappa simboli EU disponibile identifica avvio e overlay, non quelle funzioni.

Il progetto di Tri descrive conversione dei modelli al caricamento, produttori
di disegni nativi al posto di GX e skinning sulla GPU. Queste idee sono utili;
formati dei modelli e indirizzi delle funzioni Wii non si trasferiscono a MHFU.
Riferimenti primari:
https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/rendering/gx-replacement.md
https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/rendering/gpu-skinning.md
https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/rendering/deferred-model-preparation.md

FILE E VERIFICHE
Modifica ge.cpp, ge.hpp e runtime/src/hle_ge.cpp dentro platforms/psp;
aggiunge gpu/pocket_geometry_probe.hpp. Backup .before-census-v1.
Preserva stack GE, stick destro, salvataggi, backend e durata test.
Installer: controllo preventivo e rifiuto della riapplicazione senza modifiche.
Test host: 3840 comandi, 115200 vertici, hash identico prima/dopo.
Test contatori: somme esatte, formati aggregati, cadenza, frame inattivi,
overflow limitato. Integrazione: fasi disgiunte contenute nel totale liste.
Sintassi GE con ramo Vita e hle_ge con ramo host verificata.
Non compilato ARM né eseguito su Vita in questo ambiente.
