#!/usr/bin/env python3
import os
import sys
import serial
import time
import subprocess
import random
from pathlib import Path

try:
    from gen import core_pb2
except ImportError:
    print("core_pb2 module not found. Attempting to compile from .proto file...")
    current_dir = os.path.dirname(os.path.realpath(sys.argv[0]))
    generated_dir = os.path.join(current_dir, 'gen')
    os.makedirs(generated_dir , exist_ok=True)

    current_dir_path = Path(current_dir)

    proto_file = os.path.join(current_dir_path.parent.absolute(), 'proto', 'core.proto')

    if not os.path.exists(proto_file):
        print(f"Error: core.proto not found at {proto_file}")
        print("Ensure your APP_ROOT points to the correct directory structure")
        sys.exit(1)

    try:
        subprocess.run(['protoc', '--version'],
                      check=True, 
                      stdout=subprocess.PIPE,
                      stderr=subprocess.PIPE)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Error: Protocol Buffers compiler (protoc) not found")
        print("Install with: sudo apt-get install protobuf-compiler")
        sys.exit(1)

    try:
        print(f"Compiling {proto_file} to {generated_dir}...")
        subprocess.run([
            'protoc',
            f'--proto_path={os.path.dirname(proto_file)}',
            f'--python_out={generated_dir}',
            proto_file
        ], check=True, capture_output=True, text=True)
        print(f"Successfully generated core_pb2.py in {generated_dir}")
    except subprocess.CalledProcessError as e:
        print(f"protoc compilation failed: {e.stderr.strip()}")
        print("Full command attempted:")
        print(f"protoc --proto_path={os.path.dirname(proto_file)} "
              f"--python_out={generated_dir} {proto_file}")
        sys.exit(1)

    if 'core_pb2' in sys.modules:
        del sys.modules['core_pb2']

    try:
        from gen import core_pb2
    except ImportError as e:
        print(f"Error: Failed to import core_pb2 after compilation: {e}")
        sys.exit(1)



def send_sat_message(ser):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = random.randint(1, 1000000)

    sat = core_pb2.SatMessage()
    sat.message = "Hello from satellite"
    core_msg.sat.CopyFrom(sat)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"Sent SatMessage msg_id={core_msg.msg_id} ({len(serialized)} bytes)")
    return core_msg.msg_id


def send_sys_init(ser):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = random.randint(1, 1000000)

    init = core_pb2.SysInit()
    init.id = "sat-001"
    init.addr = "192.168.1.100"
    init.sec_key = "supersecretkey123"
    init.rconf = "/etc/sat/config.json"
    init.hostname = "satellite-001"
    init.wifi_network = "Starlink-Guest"
    core_msg.init.CopyFrom(init)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"Sent SysInit msg_id={core_msg.msg_id} ({len(serialized)} bytes)")
    return core_msg.msg_id


def send_core_status(ser):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = random.randint(1, 1000000)

    status = core_pb2.CoreStatus()
    status.camera = core_pb2.CoreStatus.ONLINE
    status.controller = core_pb2.CoreStatus.ONLINE
    status.cloud_connection = core_pb2.CoreStatus.ONLINE
    status.cellular = core_pb2.CoreStatus.OFFLINE
    status.wifi = core_pb2.CoreStatus.ONLINE
    status.gps = core_pb2.CoreStatus.ERROR
    status.tailscale = core_pb2.CoreStatus.ONLINE
    status.local_dash = core_pb2.CoreStatus.UNKNOWN
    status.storage_usage = 45.2
    status.tailscale_ip = "100.64.0.1"
    core_msg.status.CopyFrom(status)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"Sent CoreStatus msg_id={core_msg.msg_id} ({len(serialized)} bytes)")
    return core_msg.msg_id


def send_harbour_mode(ser):
    core_msg = core_pb2.CoreMessage()
    core_msg.msg_id = random.randint(1, 1000000)

    mode = core_pb2.HarbourMode()
    mode.cmd = core_pb2.HarbourMode.ENTER
    core_msg.mode.CopyFrom(mode)

    serialized = core_msg.SerializeToString()
    ser.write(serialized)
    print(f"Sent HarbourMode msg_id={core_msg.msg_id} ({len(serialized)} bytes)")
    return core_msg.msg_id


def wait_for_reply(ser, expected_msg_id):
    reply_data = ser.read(1024)
    if reply_data:
        try:
            reply = core_pb2.LpmReply()
            reply.ParseFromString(reply_data)
            print(f"Received LpmReply: msg_id={reply.msg_id} response={core_pb2.LpmReply.Response.Name(reply.response)}")
            if reply.msg_id != expected_msg_id:
                print(f"Warning: msg_id mismatch (expected {expected_msg_id}, got {reply.msg_id})")
        except Exception as e:
            print(f"Failed to parse reply: {e}")
            print(f"Raw reply data: {reply_data.hex()}")
    else:
        print("No reply received")


def main():
    if len(sys.argv) != 2:
        print("Usage: python send_core_message.py <serial_device>")
        print("Example: python send_core_message.py /dev/ttyXRUSB0")
        sys.exit(1)

    serial_device = sys.argv[1]

    try:
        ser = serial.Serial(
            port=serial_device,
            baudrate=115200,
            timeout=10
        )
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    print(f"Connected to {serial_device}. Sending one of each message type...")

    message_senders = [
        ("SatMessage", send_sat_message),
        ("SysInit", send_sys_init),
        ("CoreStatus", send_core_status),
        ("HarbourMode", send_harbour_mode),
    ]

    for name, sender in message_senders:
        print(f"\n--- Sending {name} ---")
        try:
            msg_id = sender(ser)
            wait_for_reply(ser, msg_id)
            time.sleep(1)
        except Exception as e:
            print(f"Error sending {name}: {e}")

    print("\nAll message types sent. Exiting.")

if __name__ == "__main__":
    main()
