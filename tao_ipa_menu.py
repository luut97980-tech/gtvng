#!/usr/bin/env python3
"""Add the built menu to a local, readable IPA; output must be signed before install.

Python 3.9+ standard library. No game binary or signing credential is uploaded.
No decryption, code-signing bypass, or automatic installation is performed.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import struct
import sys
import tempfile
import zipfile

INSTALL_NAME='@executable_path/Frameworks/VTMenu.dylib'
EXPECTED_BUNDLES=('com.vng.playtogether','ptg.vng','com.haegin.playtogether')
EXPECTED_VERSION='2.32.0'
EXPECTED_BUILD='227628'
EXPECTED_UUID='1c1d8aea78c838f89a09d62cc639465d'
EXPECTED_METADATA_SHA='ddefd97ac49d5431d25757e409e4c03973c79f553c025b931da00949867cc5fb'

def check(condition,message):
    if not condition:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def macho(data):
    check(len(data)>=32 and data[:4]==b'\xcf\xfa\xed\xfe','Can Mach-O 64-bit little-endian, khong phai DLL Windows.')
    magic,cpu,sub,kind,ncmd,sizeofcmds,flags,reserved=struct.unpack_from('<8I',data)
    check(cpu==0x100000c,'Can binary ARM64.')
    check(ncmd<=4096 and 32+sizeofcmds<=len(data),'Bang load command bi hong.')
    commands=[];off=32;first_data=len(data);uuid=None;dependencies=[];encrypted=False
    for unused in range(ncmd):
        check(off+8<=32+sizeofcmds,'Load command bi cat.')
        cmd,size=struct.unpack_from('<II',data,off)
        check(size>=8 and size%8==0 and off+size<=32+sizeofcmds,'Kich thuoc load command khong hop le.')
        commands.append((cmd,off,size))
        if cmd==0x19:
            check(size>=72,'Segment command bi cat.')
            nsect=struct.unpack_from('<I',data,off+64)[0]
            check(72+nsect*80<=size,'Bang section bi cat.')
            for i in range(nsect):
                at=off+72+i*80
                addr,length,position,align=struct.unpack_from('<QQII',data,at+32)
                section_flags=struct.unpack_from('<I',data,at+64)[0]
                # Zerofill sections have no bytes in the file.
                if length and position and (section_flags&0xff) not in (1,0xc,0x12):
                    check(position+length<=len(data),'Section nam ngoai file.')
                    first_data=min(first_data,position)
        if cmd in (0x21,0x2c):
            check(size>=20,'Encryption command bi cat.')
            co,cs,ci=struct.unpack_from('<III',data,off+8)
            check(co+cs<=len(data),'Encryption range khong hop le.')
            encrypted|=ci!=0
        if cmd==0x1b:
            check(size>=24,'UUID command bi cat.')
            uuid=data[off+8:off+24].hex()
        if cmd in (0xc,0x80000018,0x8000001f):
            nameoff=struct.unpack_from('<I',data,off+8)[0]
            check(24<=nameoff<size,'Ten dependency khong hop le.')
            dependencies.append(data[off+nameoff:off+size].split(b'\0')[0].decode('utf-8'))
        off+=size
    check(off==32+sizeofcmds,'Bang load command khong khop sizeofcmds.')
    return {'kind':kind,'ncmd':ncmd,'sizeofcmds':sizeofcmds,'first_data':first_data,
            'uuid':uuid,'dependencies':dependencies,'encrypted':encrypted,'commands':commands}

def add_library(data):
    info=macho(data)
    check(info['kind']==2,'Executable chinh khong dung loai MH_EXECUTE.')
    check(not info['encrypted'],'Executable khai bao ma hoa. Cong cu nay khong giai ma IPA.')
    if INSTALL_NAME in info['dependencies']:
        return data
    check(not any(n.endswith('/VTMenu.dylib') for n in info['dependencies']),
          'Da co VTMenu voi duong dan khac; dung lai tranh nap trung.')
    name=INSTALL_NAME.encode()+b'\0'
    size=(24+len(name)+7)&~7
    command=struct.pack('<6I',0xc,size,24,0,0x10000,0x10000)+name
    command=command.ljust(size,b'\0')
    offset=32+info['sizeofcmds']
    check(offset+size<=info['first_data'],'Khong du header padding. Khong di chuyen code de chen lenh.')
    check(data[offset:offset+size]==bytes(size),'Header padding khong rong; chua sua binary.')
    out=bytearray(data)
    out[offset:offset+size]=command
    struct.pack_into('<II',out,16,info['ncmd']+1,info['sizeofcmds']+size)
    allowed=set(range(16,24))|set(range(offset,offset+size))
    check(all(a==b or i in allowed for i,(a,b) in enumerate(zip(data,out))), 'Sua ngoai load command.')
    check(len(out)==len(data) and INSTALL_NAME in macho(out)['dependencies'],'Kiem tra executable sau chen that bai.')
    return bytes(out)

def safe_members(archive):
    names=[]
    for entry in archive.infolist():
        n=entry.filename
        path=PurePosixPath(n)
        check(not path.is_absolute() and '..' not in path.parts and '\\' not in n and '\x00' not in n,
              'Duong dan khong an toan trong IPA.')
        check(((entry.external_attr>>16)&0o170000)!=0o120000,'IPA co symlink; cong cu nay khong ho tro.')
        names.append(n)
    check(len(names)==len(set(names)),'IPA co ten file trung nhau.')
    return names

def package(ipa,dylib,manifest,output,skip_hash_check=False):
    ipa,dylib,manifest,output=map(lambda p:Path(p).resolve(),(ipa,dylib,manifest,output))
    check(output not in (ipa,dylib,manifest),'Dau ra phai khac file nguon.')
    check(not output.exists(),'File dau ra da ton tai. Chon ten moi; khong ghi de.')
    lib=dylib.read_bytes();libinfo=macho(lib)
    check(libinfo['kind']==6 and not libinfo['encrypted'],'Menu phai la ARM64 MH_DYLIB khong ma hoa.')
    meta=json.loads(manifest.read_text('utf-8-sig'))
    check(meta.get('menu_revision')==5 and meta.get('features')==['esp','spam_jump'],
          'Can dylib/manifest cua ban R5 chi ESP + Spam nhay. Hay build lai source R5.')
    check(meta.get('menu_sha256')==sha(lib),'Dylib/manifest khong khop SHA dylib.')
    if not skip_hash_check and meta.get('bundle') != 'com.haegin.playtogether':
        check(meta.get('framework_uuid')==EXPECTED_UUID,'Dylib/manifest khong khop ban build iOS nay.')
    check(meta.get('bundle') in EXPECTED_BUNDLES and meta.get('version')==EXPECTED_VERSION and
          meta.get('build')==EXPECTED_BUILD,'Manifest khac ban game.')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,temporary=tempfile.mkstemp(prefix=output.name+'.',suffix='.tmp',dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(ipa) as source:
            names=safe_members(source)
            plists=[n for n in names if len(PurePosixPath(n).parts)==3 and n.startswith('Payload/') and n.endswith('.app/Info.plist')]
            check(len(plists)==1,'IPA can dung mot app chinh trong Payload.')
            plist_name=plists[0]
            check(source.getinfo(plist_name).file_size<4*1024*1024,'Info.plist qua lon.')
            info=plistlib.loads(source.read(plist_name))
            check(info.get('CFBundleIdentifier') in EXPECTED_BUNDLES and
                  (skip_hash_check or (info.get('CFBundleShortVersionString')==EXPECTED_VERSION and str(info.get('CFBundleVersion'))==EXPECTED_BUILD)),
                  'Chi nhan PLAY TOGETHER VNG hoac Quoc te (Haegin) 2.32.0 (227628), bundle com.vng.playtogether, ptg.vng hoac com.haegin.playtogether.')
            is_global = (info.get('CFBundleIdentifier') == 'com.haegin.playtogether')
            app=plist_name.rsplit('/',1)[0]
            executable=info.get('CFBundleExecutable')
            check(isinstance(executable,str) and executable and '/' not in executable and '\\' not in executable,
                  'CFBundleExecutable khong hop le.')
            exe_name=app+'/'+executable
            framework=app+'/Frameworks/UnityFramework.framework/UnityFramework'
            target=app+'/Frameworks/VTMenu.dylib'
            check(target not in names,'IPA da co VTMenu.dylib. Hay dung IPA goc.')
            check(exe_name in names and framework in names,'IPA thieu executable/UnityFramework.')
            metadata=[n for n in names if n.startswith(app+'/') and n.endswith('/Managed/Metadata/global-metadata.dat')]
            check(len(metadata)==1,'Can dung mot global-metadata.dat trong IPA.')
            check(source.getinfo(metadata[0]).file_size<64*1024*1024,'Metadata qua lon.')
            if not is_global and not skip_hash_check:
                check(sha(source.read(metadata[0]))==EXPECTED_METADATA_SHA,'Metadata IPA khong khop file iOS da doi chieu.')
            check(source.getinfo(exe_name).file_size<64*1024*1024,'Executable launcher qua lon.')
            patched=add_library(source.read(exe_name))
            # Read only header/load commands from the large framework for UUID/encryption validation.
            with source.open(framework) as stream:
                header=stream.read(32)
                check(len(header)==32 and header[:4]==b'\xcf\xfa\xed\xfe','Framework khong phai Mach-O ARM64.')
                cpu=struct.unpack_from('<I',header,4)[0]
                ncmd,sz=struct.unpack_from('<II',header,16)
                check(cpu==0x100000c and ncmd<=4096 and sz<1024*1024,'Framework header khong hop le.')
                commands=stream.read(sz);check(len(commands)==sz,'Framework load commands bi cat.')
                pos=0;uuid=None
                for unused in range(ncmd):
                    check(pos+8<=sz,'Framework command bi cat.')
                    cmd,length=struct.unpack_from('<II',commands,pos)
                    check(length>=8 and pos+length<=sz,'Framework command khong hop le.')
                    if cmd==0x1b:check(length>=24,'UUID bi cat.');uuid=commands[pos+8:pos+24].hex()
                    if cmd in (0x21,0x2c):
                        check(length>=20 and struct.unpack_from('<I',commands,pos+16)[0]==0,
                              'Framework khai bao ma hoa. Cong cu nay khong giai ma.')
                    pos+=length
                if not is_global and not skip_hash_check:
                    check(pos==sz and uuid==EXPECTED_UUID,'Framework UUID khac ban dump da doi chieu.')
            # Stream large assets; no extraction of a 2 GB IPA is needed.
            with zipfile.ZipFile(temporary,'w',allowZip64=True) as dest:
                for entry in source.infolist():
                    target_info=copy.copy(entry)
                    if entry.filename==exe_name:
                        dest.writestr(target_info,patched)
                    else:
                        with source.open(entry) as reader,dest.open(target_info,'w',force_zip64=True) as writer:
                            shutil.copyfileobj(reader,writer,1024*1024)
                exe_info=source.getinfo(exe_name)
                entry=zipfile.ZipInfo(target,date_time=exe_info.date_time)
                entry.compress_type=zipfile.ZIP_DEFLATED;entry.create_system=exe_info.create_system
                entry.external_attr=(0o100755<<16)
                dest.writestr(entry,lib)
        with zipfile.ZipFile(temporary) as result:
            check(result.read(exe_name)==patched and result.read(target)==lib,'Kiem tra IPA dau ra that bai.')
        os.replace(temporary,output)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    return {'ipa':str(output),'signed':False,'menu_sha256':sha(lib),'note':'Can ky lai toan bo app/framework bang cong cu ky hop le truoc khi cai.'}

def choose(title,extension):
    import tkinter as tk
    from tkinter import filedialog
    window=tk.Tk();window.withdraw()
    try:selected=filedialog.askopenfilename(title=title,filetypes=[(extension,'*.'+extension),('All files','*.*')])
    finally:window.destroy()
    check(bool(selected),'Da huy, chua tao IPA.')
    return Path(selected)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ipa',type=Path);p.add_argument('--dylib',type=Path)
    p.add_argument('--manifest',type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--skip-hash-check',action='store_true',help='Bo qua kiem tra SHA metadata va UUID framework cho ban quoc te / custom')
    p.add_argument('--no-pause',action='store_true')
    a=p.parse_args()
    ipa=a.ipa or choose('Chon IPA game goc 2.32.0 / 227628','ipa')
    lib=a.dylib or choose('Chon VTMenu.dylib da build','dylib')
    manifest=a.manifest or lib.with_name('menu-build.json')
    output=a.output or ipa.with_name(ipa.stem+'-VTMenu-UNSIGNED.ipa')
    print(json.dumps(package(ipa,lib,manifest,output,skip_hash_check=True),ensure_ascii=False,indent=2))
    print('XONG: IPA CHUA KY. Can ky lai truoc khi cai; chua kiem thu menu tren may that.')

if __name__=='__main__':
    code=1
    try:main();code=0
    except Exception as e:print('LOI: '+str(e),file=sys.stderr)
    finally:
        if '--no-pause' not in sys.argv and sys.stdin is not None and sys.stdin.isatty():
            try:input('Bam Enter de dong...')
            except (EOFError,KeyboardInterrupt):pass
    raise SystemExit(code)
