PocketProbe v12 - percorso GPU per il formato 00091A (indici a 8 bit)
Richiede v11. Applicare una volta sola, dalla radice ~/mhfu-vita.

INSTALLAZIONE
Estrarre u8-gpu-v12 dentro ~/mhfu-vita e da Ubuntu eseguire:

cd ~/mhfu-vita
python3 u8-gpu-v12/apply_u8.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

Reinstallare pocket-game-boot.vpk con VitaShell. La sola copia non aggiorna
l'eseguibile installato. Le righe finali della build devono confermare la
creazione del VPK. Il pacchetto contiene sorgenti, non un VPK precompilato.

CAMBIAMENTO
Il precedente percorso rigido supportava solo VTYPE 00111A (indici uint16).
V12 aggiunge 00091A (indici uint8), con lo stesso layout vertice di 12 byte:
UV uint16 x2, colore RGBA4444, posizione int16 x3.
Lettura degli indici e avanzamento IADDR rispettano la diversa larghezza.
Geometria, colore, UV, cache v11 e controlli di clipping restano gli stessi.
Gli stati con illuminazione, texture generation o clear mode continuano
sul percorso di riferimento. La v12 non aggiunge GPU skinning/lighting.

MOTIVO DELLA SCELTA
Nel censimento precedente (16 campioni del villaggio, fase 171) il formato
00091A senza illuminazione risultava in 15520 disegni: circa 970 per campione.
Il censimento è precedente alla v11: non è una misura del risparmio attuale
né un benchmark degli FPS. Gran parte degli altri formati rimane distinta.

TEST
Verificare nel boot.log: [gpu-u8] v12 enabled.
Provare 30-45 secondi nel villaggio e in quest, con stessa posizione,
visuale e overclock dei test precedenti. Controllare anche eventuali difetti
grafici. Inviare boot.log completo prima di riavviare.
Le righe periodiche [gpu-u8] riportano attempts/draws/culled/fallback.
Questi numeri confermano l'uso del percorso; il beneficio va giudicato dagli
FPS e dal tempo totale delle liste, non dai soli contatori o timer fini.

CONFRONTO FACOLTATIVO SENZA RICOMPILARE
Creare il file vuoto ux0:data/pocketprobe/no-u8-gpu.flag per usare il vecchio
percorso per 00091A. Riavviare l'app dopo aver aggiunto o rimosso il file.
La riga iniziale riporta disabled by flag se la flag è presente.

VERIFICHE
Test host del backend con API GL simulate: output U8/U16 identico per
triangoli, strip e fan; indice 255; avanzamento puntatori attraverso Ge;
limiti RAM; fallback per stati non supportati; clipping, batch, matrici,
aggiornamento colori e UV. Regressione del percorso di riferimento.
Non è stata eseguita la build ARM in questo ambiente né la prova sulla Vita.
Nessun aumento FPS è garantito e non sono promessi 30/60 FPS.

BACKUP
L'installer verifica tutte le sostituzioni prima di scrivere e crea:
platforms/psp/gpu/ge.cpp.before-u8-v12
vita/runtime/vita_gpu.cpp.before-u8-v12
Rifiuta di sovrascrivere backup o applicare la patch una seconda volta.
Non modifica asset o salvataggi.
