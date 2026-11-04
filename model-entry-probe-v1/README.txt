PocketProbe - ingressi del renderer PMO, traccia v1

SCOPO
Preparare un percorso nativo al livello dei modelli del gioco, prima che
questi diventino migliaia di piccoli comandi GE. Questo pacchetto osserva
quattro routine del gioco e i relativi oggetti PMO. Non implementa ancora
il renderer nativo, non salta funzioni e non promette un aumento FPS.

INDIRIZZI EU ULES01213 / versione 1.01
0886171C pmo::drawMesh
088646F0 pmo::drawWeight
0886477C pmo::drawWeightMesh
088623DC pmo::compile (chiamata bind nella mappa della decompilazione)

Le identità provengono dalla documentazione MHFU-ModKit e dalla mappa JP
di mhp2g-decomp; indirizzi, prologhi e letture compatibili con le strutture
sono stati controllati nell'ELF EU SHA1:
04b60ee797bd628faa0525ebd52186fa05967963
Questa verifica statica non dimostra che siano tutte chiamate nel gameplay:
la traccia serve proprio a verificarlo sulla build corrente.

INSTALLAZIONE
Estrarre model-entry-probe-v1 dentro ~/mhfu-vita.
Su Ubuntu:

cd ~/mhfu-vita
python3 model-entry-probe-v1/apply_probe.py
cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
explorer.exe "$(wslpath -w "$PWD/build/full-runtime-vita")"

L'installer richiede i quattro corpi C++ già generati: non rigenera il gioco.
Modifica temporaneamente solo i file che li contengono e crea backup con
suffisso .before-model-entry-v1. La build può ricompilare quei file del gioco
e rifare il collegamento/VPK. Non cambiare il numero di parti o rilanciare
la generazione durante questa prova. Se l'installer segnala un ingresso
mancante, inviare quel messaggio: si ferma senza modificare i sorgenti.

Reinstallare il VPK con VitaShell. Nessuna modifica a ISO, asset o salvataggi.

TEST
Entrare nel villaggio e restarci circa 20 secondi; se possibile entrare in
quest per altri 20 secondi. Recuperare boot.log PRIMA di riavviare l'app.
Inviare il log completo. Ci interessano:
[model-entry-v1] nome routine, numero chiamate, argomenti e indirizzo chiamante
[model-entry-v1-pmo] oggetto PMO, header, conteggi e riferimenti alla geometria

I contatori sono cumulativi. Vengono scritti solo i primi otto ingressi e
poi le potenze di due, quindi non costituiscono un censimento di tutti gli
oggetti o di tutti i chiamanti. Nessun timer per vertice. La traccia non
modifica registri o memoria guest. I puntatori vengono controllati prima
di leggere; un header non confermato viene segnalato, non interpretato.
Le prestazioni non sono l'obiettivo di questa build diagnostica.

RIMOZIONE DOPO LA RACCOLTA
Da ~/mhfu-vita:
python3 model-entry-probe-v1/restore_probe.py
Poi ricompilare e reinstallare il VPK. Lo script verifica gli hash prima
di ripristinare, per non sovrascrivere altre modifiche successive.
Conserva i backup originali. Non applicare altre patch ai file generati
prima di aver rimosso questa traccia.

VERIFICHE
Installer provato su sorgenti di test, con ingressi mancanti e duplicati,
riapplicazione e ripristino dopo modifiche concorrenti. C++ strumentato
compilato su host; memoria/registri guest preservati, puntatori invalidi
gestiti. La build ARM e la prova sulle funzioni generate reali restano da
eseguire sul PC e sulla Vita dell'utente.

FONTI
https://github.com/MHFU-ModKit/formats/blob/main/docs/ANIMATION_FORMAT.md
https://github.com/MHFU-ModKit/formats/blob/main/docs/PMO_MODEL_FORMAT.md
https://github.com/tclamb/mhp2g-decomp/blob/development/config/eboot.symbol_addrs.txt
https://github.com/tclamb/mhp2g-decomp/blob/development/include/model_base.hpp
