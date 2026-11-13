#!/usr/bin/env python3
"""Real OpenSSL signature and release-package regression; no device or network."""
import base64
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('release',ROOT/'tools/package_release.py');release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)
class ReleaseTest(unittest.TestCase):
    def test_signature_and_recovery_tampering(self):
        with tempfile.TemporaryDirectory(prefix='pet-release-test-') as directory:
            temp=Path(directory);key=temp/'private.pem';public=temp/'public.der';build=temp/'build';build.mkdir()
            release.openssl('genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',key);key.chmod(0o600)
            release.openssl('rsa','-in',key,'-RSAPublicKey_out','-outform','DER','-out',public)
            partitions=ROOT/'firmware/partitions/16m.csv';hash_value=release.digest(partitions.read_bytes())
            app=bytearray(512);app[0]=0xE9;struct.pack_into('<I',app,32,0xABCD5432);app[48:53]=b'0.4.0';app[80:89]=b'nexus_pet'
            app.extend(f'PET04_ID:nexus_pet:dodo-s3-lcd:none:{hash_value}:END'.encode());(build/'nexus_pet.bin').write_bytes(app)
            for name in ['bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin']:
                file=build/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(b'host-boundary-fixture')
            package=temp/'package';release.create(build,key,12,package,partitions)
            self.assertEqual(release.verify(package,public)['releaseSequence'],12)
            plan=package/'usb/recovery-plan.json';saved_plan=plan.read_bytes();plan.unlink()
            with self.assertRaises(ValueError):release.verify(package,public)
            plan.write_bytes(saved_plan)
            (build/'sdkconfig').write_text('CONFIG_PET_OTA_ENABLED=y\n')
            with self.assertRaises(ValueError):release.create(build,key,13,temp/'wrong-key',partitions)
            (build/'nexus_pet.bin').write_bytes(bytes(app)+public.read_bytes())
            self.assertEqual(release.create(build,key,13,temp/'matching-key',partitions)['releaseSequence'],13)
            (build/'nexus_pet.bin').write_bytes(app)
            (build/'sdkconfig').unlink()
            with self.assertRaises(ValueError):release.create(build,key,13,package,partitions)
            image=package/'firmware.bin';saved=image.read_bytes();image.write_bytes(saved[:-1])
            with self.assertRaises(ValueError):release.verify(package,public)
            image.write_bytes(saved)
            plan=package/'usb/recovery-plan.json';saved_plan=plan.read_bytes();plan.write_bytes(saved_plan.replace(b'131072',b'65536'))
            with self.assertRaises(subprocess.CalledProcessError):release.verify(package,public)
            plan.write_bytes(saved_plan)
            boot=package/'usb/bootloader.bin';boot.write_bytes(b'corrupted')
            with self.assertRaises(ValueError):release.verify(package,public)
            boot.write_bytes(b'host-boundary-fixture')
            manifest=package/'manifest.json';value=json.loads(manifest.read_text());sig=bytearray(base64.b64decode(value['signature']));sig[3]^=1;value['signature']=base64.b64encode(sig).decode();manifest.write_text(json.dumps(value))
            with self.assertRaises(subprocess.CalledProcessError):release.verify(package,public)
if __name__=='__main__':unittest.main()
