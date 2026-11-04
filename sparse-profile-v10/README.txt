PocketProbe - diagnostica rendering v10 (richiede v9)

Non è una patch che promette un aumento FPS. Serve a scegliere il prossimo
intervento usando tempi separati, anziché contatori di disegni eliminati.

Da Ubuntu, con questa cartella estratta in ~/mhfu-vita:

cd ~/mhfu-vita
python3 sparse-profile-v10/apply_profile.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell. La sola copia non aggiorna
l'app installata. Nessun cambiamento ai file del gioco o ai salvataggi.

TEST
Controllare che il boot.log contenga [render-v10] sparse profiling installed.
Restare circa 45 secondi nel villaggio e 45 secondi in quest. Mantenere le
stesse impostazioni di overclock e non modificare altre flag tra le scene.
Inviare il boot.log completo prima di riavviare l'app (il log viene riscritto).

LETTURA DEI DATI
Ogni 5 secondi: [render-v10] e [render-v10-time].
I campioni dei percorsi frequenti sono pseudo-casuali, circa 1 ogni 2048
chiamate. Le liste, le rare decodifiche e gli upload vengono cronometrati
direttamente; matrici/hash hanno campionamento 1/256.
Le righe sono tempi INCLUSIVI: le categorie annidate si sovrappongono.
Non sommarle. Sono stime wall-clock sul worker GE, non percentuali CPU.
samples=0 significa dato non disponibile; non significa costo nullo.
Pochi campioni non permettono conclusioni affidabili. Il costo della lettura
dell'orologio è incluso; clock_pair_min_ns dà un riferimento, non una
correzione esatta. I totali commands_total/draws_total sono cumulativi.
Vecchi [gpu-cost-legacy-disabled] sono contatori spenti: ignorarne gli zeri.

Il probe può essere spento creando un file vuoto:
ux0:data/pocketprobe/no-sparse-profile.flag
Riavviare l'app dopo aver aggiunto/rimosso la flag. Con questa flag i vecchi
timer fini restano spenti. È utile per confrontare il costo della diagnostica.

INSTALLAZIONE E VERIFICHE
L'installer controlla tutte le sostituzioni prima di scrivere e salva i tre
sorgenti originali con suffisso .before-profile-v10. Non applica cambiamenti
se trova una versione incompatibile o la diagnostica già presente.
Test host: contatori e campionamento con orologio simulato; regressioni dei
vertici, culling e batch; controllo sintattico backend con header API.
La build ARM e la prova sulla Vita restano da eseguire sul PC dell'utente.
