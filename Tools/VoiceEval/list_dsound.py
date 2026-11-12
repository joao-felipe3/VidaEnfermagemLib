import ctypes

dsound = ctypes.windll.dsound
DSEnumCallback = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p)

def callback(guid, desc, mod, ctx):
    g_str = 'None (Default)' if not guid else 'GUID'
    d_str = desc.decode('latin1', errors='replace') if desc else ''
    m_str = mod.decode('latin1', errors='replace') if mod else ''
    print(f'DS Device: desc="{d_str}" mod="{m_str}" guid={g_str}')
    return True

cb = DSEnumCallback(callback)
dsound.DirectSoundCaptureEnumerateA(cb, 0)

