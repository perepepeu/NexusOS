"""Generate a NEW disposable FAT32 volume using mtools, then fragment chains.
The output must not exist. Never accepts an existing disk to format.
"""
from pathlib import Path
import struct
import subprocess

SECTORS=131072
def create(path,work):
    with path.open("xb") as image: image.truncate(SECTORS*512)
    subprocess.run(["mformat","-i",str(path),"-F","-c","1","-v","NEXUS","::"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    def copy(name,data):
        source=work/"source.bin"; source.write_bytes(data)
        subprocess.run(["mcopy","-i",str(path),str(source),f"::/{name}"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    expected=bytes((i*7+11)&255 for i in range(8197))
    copy("FRAG.BIN",expected)
    subprocess.run(["mmd","-i",str(path),"::/SUB"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    copy("SUB/CHILD.TXT",b"fat32-child"); copy("EMPTY",b"")
    copy("Long File Name Example.txt",b"long-name-alias\n")
    copy("BROKEN.BIN",b"x"*512)
    for i in range(24): copy(f"F{i:02d}.TXT",b"fixture")
    copy("TAIL.TXT",b"tail")
    with path.open("r+b") as disk:
        boot=disk.read(512); reserved=struct.unpack_from("<H",boot,14)[0]; fats=boot[16]
        fatsize=struct.unpack_from("<I",boot,36)[0]; spc=boot[13]; root=struct.unpack_from("<I",boot,44)[0]
        data=reserved+fats*fatsize; clusters=(SECTORS-data)//spc
        def get(cluster):
            disk.seek(reserved*512+cluster*4); return struct.unpack("<I",disk.read(4))[0]&0xfffffff
        def put(cluster,value):
            for fat in range(fats):
                disk.seek((reserved+fat*fatsize)*512+cluster*4); raw=struct.unpack("<I",disk.read(4))[0]
                disk.seek(-4,1); disk.write(struct.pack("<I",(raw&0xf0000000)|value))
        def chain(first):
            result=[]
            while first<0xffffff8:
                assert 2<=first<=clusters+1 and first not in result
                result.append(first); first=get(first)
            return result
        def location(cluster): return (data+(cluster-2)*spc)*512
        def entries():
            result={}
            for cluster in chain(root):
                disk.seek(location(cluster)); raw=disk.read(spc*512)
                for i in range(0,len(raw),32):
                    e=raw[i:i+32]
                    if not e[0]: return result
                    if e[0]==0xe5 or e[11]==15 or e[11]&8: continue
                    name=e[:8].decode("ascii").rstrip()+(("."+e[8:11].decode("ascii").rstrip()) if e[8:11]!=b"   " else "")
                    result[name]=(location(cluster)+i,struct.unpack_from("<H",e,26)[0]|(struct.unpack_from("<H",e,20)[0]<<16))
            return result
        initial=entries(); assert any("~" in name for name in initial)
        def fragment(first):
            old=chain(first); assert len(old)>=2
            free=next(c for c in range(1000,clusters+2) if not get(c))
            disk.seek(location(old[1])); contents=disk.read(spc*512)
            disk.seek(location(free)); disk.write(contents)
            put(free,get(old[1])); put(old[0],free); put(old[1],0)
            assert chain(first)[1]==free and abs(free-old[0])>1
        fragment(initial["FRAG.BIN"][1]); fragment(root)
        updated=entries(); broken=updated["BROKEN.BIN"][1]; put(broken,broken)
        metadata={"root":root,"reserved":reserved,"fats":fats,"fatsize":fatsize,"data":data,
                  "clusters":clusters,"aliases":[n for n in updated if "~" in n]}
    actual=subprocess.check_output(["mtype","-i",str(path),"::/FRAG.BIN"],stderr=subprocess.PIPE)
    assert actual==expected,"External mtools reader disagrees with fragmented fixture"
    return metadata
