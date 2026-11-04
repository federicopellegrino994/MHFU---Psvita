Correzione dello stack del thread grafico per Pocket GPU v2

Il dump psp2core-1793701313 mostra Data Abort in SceShaccCg +0x85124.
Thread pthread 0x4001012d: base stack 0x8c621000, dimensione 0x8000 (32 KiB).
SP=0x8c620f78 e DFAR=0x8c620f70 sono sotto la base dello stack.
Il secondo dump .tmp è troncato e non è stato usato per confermare la diagnosi.

Questa patch crea soltanto il worker GE con pthread_attr_setstacksize a 2 MiB.
Controlla gli errori delle API pthread e mantiene join e shutdown del worker.
Gli altri thread, i dati del gioco, i salvataggi e il timer di un'ora non cambiano.
Non disattiva il combinatore RGB/RGBA o le misure della v2.

Estrarre pocket-gpu-stack-fix dentro ~/mhfu-vita e, da quella cartella, eseguire:
  python3 pocket-gpu-stack-fix/apply_gpu_stack.py
Se conferma l'applicazione:
  cmake --build build/full-runtime-vita --target pocket-game-boot.vpk-vpk -j2
Poi copiare e reinstallare il VPK con VitaShell.

Log atteso: [ge] graphics worker stack configured: 2097152 bytes
Provare prima i menu, poi casa/villaggio e inviare boot.log.
Non è un'ottimizzazione FPS: corregge la dimensione dello stack del compilatore shader.

Il sorgente originale è copiato in hle_ge.cpp.before-shader-stack.
L'installer rifiuta strutture inattese e una seconda applicazione.
Verifiche locali: applicazione/ripetizione su sorgente ricevuto, sintassi C++20
con ramo __vita__ e dichiarazioni pthread dell'host. Compilazione e verifica
sul dispositivo Vita restano da effettuare dall'utente.
