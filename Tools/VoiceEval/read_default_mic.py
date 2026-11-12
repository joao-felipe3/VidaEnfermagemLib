import winreg

def get_default_audio_device():
    # In Windows 10/11, audio endpoints are in HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture
    path = r"SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture"
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path)
        count = winreg.QueryInfoKey(key)[0]
        print(f"Total capture endpoints in registry: {count}")
        for i in range(count):
            subkey_name = winreg.EnumKey(key, i)
            subkey = winreg.OpenKey(key, subkey_name)
            try:
                state, _ = winreg.QueryValueEx(subkey, "DeviceState")
                # 1 = DEVICE_STATE_ACTIVE
                prop_key = winreg.OpenKey(subkey, "Properties")
                name, _ = winreg.QueryValueEx(prop_key, "{a45c254e-df1c-4efd-8020-67d146a850e0},2")
                desc, _ = winreg.QueryValueEx(prop_key, "{a45c254e-df1c-4efd-8020-67d146a850e0},14")
                print(f"  [{subkey_name}] State={state} Name='{name}' Desc='{desc}'")
            except Exception as e:
                pass
    except Exception as e:
        print("Error reading registry:", e)

get_default_audio_device()

