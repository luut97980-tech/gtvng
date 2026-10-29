import importlib.util
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec=importlib.util.spec_from_file_location('ipa_tools',Path(__file__).parents[1]/'tao_ipa_menu.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def binary(kind=2,uuid=None,cryptid=0,padding=True):
    # One executable section, optional UUID, and encryption command.
    sec=struct.pack('<16s16sQQIIIIIIII',b'__text',b'__TEXT',4096,16,4096,2,0,0,0x80000400,0,0,0)
    seg=struct.pack('<II16sQQQQIIII',0x19,152,b'__TEXT',0,8192,0,4112,7,5,1,0)+sec
    crypt=struct.pack('<6I',0x2c,24,4096,16,cryptid,0)
    commands=seg+crypt
    if uuid:commands+=struct.pack('<II',0x1b,24)+bytes.fromhex(uuid)
    header=struct.pack('<8I',0xfeedfacf,0x100000c,0,kind,2+bool(uuid),len(commands),0,0)
    data=(header+commands).ljust(4096,b'\0')+b'codebytes0123456'
    if not padding:data=bytearray(data);data[32+len(commands)]=7;data=bytes(data)
    return data

class IpaTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.ipa=self.root/'game.ipa';self.out=self.root/'menu.ipa'
        self.lib=self.root/'VTMenu.dylib';self.lib.write_bytes(binary(6))
        self.manifest=self.root/'menu-build.json'
        self.manifest.write_text(json.dumps({'menu_revision':5,'features':['esp','spam_jump'],'bundle':m.EXPECTED_BUNDLES[0],'version':m.EXPECTED_VERSION,
            'build':m.EXPECTED_BUILD,'framework_uuid':m.EXPECTED_UUID,'menu_sha256':m.sha(self.lib.read_bytes())}))
        self.meta=b'ios metadata fixture';self.metaPatch=patch.object(m,'EXPECTED_METADATA_SHA',m.sha(self.meta));self.metaPatch.start();self.addCleanup(self.metaPatch.stop)
        self.entries={
          'Payload/Game.app/Info.plist':plistlib.dumps({'CFBundleIdentifier':m.EXPECTED_BUNDLES[0],
            'CFBundleShortVersionString':m.EXPECTED_VERSION,'CFBundleVersion':m.EXPECTED_BUILD,'CFBundleExecutable':'PLAYTOGETHERVNG'}),
          'Payload/Game.app/PLAYTOGETHERVNG':binary(),
          'Payload/Game.app/Frameworks/UnityFramework.framework/UnityFramework':binary(6,m.EXPECTED_UUID),
          'Payload/Game.app/Data/Managed/Metadata/global-metadata.dat':self.meta,
          'Payload/Game.app/Data/asset.bin':b'game asset unchanged',
        }
        self.write()
    def write(self):
        with zipfile.ZipFile(self.ipa,'w',zipfile.ZIP_DEFLATED) as z:
            for n,b in self.entries.items():z.writestr(n,b)
    def run_package(self):return m.package(self.ipa,self.lib,self.manifest,self.out)
    def test_complete_streaming_package(self):
        before=self.ipa.read_bytes();report=self.run_package();self.assertFalse(report['signed'])
        self.assertEqual(self.ipa.read_bytes(),before)
        with zipfile.ZipFile(self.out) as z:
            self.assertIsNone(z.testzip());self.assertEqual(len(z.namelist()),6)
            for n,b in self.entries.items():
                if n.endswith('/PLAYTOGETHERVNG'):self.assertIn(m.INSTALL_NAME,m.macho(z.read(n))['dependencies'])
                else:self.assertEqual(z.read(n),b)
            self.assertEqual(z.read('Payload/Game.app/Frameworks/VTMenu.dylib'),self.lib.read_bytes())
    def test_alt_vng_bundle_accepted(self):
        n='Payload/Game.app/Info.plist';d=plistlib.loads(self.entries[n])
        d['CFBundleIdentifier']=m.EXPECTED_BUNDLES[1];self.entries[n]=plistlib.dumps(d);self.write()
        self.run_package();self.assertTrue(self.out.exists())
    def test_global_bundle_accepted(self):
        n='Payload/Game.app/Info.plist';d=plistlib.loads(self.entries[n])
        d['CFBundleIdentifier']='com.haegin.playtogether';self.entries[n]=plistlib.dumps(d);self.write()
        self.run_package();self.assertTrue(self.out.exists())
    def test_patch_only_changes_header_and_is_idempotent(self):
        original=binary();patched=m.add_library(original)
        self.assertEqual(patched[4096:],original[4096:]);self.assertEqual(len(patched),len(original))
        self.assertEqual(m.add_library(patched),patched)
    def test_encryption_refused(self):
        self.entries['Payload/Game.app/PLAYTOGETHERVNG']=binary(cryptid=1);self.write()
        with self.assertRaisesRegex(ValueError,'ma hoa'):self.run_package()
        self.assertFalse(self.out.exists())
    def test_framework_encryption_refused(self):
        self.entries['Payload/Game.app/Frameworks/UnityFramework.framework/UnityFramework']=binary(6,m.EXPECTED_UUID,1);self.write()
        with self.assertRaisesRegex(ValueError,'ma hoa'):self.run_package()
    def test_nonempty_padding_refused(self):
        with self.assertRaisesRegex(ValueError,'padding khong rong'):m.add_library(binary(padding=False))
    def test_metadata_mismatch_refused(self):
        self.entries['Payload/Game.app/Data/Managed/Metadata/global-metadata.dat']=b'wrong';self.write()
        with self.assertRaisesRegex(ValueError,'Metadata IPA khong khop'):self.run_package()
    def test_framework_uuid_mismatch_refused(self):
        self.entries['Payload/Game.app/Frameworks/UnityFramework.framework/UnityFramework']=binary(6,'00'*16);self.write()
        with self.assertRaisesRegex(ValueError,'UUID khac'):self.run_package()
    def test_library_hash_refused(self):
        self.lib.write_bytes(binary(6)+b'altered')
        with self.assertRaisesRegex(ValueError,'manifest khong khop'):self.run_package()
    def test_old_menu_revision_refused(self):
        meta=json.loads(self.manifest.read_text());meta['menu_revision']=4;self.manifest.write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError,'ban R5'):self.run_package()

    def test_unknown_version_refused(self):
        n='Payload/Game.app/Info.plist';d=plistlib.loads(self.entries[n]);d['CFBundleVersion']='other'
        self.entries[n]=plistlib.dumps(d);self.write()
        with self.assertRaisesRegex(ValueError,'Chi nhan'):self.run_package()
    def test_duplicate_injection_refused(self):
        self.entries['Payload/Game.app/Frameworks/VTMenu.dylib']=self.lib.read_bytes();self.write()
        with self.assertRaisesRegex(ValueError,'da co VTMenu'):self.run_package()
    def test_path_traversal_refused(self):
        self.entries['../outside']=b'bad';self.write()
        with self.assertRaisesRegex(ValueError,'Duong dan khong an toan'):self.run_package()
    def test_existing_output_preserved(self):
        self.out.write_bytes(b'keep')
        with self.assertRaisesRegex(ValueError,'da ton tai'):self.run_package()
        self.assertEqual(self.out.read_bytes(),b'keep')

if __name__=='__main__':unittest.main()
