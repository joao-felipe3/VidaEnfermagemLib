import winreg

try:
    key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Multimedia\Sound Mapper")
    rec, _ = winreg.QueryValueEx(key, "Record")
    print("Sound Mapper Record Default Device:", rec)
except Exception as e:
    print("Sound Mapper error:", e)

