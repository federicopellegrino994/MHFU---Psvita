#!/usr/bin/env python3
from pathlib import Path
import shutil
p=Path('platforms/psp/runtime/src/hle_ge.cpp')
s=p.read_text()
if 'POCKET_GE_SHADER_STACK' in s:
    raise SystemExit('Correzione stack già presente: nessun file modificato.')
field='    std::thread worker;'
launch='        if (!worker.joinable()) worker = std::thread([this] { worker_main(); });'
join='        if (worker.joinable()) worker.join();'
for marker in [field,launch,join,'#include <thread>']:
    if s.count(marker)!=1:
        raise SystemExit('Sorgente diverso dal previsto: nessun file modificato. Invia hle_ge.cpp.')
s=s.replace('#include <thread>', '#include <thread>\n#include <system_error>\n#if defined(__vita__)\n#include <pthread.h>\n#endif',1)
s=s.replace(field,'''    // POCKET_GE_SHADER_STACK: runtime shader compilation needs more than the
    // Vita pthread default 32 KiB. Only the graphics worker gets a larger stack.
#if defined(__vita__)
    pthread_t worker{};
    bool worker_started = false;
    bool worker_joinable() const { return worker_started; }
    void join_worker() {
        const int rc = pthread_join(worker, nullptr);
        if (rc) std::fprintf(stderr, "[ge] pthread_join failed: %d\\n", rc);
        else worker_started = false;
    }
    void start_worker() {
        constexpr std::size_t stack_bytes = 2u * 1024u * 1024u;
        pthread_attr_t attr;
        int rc = pthread_attr_init(&attr);
        if (rc) throw std::system_error(rc, std::generic_category(), "GE pthread_attr_init");
        rc = pthread_attr_setstacksize(&attr, stack_bytes);
        if (!rc) rc = pthread_create(&worker, &attr, [](void *arg) -> void * {
            static_cast<GeState *>(arg)->worker_main();
            return nullptr;
        }, this);
        pthread_attr_destroy(&attr);
        if (rc) throw std::system_error(rc, std::generic_category(), "GE thread creation / stack");
        worker_started = true;
        std::fprintf(stderr, "[ge] graphics worker stack configured: %u bytes\\n",
                     static_cast<unsigned>(stack_bytes));
    }
#else
    std::thread worker;
    bool worker_joinable() const { return worker.joinable(); }
    void join_worker() { worker.join(); }
    void start_worker() { worker = std::thread([this] { worker_main(); }); }
#endif''',1)
s=s.replace(launch, '        if (!worker_joinable()) start_worker();',1)
s=s.replace(join, '        if (worker_joinable()) join_worker();',1)
backup=p.with_name(p.name+'.before-shader-stack')
if backup.exists():
    raise SystemExit('Backup già presente: nessun file modificato.')
shutil.copy2(p,backup)
p.write_text(s)
print('Thread grafico configurato con stack di 2 MiB. Ricompilare e reinstallare il VPK.')
