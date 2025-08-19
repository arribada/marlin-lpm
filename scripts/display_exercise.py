#!/usr/bin/env python3
import os
import sys
import serial
import time
import subprocess
from pathlib import Path

try:
    from gen import core_pb2
except ImportError:
    print("core_pb2 module not found. Compiling from .proto file...")
    current_dir = os.path.dirname(os.path.realpath(sys.argv[0]))
    generated_dir = os.path.join(current_dir, 'gen')
    os.makedirs(generated_dir, exist_ok=True)

    current_dir_path = Path(current_dir)
    proto_file = os.path.join(current_dir_path.parent.absolute(), 'proto', 'core.proto')

    if not os.path.exists(proto_file):
        print(f"Error: core.proto not found at {proto_file}")
        sys.exit(1)

    try:
        subprocess.run(['protoc', '--version'],
                      check=True,
                      stdout=subprocess.PIPE,
                      stderr=subprocess.PIPE)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Error: Protocol Buffers compiler (protoc) not found")
        sys.exit(1)

    try:
        subprocess.run([
            'protoc',
            f'--proto_path={os.path.dirname(proto_file)}',
            f'--python_out={generated_dir}',
            proto_file
        ], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        print(f"protoc compilation failed: {e.stderr.strip()}")
        sys.exit(1)

    if 'core_pb2' in sys.modules:
        del sys.modules['core_pb2']

    try:
        from gen import core_pb2
    except ImportError as e:
        print(f"Error: Failed to import core_pb2 after compilation: {e}")
        sys.exit(1)


def send_sys_init(ser, hostname="satellite-001"):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = 1

    init = core_pb2.SysInit()
    init.id = "sat-001"
    init.addr = "192.168.1.100"
    init.sec_key = "supersecretkey123"
    init.rconf = "/etc/sat/config.json"
    init.hostname = hostname
    init.wifi_network = "Starlink-Guest"
    core_msg.init.CopyFrom(init)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"  Sent SysInit  hostname=\"{hostname}\"  ({len(serialized)} bytes)")


def send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=25.0,
                     tailscale_ip="100.64.0.1"):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = 2

    status = core_pb2.CoreStatus()
    status.camera = camera
    status.controller = controller
    status.cloud_connection = cloud_connection
    status.cellular = cellular
    status.wifi = wifi
    status.gps = gps
    status.tailscale = tailscale
    status.local_dash = local_dash
    status.storage_usage = storage_usage
    status.tailscale_ip = tailscale_ip
    core_msg.status.CopyFrom(status)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)

    names = ["CAM", "CTRL", "CLOUD", "CELL", "WIFI", "GPS", "TS", "DASH"]
    vals = [camera, controller, cloud_connection, cellular, wifi, gps, tailscale, local_dash]
    labels = []
    for n, v in zip(names, vals):
        labels.append(f"{n}={core_pb2.CoreStatus.Status.Name(v)}")
    print(f"  Sent CoreStatus  [{', '.join(labels)}]  storage={storage_usage:.0f}%  TS={tailscale_ip}  ({len(serialized)} bytes)")


def send_harbour_mode(ser, enter=True):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = 3

    mode = core_pb2.HarbourMode()
    mode.cmd = core_pb2.HarbourMode.ENTER if enter else core_pb2.HarbourMode.EXIT
    core_msg.mode.CopyFrom(mode)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"  Sent HarbourMode  cmd=\"{core_pb2.HarbourMode.Command.Name(mode.cmd)}\"  ({len(serialized)} bytes)")


def send_sat_message(ser):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = 4

    sat = core_pb2.SatMessage()
    sat.message = "Hello from satellite"
    core_msg.sat.CopyFrom(sat)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"  Sent SatMessage  msg_id={core_msg.msg_id}  ({len(serialized)} bytes)")


def main():
    if len(sys.argv) != 2:
        print("Usage: python display_exercise.py <serial_device>")
        print("Example: python display_exercise.py /dev/ttyXRUSB0")
        sys.exit(1)

    serial_device = sys.argv[1]

    try:
        ser = serial.Serial(
            port=serial_device,
            baudrate=115200,
            timeout=2
        )
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    print(f"Connected to {serial_device}")
    print("Watch the display to see each effect.")

    send_sys_init(ser, hostname="marlin-001")
    time.sleep(10)

    send_sat_message(ser)
    time.sleep(2)
    send_sat_message(ser)
    time.sleep(2)
    send_sat_message(ser)

    send_core_status(ser,
                     camera=core_pb2.CoreStatus.OFFLINE,
                     controller=core_pb2.CoreStatus.OFFLINE,
                     cloud_connection=core_pb2.CoreStatus.OFFLINE,
                     cellular=core_pb2.CoreStatus.OFFLINE,
                     wifi=core_pb2.CoreStatus.OFFLINE,
                     gps=core_pb2.CoreStatus.OFFLINE,
                     tailscale=core_pb2.CoreStatus.OFFLINE,
                     local_dash=core_pb2.CoreStatus.OFFLINE,
                     storage_usage=50.0,
                     tailscale_ip="100.64.0.1")

    time.sleep(10)


    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.OFFLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ERROR,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.UNKNOWN,
                     storage_usage=45.2,
                     tailscale_ip="100.64.0.42")

    time.sleep(10)

    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=12.0,
                     tailscale_ip="100.64.0.42")

    time.sleep(10)
    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ERROR,
                     controller=core_pb2.CoreStatus.ERROR,
                     cloud_connection=core_pb2.CoreStatus.ERROR,
                     cellular=core_pb2.CoreStatus.ERROR,
                     wifi=core_pb2.CoreStatus.ERROR,
                     gps=core_pb2.CoreStatus.ERROR,
                     tailscale=core_pb2.CoreStatus.ERROR,
                     local_dash=core_pb2.CoreStatus.ERROR,
                     storage_usage=96.0,
                     tailscale_ip="100.64.0.42")

    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=96.0,
                     tailscale_ip="100.64.0.42")

    time.sleep(10)
    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.OFFLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=72.0,
                     tailscale_ip="")

    time.sleep(10)
    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=-1.0,
                     tailscale_ip="10.0.0.5")
    time.sleep(10)

    send_harbour_mode(ser, enter=True)
    time.sleep(10)

    send_harbour_mode(ser, enter=False)
    time.sleep(10)

    send_core_status(ser,
                     camera=core_pb2.CoreStatus.ONLINE,
                     controller=core_pb2.CoreStatus.ONLINE,
                     cloud_connection=core_pb2.CoreStatus.ONLINE,
                     cellular=core_pb2.CoreStatus.ONLINE,
                     wifi=core_pb2.CoreStatus.ONLINE,
                     gps=core_pb2.CoreStatus.ONLINE,
                     tailscale=core_pb2.CoreStatus.ONLINE,
                     local_dash=core_pb2.CoreStatus.ONLINE,
                     storage_usage=30.0,
                     tailscale_ip="100.64.0.42")

    time.sleep(10)

    ser.close()


if __name__ == "__main__":
    main()
