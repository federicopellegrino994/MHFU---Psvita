#!/usr/bin/env python3
from pathlib import Path
import argparse,struct,json,zipfile,hashlib

LIMIT=12*1024*1024
def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h
def inspect_capture(path):
    if path.stat().st_size>LIMIT:raise ValueError('Cattura oltre il limite previsto di 12 MiB')
    data=path.read_bytes()
    if data[:8]!=b'PMOCAP1\0':raise ValueError('Magic cattura non valido')
    at=8;chunks=[];draws=[];geometry={};missing=[];finished=False;model=None
    while at<len(data):
        if len(data)-at<16:raise ValueError('Header chunk troncato')
        tag,address,size,checksum=struct.unpack_from('<4I',data,at);at+=16
        if size>len(data)-at:raise ValueError('Dati chunk troncati')
        payload=data[at:at+size];at+=size
        if fnv(payload)!=checksum:raise ValueError(f'Checksum chunk {tag} non valido')
        chunks.append({'tag':tag,'address':f'{address:08X}','size':size,'fnv1a32':f'{checksum:08X}'})
        if tag==1:
            if size!=52:raise ValueError('Metadati modello di dimensione errata')
            v=struct.unpack('<13I',payload)
            model={'self':f'{v[1]:08X}','header':f'{v[6]:08X}','geometry':f'{v[7]:08X}','geometry_bytes':v[8],'mesh_count_field':v[11],'selected_mesh_argument':v[4]}
        if tag in (4,10):geometry[tag]=hashlib.sha256(payload).hexdigest()
        if tag==100:
            if size!=44:raise ValueError('Metadati disegno di dimensione errata')
            v=struct.unpack('<11I',payload);draws.append({'number':v[0],'pc':f'{v[1]:08X}','primitive':v[2]>>16&7,'count':v[7],'stride':v[3],'vertex_bytes':v[9],'indices_mapped':bool(v[10])})
        if size==0 and tag in (5,6,7,8,9,108,109,110):missing.append({'tag':tag,'address':f'{address:08X}'})
        if tag==255:finished=True
    if not any(c['tag']==1 for c in chunks) or 4 not in geometry:raise ValueError('Dati modello essenziali mancanti')
    return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'model':model,'chunks':chunks,'draws':draws,'footer_present':finished,
        'entry_and_ge_geometry_equal':geometry[4]==geometry[10] if 10 in geometry else None,
        'missing_or_empty_sections':missing,
        'note':'Cattura di un modello e di disegni GE, non un frame atomico dell’intero gioco. Un footer mancante può indicare una cattura parziale; la parità delle due geometrie va verificata.'}
def main():
    p=argparse.ArgumentParser(description='Verifica e impacchetta soltanto la cattura PMO e il boot.log copiati dalla Vita.')
    p.add_argument('capture',nargs='?',type=Path,default=Path('user_data/incoming/model-capture.bin'))
    p.add_argument('--log',type=Path,default=Path('user_data/incoming/boot.log'))
    p.add_argument('--out',type=Path,default=Path('build/model-capture.zip'))
    a=p.parse_args()
    if not a.capture.is_file():p.error('model-capture.bin non trovato nel percorso indicato')
    try:summary=inspect_capture(a.capture)
    except ValueError as e:p.error(str(e))
    if not a.log.is_file():p.error('boot.log mancante: copialo dalla stessa esecuzione della Vita')
    if a.log.stat().st_size>4*1024*1024:p.error('boot.log oltre 4 MiB: invialo separatamente')
    model=summary['model'];expected=f"this={model['self']} header={model['header']} geometry={model['geometry']} bytes={model['geometry_bytes']}"
    if '[model-capture-v1] model saved: '+expected not in a.log.read_text(errors='replace'):
        p.error('Il boot.log non conferma il modello di questa cattura: copia il log della stessa esecuzione')
    if a.out.resolve() in (a.capture.resolve(),a.log.resolve()):p.error('Il file ZIP non può sostituire i file di ingresso')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(a.out,'w',zipfile.ZIP_STORED) as z:
        z.write(a.capture,'model-capture.bin');z.write(a.log,'boot.log')
        z.writestr('capture-summary.json',json.dumps(summary,indent=2,ensure_ascii=False))
    with zipfile.ZipFile(a.out) as z:
        if z.testzip() is not None:p.error('Verifica ZIP fallita')
    print('Archivio verificato:',a.out.resolve())
    print('Disegni GE:',len(summary['draws']),'Footer:',summary['footer_present'],'Geometria identica ingresso/GE:',summary['entry_and_ge_geometry_equal'])
    if not summary['draws']:print('ATTENZIONE: modello presente ma nessun disegno GE corrispondente; invia comunque archivio e log per analisi.')
    elif summary['entry_and_ge_geometry_equal'] is False:print('ATTENZIONE: la geometria è cambiata tra ingresso e GE; entrambe le copie sono conservate.')
if __name__=='__main__':main()
