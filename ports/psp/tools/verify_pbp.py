"""Reject accidental static-ELF payloads or missing PSP-3008 RAM requests."""
import pathlib, struct, sys
p=pathlib.Path(sys.argv[1]).read_bytes()
header=struct.unpack_from('<10I',p)
assert header[0]==0x50425000,'Not an EBOOT.PBP'
offsets=header[2:]+(len(p),)
assert list(offsets)==sorted(offsets) and offsets[0]>=40,'Invalid PBP offsets'
sfo=p[offsets[0]:offsets[1]]
magic,version,keys,data,count=struct.unpack_from('<4s4I',sfo)
assert magic==b'\0PSF'
values={}
for n in range(count):
    key,fmt,length,capacity,offset=struct.unpack_from('<HHIII',sfo,20+n*16)
    end=sfo.index(b'\0',keys+key)
    name=sfo[keys+key:end].decode()
    raw=sfo[data+offset:data+offset+length]
    values[name]=struct.unpack('<I',raw)[0] if fmt==0x404 else raw.rstrip(b'\0').decode(errors='replace')
assert values.get('MEMSIZE')==1,values
payload=p[offsets[6]:offsets[7]]
assert payload[:4]==b'\x7fELF','Missing PRX payload'
kind,machine=struct.unpack_from('<HH',payload,16)
assert kind==0xffa0 and machine==8,'Payload must be a relocatable PSP PRX, not a static ELF'
print(f"PASS: {values['TITLE']}; MEMSIZE=1; MIPS PRX 0x{kind:04x}; {len(p):,}-byte PBP")
