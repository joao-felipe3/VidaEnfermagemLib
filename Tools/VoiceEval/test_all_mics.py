import ctypes
import time

winmm = ctypes.windll.winmm

class WAVEINCAPS(ctypes.Structure):
    _fields_ = [
        ('wMid', ctypes.c_ushort),
        ('wPid', ctypes.c_ushort),
        ('vDriverVersion', ctypes.c_uint),
        ('szPname', ctypes.c_char * 32),
        ('dwFormats', ctypes.c_uint),
        ('wChannels', ctypes.c_ushort),
        ('wReserved1', ctypes.c_ushort),
    ]

num = winmm.waveInGetNumDevs()
print(f"Total recording devices: {num}")
for i in range(num):
    caps = WAVEINCAPS()
    winmm.waveInGetDevCapsA(i, ctypes.byref(caps), ctypes.sizeof(caps))
    name = caps.szPname.decode("latin1", errors="replace")
    print(f"[{i}] {name}")

