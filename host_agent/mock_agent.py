#!/usr/bin/env python3
import os
import sys
import time
import random
import math

# Try to import pyserial
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("=" * 60)
    print("Error: 'pyserial' package is not installed.")
    print("Please install it using: pip install pyserial")
    print("=" * 60)
    sys.exit(1)

def find_arduino_port():
    # Prioritize custom udev symlink if it exists
    if os.path.exists("/dev/arduino_monitor"):
        return "/dev/arduino_monitor"
        
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.lower()
        if any(kw in desc for kw in ["arduino", "ch340", "cp210", "ftdi", "usb serial", "usb-to-uart"]):
            return port.device
    if ports:
        return ports[0].device
    return None

def main():
    print("Starting Mock Proxmox Monitor Agent for Testing...")
    
    port = find_arduino_port()
    if not port:
        print("Error: No Serial device detected. Please connect your Arduino Uno.")
        print("Available ports list:")
        for p in serial.tools.list_ports.comports():
            print(f"  - {p.device}: {p.description}")
        sys.exit(1)
        
    print(f"Connecting to Arduino on: {port} at 115200 baud...")
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)  # Wait for Arduino reset
        print("Mock Connection established!")
    except Exception as e:
        print(f"Failed to connect to Serial: {e}")
        sys.exit(1)
        
    print("Mock agent is running. Streaming simulated metrics. Press Ctrl+C to exit.")
    
    step = 0
    sim_uptime_mins = 1420  # ~23 hours
    
    try:
        while True:
            # Generate fluctuating values using sine/cosine for smooth transitions
            step += 0.1
            
            # CPU usage fluctuates between 10% and 90%
            cpu = int(50 + 40 * math.sin(step))
            
            # RAM usage fluctuates between 55% and 80%
            ram = int(67 + 12 * math.cos(step * 0.7))
            
            # Temp fluctuates between 41.2C and 69.5C
            temp = float(45.0 + 15.0 * math.sin(step) + 5.0 * random.random())
            
            # SSD metric (nvme0n1)
            ssd_disk = int(43 + random.randint(-1, 1))
            ssd_temp = int(42 + 4 * math.sin(step * 0.5))
            
            # HDD metrics (sda..sdd active, sde..sdf empty)
            h1_disk, h1_temp = int(45 + random.randint(-1, 1)), int(38 + 2 * math.sin(step * 0.4))
            h2_disk, h2_temp = int(60 + random.randint(-1, 1)), int(41 + 3 * math.cos(step * 0.6))
            h3_disk, h3_temp = int(12 + random.randint(-1, 1)), int(36 + 2 * math.sin(step * 0.8))
            h4_disk, h4_temp = int(85 + random.randint(-1, 1)), int(48 + 5 * math.sin(step * 0.3))
            
            # Workloads statuses (QEMU VMs, LXC CTs, Docker Containers)
            vm_run, vm_stop = int(3 + math.sin(step * 0.3)), int(1)
            ct_run, ct_stop = int(4 + math.cos(step * 0.4)), int(0)
            doc_run, doc_stop = int(8 + 2 * math.sin(step * 0.5)), int(2)
            
            # Increment simulated uptime
            sim_uptime_mins += 1
            days = sim_uptime_mins // 1440
            hours = (sim_uptime_mins % 1440) // 60
            minutes = sim_uptime_mins % 60
            
            if days > 0:
                uptime = f"{days}d {hours}h"
            else:
                uptime = f"{hours}h {minutes}m"
                
            # Format packet: CPU|RAM|CPU_TEMP|SSD_Name|SSD_Size|SSD_Usage|SSD_Temp|HDD1..6|VM_RUN|VM_STOP|CT_RUN|CT_STOP|DOC_RUN|DOC_STOP|UPTIME\n
            packet = f"{cpu}|{ram}|{temp:.1f}|nvme0n1|256G|{ssd_disk}|{ssd_temp}|sda|1TB|{h1_disk}|{h1_temp}|sdb|2TB|{h2_disk}|{h2_temp}|sdc|500G|{h3_disk}|{h3_temp}|sdd|1TB|{h4_disk}|{h4_temp}|None|0G|-1|-1|None|0G|-1|-1|{vm_run}|{vm_stop}|{ct_run}|{ct_stop}|{doc_run}|{doc_stop}|{uptime}\n"
            
            # Write to serial
            ser.write(packet.encode('utf-8'))
            ser.flush()
            
            # Print mock values to CLI
            print(f"Sent Mock: CPU:{cpu}% | RAM:{ram}% | CPU Temp:{temp:.1f}C | VMs:{vm_run}/{vm_stop} | LXC:{ct_run}/{ct_stop} | Docker:{doc_run}/{doc_stop} | Uptime:{uptime}")
            
            time.sleep(2.0)
            
    except KeyboardInterrupt:
        print("\nExiting mock agent...")
    finally:
        ser.close()

if __name__ == '__main__':
    main()
