# Reboot the ESP32-S3 into the app through USB-Serial/JTAG control lines and print its output.
import serial, time, glob, sys
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.2)
s.dtr = False; s.rts = False; time.sleep(0.1)   # GPIO0 released (RTS write propagates DTR)
s.rts = True; time.sleep(0.12); s.rts = False    # EN pulse -> normal boot
t = time.time(); out = b''
while time.time() - t < secs:
    try:
        out += s.read(4096)
    except Exception:
        time.sleep(0.3)
        try:
            s.close(); s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.2)
        except Exception: pass
print(out.decode(errors='replace')[-1500:] or '(nothing)')
