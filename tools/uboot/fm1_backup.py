# FM-1 (WL82) UBOOT: read-only backup of the whole 1 MiB SPI NOR.
# Writes nothing to flash. Run from an administrator PowerShell.
import sys, os, hashlib, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'jl-uboot-tool'))
from jltech.uboot import JL_MSCDevice, JL_UBOOT, JL_LoaderV2
from jltech.cipher import cipher_bytes, jl_crc_cipher
import subprocess

def find_drive():
    out = subprocess.check_output(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_DiskDrive | Where-Object { $_.PNPDeviceID -like 'USBSTOR\\DISK&VEN_WL82&PROD_UBOOT1.00*' } | ForEach-Object { $_.Index }"], text=True)
    idx = [l.strip() for l in out.splitlines() if l.strip()]
    if len(idx) != 1:
        sys.exit(f'WL82 UBOOT1.00 disk not found exactly once: {idx}')
    return r'\\.\PhysicalDrive' + idx[0]

path = find_drive()
print('device:', path)
with JL_MSCDevice(path) as dev:
    vendor, product, rev = dev.inquiry()
    print('inquiry:', vendor, product, rev)
    if vendor != 'WL82' or product not in ('UBOOT1.00',):
        sys.exit('not the FM-1 in UBOOT1.00: stopping')
    rom = JL_UBOOT(dev.dev)
    blob = open(os.path.join(HERE, 'jl-uboot-tool', 'data', 'loaderblobs', 'usb', 'wl82loader.bin'), 'rb').read()
    addr = 0x1C02000
    for i in range(0, len(blob), 512):
        rom.mem_write(addr + i, blob[i:i + 512])      # WL82 ROM: raw (already MengLi-ciphered) blocks
    rom.mem_jump(addr, 0x0001)
    print('loader running')
    ld = JL_LoaderV2(dev.dev)
    key = ld.chip_key()
    od = ld.online_device()
    print(f'chip key 0x{key:04X}, flash type {od["type"]} id 0x{od["id"]:06X}')
    if od['type'] != 3 or od['id'] != 0x856014:
        sys.exit('unexpected flash: stopping')
    imgs = []
    for p in range(2):
        t = time.time(); buf = bytearray()
        for a in range(0, 0x100000, 512):
            buf += ld.flash_read(a, 512)
        imgs.append(bytes(buf))
        print(f'pass {p + 1}: {len(buf)} B in {time.time() - t:.1f} s, sha256 {hashlib.sha256(buf).hexdigest()}')
    if imgs[0] != imgs[1]:
        sys.exit('the two reads differ: not saved')
    out = os.path.join(HERE, time.strftime('fm1_backup_%Y%m%d_%H%M%S.bin'))
    open(out, 'wb').write(imgs[0])
    print('saved', out)
print('done (the FM-1 stays in the loader: power it off and on to leave)')
