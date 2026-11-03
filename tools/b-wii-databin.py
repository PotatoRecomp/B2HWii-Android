"""Build a Wii SD-export data.bin (layout of Dolphin's Core/WiiSave.cpp) from a NAND save folder.

Used to test the launcher's Import Save on data.bin files:
    python -I tools/b-wii-databin.py SAVE_DIR OUT.bin
SAVE_DIR holds basara2w.dat (and banner.bin when there). Certificates are zero-filled.
"""
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

SD_KEY = bytes.fromhex("ab01b9d8e1622b08afbad84dbfc2a55d")
SD_IV = bytes.fromhex("216712e6aa1f689f95c5a22324dc6a98")
TITLE_ID = 0x000100005242534A


def enc(data, iv):
    e = Cipher(algorithms.AES(SD_KEY), modes.CBC(iv)).encryptor()
    return e.update(data) + e.finalize()


def main(save_dir, out):
    save = open(os.path.join(save_dir, "basara2w.dat"), "rb").read()
    banner_path = os.path.join(save_dir, "banner.bin")
    banner = open(banner_path, "rb").read() if os.path.isfile(banner_path) else b"WIBN" + bytes(0xA0 - 4)
    header = struct.pack(">QIBB16sH", TITLE_ID, len(banner), 0x3C, 0, bytes(16), 0) + banner
    header = header.ljust(0xF0C0, b"\0")
    padded = save.ljust((len(save) + 0x3F) & ~0x3F, b"\0")
    iv = os.urandom(16)
    fhdr = struct.pack(">IIBBB", 0x03ADF17E, len(save), 0x3C, 0, 1) + b"basara2w.dat".ljust(0x45, b"\0") + iv
    fhdr = fhdr.ljust(0x80, b"\0")
    bk = struct.pack(">IIIII", 0x70, 0x426B0001, 0, 1, len(fhdr) + len(padded)).ljust(0x80, b"\0")
    blob = enc(header, SD_IV) + bk + fhdr + enc(padded, iv) + bytes(0x340)
    open(out, "wb").write(blob)
    print(out, len(blob))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
