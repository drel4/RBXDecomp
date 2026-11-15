"""Run host loader rejection tests, in addition to rendering real places."""
import pathlib, subprocess, tempfile

exe=pathlib.Path(__file__).resolve().parents[1]/'scene_check'
def run(data, good, label):
    with tempfile.NamedTemporaryFile(suffix='.rbxl') as f:
        f.write(data); f.flush()
        p=subprocess.run([str(exe),f.name],capture_output=True,text=True)
        assert (p.returncode==0)==good,(label,p.returncode,p.stdout,p.stderr)
    print('PASS:',label)

root=b'<roblox version="4"><Item class="Workspace">'
part=b'<Item class="Part"><Properties><Vector3 name="size"><X>2</X><Y>3</Y><Z>4</Z></Vector3></Properties></Item>'
tail=b'</Item></roblox>'
run(root+part+tail,True,'minimal XML part')
run(b'<?xml version="1.0"?>'+root+b'<!-- <Item> -->'+part+b'<Item class="Script"><Properties><string name="Source"><![CDATA[<Item class="Part">not geometry]]></string></Properties></Item>'+tail,True,'comments and script CDATA do not become parts')
run(b'<roblox!\x89\xff\r\n\x1a\n'+b'\0'*64,False,'binary RBXL explains unsupported format')
run(root+part,False,'truncated document')
run(root+part.replace(b'<X>2',b'<X>nan')+tail,False,'non-finite size')
run(root+part.replace(b'<X>2',b'<X>-2')+tail,False,'negative dimensions')
run(root+part.replace(b'</Properties>',b'<float name="Transparency">1</float></Properties>')+tail,False,'invisible-only scene')
run(b'<!DOCTYPE roblox [<!ENTITY x SYSTEM "file:///etc/passwd">]>'+root+part+tail,False,'no external entity expansion')
run(b'<roblox><Item class="StarterPack">'+part+tail,False,'backpack geometry is not world geometry')
run(root+part.replace(b'</Vector3>',b'</CoordinateFrame>')+tail,False,'mismatched XML')
run(root+b'<Model>'*200+part+b'</Model>'*200+tail,False,'nesting bound')
run(root+part*8001+tail,False,'part memory budget')
large=part.replace(b'<X>2',b'<X>1000').replace(b'<Y>3',b'<Y>1000').replace(b'<Z>4',b'<Z>1000')
run(root+large+tail,True,'subdivided large faces stay within reserved vertex count')
run(root+large*17+tail,False,'subdivided geometry memory budget')
run(root+part+b'<Item class="Script"><Properties><ProtectedString name="Source"><![CDATA['+b'x'*262145+b']]></ProtectedString></Properties></Item>'+tail,False,'script source budget rejects instead of truncating')
run(root+part.replace(b'class="Part"',b'class="Part" referent="same"')*2+tail,False,'duplicate referents rejected')
run(root+part.replace(b'</Properties>',b'<string name="Name">bad &unknown;</string></Properties>')+tail,False,'undeclared entity rejected')
inline=b'<Item class="Decal"><Properties><Content name="Texture"><binary>'
inline_end=b'</binary></Content></Properties></Item>'
run(root+part+inline+b'A'*900000+inline_end+tail,True,'bounded large inline image retained')
run(root+part+inline+b'A'*1048577+inline_end+tail,False,'inline binary budget rejects instead of truncating')
