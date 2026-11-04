Verifica del codice ARM del renderer — raccolta in sola lettura

Estrarre pocket-arm-build-check dentro ~/mhfu-vita.
Nel terminale Ubuntu:
cd ~/mhfu-vita
python3 pocket-arm-build-check/collect_arm.py

Poi aprire la cartella:
explorer.exe "$(wslpath -w "$PWD/build")"

Allegare arm-render-diagnostics.zip in chat.
Non richiede compilazione, collegamento o installazione di un altro VPK.

Raccoglie SOLO gli oggetti ARM ge.cpp e vita_gpu.cpp presenti nella build
full-runtime-vita, relative opzioni del compilatore/linker e un manifest.
Non include ISO, prepared.elf, DATA.BIN, salvataggi o codice generato del gioco.
Non modifica sorgenti o configurazioni. Scrive solo il piccolo archivio
build/arm-render-diagnostics.zip, sostituendo un'eventuale raccolta precedente.

Scopo: verificare il codice ARM effettivo, uso delle istruzioni floating point,
funzioni di supporto chiamate e opzioni applicate. L'analisi del binario non è
una misura di dove la CPU trascorre il tempo: serve a verificare prima una
possibile causa strutturale, senza un altro ciclo di VPK sperimentali.
