import winreg

path = r"SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture"
key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path)
count = winreg.QueryInfoKey(key)[0]
for i in range(count):
    subkey_name = winreg.EnumKey(key, i)
    subkey = winreg.OpenKey(key, subkey_name)
    try:
        state, _ = winreg.QueryValueEx(subkey, "DeviceState")
        if state == 1: # ACTIVE
            prop_key = winreg.OpenKey(subkey, "Properties")
            try:
                name, _ = winreg.QueryValueEx(prop_key, "{a45c254e-df1c-4efd-8020-67d146a850e0},2")
            except:
                name = ""
            try:
                desc, _ = winreg.QueryValueEx(prop_key, "{b3f8fa53-0004-438e-9003-51a46e139bfc},6")
            except:
                desc = ""
            try:
                friendly, _ = winreg.QueryValueEx(prop_key, "{a45c254e-df1c-4efd-8020-67d146a850e0},14")
            except:
                friendly = ""
            print(f"ACTIVE: {subkey_name} | Friendly='{friendly}' | Name='{name}' | Desc='{desc}'")
    except Exception:
        pass

