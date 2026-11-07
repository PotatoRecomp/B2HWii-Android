#!/usr/bin/env python3
"""Read guest memory of the running Windows build (WiiCompiled.exe, guest at 0x100000000000 + address).

python -I tools/b-peek.py ADDR SIZE [OUT.bin]
Prints a hex dump (16 bytes per line), or writes the bytes to OUT.bin.
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys

GUEST_BASE = 0x100000000000
PROCESS_VM_READ, PROCESS_QUERY_INFORMATION = 0x10, 0x400


def find_pid():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WiiCompiled.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.strip('"').split('","')
        if len(parts) > 1 and parts[0].lower() == 'wiicompiled.exe':
            return int(parts[1])
    sys.exit('WiiCompiled.exe is not running')


def read(pid, addr, size):
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.OpenProcess.restype = wt.HANDLE
    k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                      ctypes.POINTER(ctypes.c_size_t)]
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        sys.exit('OpenProcess failed: %d' % ctypes.get_last_error())
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(GUEST_BASE + addr), buf, size, ctypes.byref(got)):
        sys.exit('ReadProcessMemory failed: %d' % ctypes.get_last_error())
    k32.CloseHandle(h)
    return buf.raw[:got.value]


addr = int(sys.argv[1], 16)
data = read(find_pid(), addr, int(sys.argv[2], 0))
if len(sys.argv) > 3:
    open(sys.argv[3], 'wb').write(data)
else:
    for off in range(0, len(data), 16):
        print('%08X  %s' % (addr + off, data[off:off + 16].hex(' ')))
