import ctypes
from ctypes import wintypes
import time
import math

winmm = ctypes.windll.winmm

class WAVEFORMATEX(ctypes.Structure):
    _fields_ = [
        ('wFormatTag', wintypes.WORD),
        ('nChannels', wintypes.WORD),
        ('nSamplesPerSec', wintypes.DWORD),
        ('nAvgBytesPerSec', wintypes.DWORD),
        ('nBlockAlign', wintypes.WORD),
        ('wBitsPerSample', wintypes.WORD),
        ('cbSize', wintypes.WORD)
    ]

class WAVEHDR(ctypes.Structure):
    pass

WAVEHDR._fields_ = [
    ('lpData', ctypes.POINTER(ctypes.c_char)),
    ('dwBufferLength', wintypes.DWORD),
    ('dwBytesRecorded', wintypes.DWORD),
    ('dwUser', ctypes.c_void_p),
    ('dwFlags', wintypes.DWORD),
    ('dwLoops', wintypes.DWORD),
    ('lpNext', ctypes.POINTER(WAVEHDR)),
    ('reserved', ctypes.c_void_p)
]

class WAVEINCAPS(ctypes.Structure):
    _fields_ = [
        ('wMid', wintypes.WORD),
        ('wPid', wintypes.WORD),
        ('vDriverVersion', wintypes.DWORD),
        ('szPname', ctypes.c_char * 32),
        ('dwFormats', wintypes.DWORD),
        ('wChannels', wintypes.WORD),
        ('wReserved1', wintypes.WORD),
    ]

num_devs = winmm.waveInGetNumDevs()
print(f"Detectando áudio nos {num_devs} dispositivos de entrada (gravando 1s de teste em cada)...")

wfx = WAVEFORMATEX()
wfx.wFormatTag = 1 # PCM
wfx.nChannels = 1
wfx.nSamplesPerSec = 16000
wfx.wBitsPerSample = 16
wfx.nBlockAlign = 2
wfx.nAvgBytesPerSec = 32000
wfx.cbSize = 0

for dev_id in range(num_devs):
    caps = WAVEINCAPS()
    winmm.waveInGetDevCapsA(dev_id, ctypes.byref(caps), ctypes.sizeof(caps))
    name = caps.szPname.decode("latin1", errors="replace")

    hWaveIn = wintypes.HANDLE()
    res = winmm.waveInOpen(ctypes.byref(hWaveIn), dev_id, ctypes.byref(wfx), 0, 0, 0)
    if res != 0:
        print(f"Device [{dev_id}] '{name}': Erro ao abrir ({res})")
        continue

    buf_size = 16000 * 2 # 1 segundo
    buf = (ctypes.c_char * buf_size)()
    hdr = WAVEHDR()
    hdr.lpData = ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))
    hdr.dwBufferLength = buf_size
    hdr.dwFlags = 0

    winmm.waveInPrepareHeader(hWaveIn, ctypes.byref(hdr), ctypes.sizeof(hdr))
    winmm.waveInAddBuffer(hWaveIn, ctypes.byref(hdr), ctypes.sizeof(hdr))
    winmm.waveInStart(hWaveIn)

    time.sleep(1.1)

    winmm.waveInStop(hWaveIn)
    winmm.waveInReset(hWaveIn)
    winmm.waveInUnprepareHeader(hWaveIn, ctypes.byref(hdr), ctypes.sizeof(hdr))
    winmm.waveInClose(hWaveIn)

    recorded_bytes = hdr.dwBytesRecorded
    raw_data = bytes(buf)[:recorded_bytes]
    samples = [int.from_bytes(raw_data[i:i+2], 'little', signed=True) for i in range(0, len(raw_data), 2)]
    
    if samples:
        max_val = max(abs(s) for s in samples)
        rms = math.sqrt(sum(s*s for s in samples) / len(samples))
        print(f"Device [{dev_id}] '{name}': Gravou {recorded_bytes} bytes | Pico={max_val:5d} ({max_val*100/32767:.1f}%) | RMS={rms:.1f}")
    else:
        print(f"Device [{dev_id}] '{name}': 0 bytes gravados.")

