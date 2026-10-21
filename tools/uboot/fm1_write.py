# FM-1 (WL82) UBOOT: write a firmware's app area [0x4000, 0x93000) -- exactly what Felucca's update loader writes.
# The flash head [0, 0x4000) and the data area from 0x93000 are never touched.
#   python fm1_write.py TARGET_FLASH.bin BACKUP.bin            dry run: checks and lists the sectors, writes nothing
#   python fm1_write.py TARGET_FLASH.bin BACKUP.bin --write    erase + write + read back each changed 4 KiB sector
# Run from an administrator PowerShell, with the FM-1 freshly in UBOOT (WL82 UBOOT1.00).
import sys, os, hashlib, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'jl-uboot-tool'))
from jltech.uboot import JL_MSCDevice, JL_UBOOT, JL_LoaderV2

LO, HI, SEC, FLASH = 0x4000, 0x93000, 0x1000, 0x100000

def find_drive():
    out = subprocess.check_output(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_DiskDrive | Where-Object { $_.PNPDeviceID -like 'USBSTOR\\DISK&VEN_WL82&PROD_UBOOT1.00*' } | ForEach-Object { $_.Index }"], text=True)
    idx = [l.strip() for l in out.splitlines() if l.strip()]
    if len(idx) != 1:
        sys.exit(f'WL82 UBOOT1.00 disk not found exactly once: {idx}')
    return r'\\.\PhysicalDrive' + idx[0]

def read_all(ld, n=FLASH):
    buf = bytearray()
    for a in range(0, n, 512):
        buf += ld.flash_read(a, 512)
    return bytes(buf)

args = [a for a in sys.argv[1:] if not a.startswith('--')]
do_write = '--write' in sys.argv
if len(args) != 2:
    sys.exit('usage: fm1_write.py TARGET_FLASH.bin BACKUP.bin [--write]')
target = open(args[0], 'rb').read()
backup = open(args[1], 'rb').read()
print(f'target {args[0]}: {len(target):#x} B sha256 {hashlib.sha256(target).hexdigest()}')
print(f'backup {args[1]}: {len(backup):#x} B sha256 {hashlib.sha256(backup).hexdigest()}')
if len(target) != HI or len(backup) != FLASH:
    sys.exit('unexpected sizes (target must be 0x93000, backup 1 MiB): stopping')
diff = [a for a in range(LO, HI, SEC) if target[a:a + SEC] != backup[a:a + SEC]]
print(f'{len(diff)} sectors of the app area differ from the backup' + (f': {diff[0]:#x} .. {diff[-1]:#x}' if diff else ''))

path = find_drive()
print('device:', path)
with JL_MSCDevice(path) as dev:
    vendor, product, rev = dev.inquiry()
    if vendor != 'WL82' or product != 'UBOOT1.00':
        sys.exit(f'not the FM-1 in UBOOT1.00 ({vendor} {product}): stopping')
    rom = JL_UBOOT(dev.dev)
    blob = open(os.path.join(HERE, 'jl-uboot-tool', 'data', 'loaderblobs', 'usb', 'wl82loader.bin'), 'rb').read()
    for i in range(0, len(blob), 512):
        rom.mem_write(0x1C02000 + i, blob[i:i + 512])
    rom.mem_jump(0x1C02000, 0x0001)
    ld = JL_LoaderV2(dev.dev)
    key, od = ld.chip_key(), ld.online_device()
    print(f'chip key 0x{key:04X}, flash type {od["type"]} id 0x{od["id"]:06X}')
    if key != 0x980F or od['type'] != 3 or od['id'] != 0x856014:
        sys.exit('unexpected chip key / flash: stopping')
    print('reading the whole flash ...')
    now = read_all(ld)
    if now[:HI] != backup[:HI]:
        bad = [a for a in range(0, HI, SEC) if now[a:a + SEC] != backup[a:a + SEC]]
        sys.exit(f'the flash differs from the backup in [0, {HI:#x}) at {[hex(a) for a in bad[:8]]}: not writing')
    print(f'the flash equals the backup over [0, {HI:#x})')
    if not do_write:
        print(f'dry run: nothing written (add --write to write {len(diff)} sectors)')
        sys.exit(0)
    for n, a in enumerate(diff, 1):
        sec = target[a:a + SEC]
        for attempt in range(1, 4):
            ld.flash_erase_sector(a)
            for o in range(0, SEC, 512):
                ld.flash_write(a + o, sec[o:o + 512])
            back = b''.join(ld.flash_read(a + o, 512) for o in range(0, SEC, 512))
            if back == sec:
                break
            print(f'  sector {a:#07x}: read-back differs (attempt {attempt})')
        else:
            sys.exit(f'sector {a:#07x} would not verify: the FM-1 stays in the loader; run this again to retry')
        print(f'  [{n}/{len(diff)}] sector {a:#07x} written and verified')
    print('final read of the whole flash ...')
    final = read_all(ld)
    expect = now[:LO] + target[LO:] + now[HI:]          # the head is never written: it stays the device's own
    if final != expect:
        bad = [a for a in range(0, FLASH, SEC) if final[a:a + SEC] != expect[a:a + SEC]]
        sys.exit(f'final read DIFFERS at {[hex(a) for a in bad[:8]]}: run this again')
    print('final read equals the expected image: done. Power the FM-1 off and on.')
