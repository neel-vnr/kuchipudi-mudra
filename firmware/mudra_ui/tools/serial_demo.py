"""Send sample MUDRA AI newline-delimited JSON messages to the ESP32."""
import argparse, json, time
import serial

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='COM9');p.add_argument('--baud',type=int,default=115200);a=p.parse_args()
    with serial.Serial(a.port,a.baud,timeout=.1) as s:
        time.sleep(2)
        for msg in [
            {'event':'connection','status':'connected'},
            {'event':'camera','status':'active'},
            {'event':'model','status':'connected'},
            {'event':'detection','mudra':'Pataka','confidence':96.4,'status':'recognized'},
            {'event':'detection','status':'no_hand'},
        ]:
            s.write((json.dumps(msg,separators=(',',':'))+'\n').encode());time.sleep(2)
        print('Demo messages sent. Incoming device messages:')
        end=time.time()+15
        while time.time()<end:
            line=s.readline().decode(errors='replace').strip()
            if line: print(line)
if __name__=='__main__': main()
