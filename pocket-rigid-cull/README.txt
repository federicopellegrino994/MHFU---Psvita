PocketProbe — scarto anticipato geometria v9 (richiede v8)

Il log v8 conferma il percorso attivo: gestisce circa il 41% dei tentativi nel
villaggio e il 69% in quest. Il resto torna al GE CPU per il controllo clipping.
Questa patch evita quel secondo passaggio nei soli gruppi di geometria che
possiamo dimostrare essere certamente esclusi dal GE di riferimento.
Non è stato ancora misurato un aumento di FPS su Vita per questa patch.

REGOLE
Supporta gli stessi disegni 0x00111A della v8. Per ogni vertice classifica:
- compatibile con il percorso GPU;
- certamente esterno al near plane (o con w negativo);
- certamente dentro il near plane ma esterno alla guard band PSP 0..4096;
- incerto / profondità da clampare / altro: fallback CPU.

Scarta un intero comando SOLO se tutti i suoi vertici sono certamente esterni
secondo la seconda o terza regola. In quel caso ogni triangolo del comando ha
un vertice che determina lo scarto guard-band, oppure non ha alcun vertice
interno al near plane. È lo stesso risultato di Ge::draw_triangle.
Le tolleranze numeriche conservano i casi vicini ai confini sul percorso CPU.
La profondità fuori range NON viene considerata invisibile: il backend
precedente la clampa, quindi rimane sul percorso precedente.
I gruppi misti non vengono disegnati parzialmente: usano il fallback completo.
Non cambia il taglio PSP per aumentare artificialmente la copertura GPU.

INSTALLAZIONE
Estrarre pocket-rigid-cull dentro ~/mhfu-vita. Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-rigid-cull/apply_cull.py

Solo dopo "V9 applicata", compilare:
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2

Aprire la cartella:
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell.
Provare villaggio e la stessa area della quest, con gli stessi clock e posizioni
comparabili. Inviare boot.log e segnalare eventuali nuovi difetti grafici.

LOG
[gpu-rigid-cull] v9 enabled; ...
[gpu-rigid-cull] period: culled_draws=... culled_vertices=... outside_near=...
outside_guard=... boundary_vertices=...

culled_draws / culled_vertices sono gruppi e vertici scartati in anticipo, senza
invio GPU o secondo passaggio al GE CPU. outside_near/outside_guard/boundary
contano i vertici esaminati e non corrispondono tutti a disegni scartati.
Le righe [gpu-rigid] restano: draws significa comandi disegnati dal percorso GPU;
quelli gestiti ma esclusi sono separati in culled_draws. Il residuo ritorna CPU.

DISATTIVAZIONE / CONFRONTO
Per spegnere solo lo scarto anticipato, copiare il file vuoto
optional/no-rigid-cull.flag sulla Vita come:
ux0:data/pocketprobe/no-rigid-cull.flag
Riavviare l'app. Rimuovere il file per riattivarlo.
Il flag v8 no-rigid-gpu.flag continua a disattivare l'intero percorso GPU rigido.
Non copiare i flag per attivare normalmente il nuovo test.

VERIFICHE HOST
- 873 disegni scartati dalla nuova logica, con matrici variabili, triangoli,
  strisce e ventagli: il GE reale di riferimento produce zero triangoli.
- Casi misti/incerti rimangono in fallback; verificati esplicitamente profondità
  lontana da clampare e attraversamento del near plane.
- Passano i test precedenti con GL simulato: raggruppamento, cambi matrice,
  passaggi CPU/GPU, fallback senza disegni parziali, colori piatti e UV RTT.
- Confronto 2400 vertici col GE, tutti i colori RGBA4444, UV, avanzamento degli
  indici e generazioni matrici; sintassi backend con header ufficiale vitaGL.
- Installer: backup, verifica hash dell'header v8, rifiuto di sorgenti inattesi
  o della riapplicazione senza modifiche.

Non compilato ARM né eseguito su GPU Vita in questo ambiente. I test con GL
simulato verificano la logica CPU, non il driver reale o gli FPS del gioco.
Modifica solo vita/runtime/vita_gpu.cpp e rigid_gpu_math.hpp.
Backup con suffisso .before-cull-v9. Preserva controlli, salvataggi e timer.
