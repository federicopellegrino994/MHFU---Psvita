from pathlib import Path
import struct,sys
src=Path(sys.argv[1]).read_bytes();assert src[:8]==b'PMOCAP1\0' and len(src)<=12*1024*1024
parts={};offset=8
while offset<len(src):
 assert len(src)-offset>=16
 tag,address,size,wanted=struct.unpack_from('<4I',src,offset);offset+=16
 assert size<=len(src)-offset
 data=src[offset:offset+size];offset+=size;h=2166136261
 for byte in data:h=((h^byte)*16777619)&0xffffffff
 assert h==wanted
 if tag<100:parts[tag]=(address,data)
assert all(t in parts for t in [1,2,3,4,5,6,7,9,10])
assert parts[4][1]==parts[10][1], 'Geometria modificata tra ingresso e GE: questa fixture non è confrontabile.'
meta=struct.unpack('<13I',parts[1][1]);items=[parts[t] for t in [2,3,4,5,6,7,9]]
result=struct.pack('<5I',0x504d4f31,meta[1],meta[3],meta[4],len(items))
for address,data in items:result+=struct.pack('<II',address,len(data))+data
out=Path(sys.argv[2]);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(result)
print('Fixture privata creata:',out)
