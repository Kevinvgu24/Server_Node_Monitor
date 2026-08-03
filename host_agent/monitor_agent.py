#!/usr/bin/env python3
import os
import sys
import time
import shutil
import subprocess
import json

# Try to import pyserial
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("=" * 60)
    print("Error: 'pyserial' package is not installed.")
    print("On Proxmox (Debian), please install it using:")
    print("    sudo apt-get update && sudo apt-get install -y python3-serial")
    print("=" * 60)
    sys.exit(1)

# Helper to find Arduino Serial Port
def find_arduino_port():
    # Prioritize custom udev symlink if it exists
    if os.path.exists("/dev/arduino_monitor"):
        return "/dev/arduino_monitor"
        
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.lower()
        # Common keywords for Arduino Uno or USB Serial bridges
        if any(kw in desc for kw in ["arduino", "ch340", "cp210", "ftdi", "usb serial", "usb-to-uart"]):
            return port.device
    # Fallback to first available port if found
    if ports:
        return ports[0].device
    return None

# 1. CPU Usage (from /proc/stat)
def get_cpu_usage(last_idle, last_total):
    try:
        with open('/proc/stat', 'r') as f:
            lines = f.readlines()
        for line in lines:
            if line.startswith('cpu '):
                parts = [int(x) for x in line.split()[1:]]
                # idle = idle + iowait
                idle = parts[3] + parts[4]
                total = sum(parts)
                
                diff_idle = idle - last_idle
                diff_total = total - last_total
                
                if diff_total == 0:
                    return 0, idle, total
                
                cpu_pct = int(100 * (1.0 - (diff_idle / diff_total)))
                return max(0, min(100, cpu_pct)), idle, total
    except Exception as e:
        print(f"Error reading CPU: {e}")
    return 0, last_idle, last_total

# 2. RAM Usage (from /proc/meminfo)
def get_ram_usage():
    try:
        with open('/proc/meminfo', 'r') as f:
            content = f.read()
        mem_total = 0
        mem_free = 0
        mem_buffers = 0
        mem_cached = 0
        for line in content.split('\n'):
            if line.startswith('MemTotal:'):
                mem_total = int(line.split()[1])
            elif line.startswith('MemFree:'):
                mem_free = int(line.split()[1])
            elif line.startswith('Buffers:'):
                mem_buffers = int(line.split()[1])
            elif line.startswith('Cached:'):
                mem_cached = int(line.split()[1])
        
        # Actual used memory by applications
        mem_used = mem_total - mem_free - mem_buffers - mem_cached
        if mem_total > 0:
            return int(100 * mem_used / mem_total)
    except Exception as e:
        print(f"Error reading RAM: {e}")
    return 0

# 3. Disk Temperature Detection (sysfs hwmon / smartctl / hddtemp)
def get_disk_temp(dev_name):
    if not dev_name or dev_name == 'None':
        return -1
        
    # Method 1: Check sysfs hwmon directly linked to block device
    try:
        sys_hwmon = f"/sys/block/{dev_name}/device/hwmon"
        if os.path.exists(sys_hwmon):
            for hwm in os.listdir(sys_hwmon):
                tf = os.path.join(sys_hwmon, hwm, "temp1_input")
                if os.path.exists(tf):
                    with open(tf, 'r') as f:
                        val = int(f.read().strip())
                    if val > 0:
                        return int(val / 1000) if val > 1000 else int(val)
    except:
        pass

    # Method 2: Check smartctl JSON for exact /dev/{dev_name}
    try:
        res = subprocess.run(
            ['smartctl', '-A', f'/dev/{dev_name}', '--json'],
            capture_output=True, text=True, timeout=2
        )
        if res.returncode == 0 or res.stdout:
            data = json.loads(res.stdout)
            temp = data.get('temperature', {}).get('current')
            if temp is not None and int(temp) > 0:
                return int(temp)
    except:
        pass

    # Method 3: Fallback hddtemp
    try:
        res = subprocess.run(
            ['hddtemp', '-n', f'/dev/{dev_name}'],
            capture_output=True, text=True, timeout=2
        )
        if res.returncode == 0:
            val = int(res.stdout.strip())
            if val > 0:
                return val
    except:
        pass

    return -1

# Helper to check if a block device is a physical disk
def is_physical_disk(name):
    if not name:
        return False
    # Filter out virtual / system block devices (ZFS zd, loop, cdrom sr, ram, dm, nbd, rbd)
    if any(name.startswith(prefix) for prefix in ['zd', 'loop', 'sr', 'ram', 'dm-', 'nbd', 'rbd']):
        return False
    # Allow physical drive prefixes (sd, nvme, hd, vd)
    if any(name.startswith(prefix) for prefix in ['sd', 'nvme', 'hd', 'vd']):
        return True
    return False

# 4. Disk List and Status (1 SSD + up to 6 HDDs)
def get_disks_info():
    ssd = {'name': 'None', 'type': 'SSD', 'size': '0G', 'usage': -1, 'temp': -1}
    hdds = []
    
    def get_path_usage(path):
        try:
            total, used, free = shutil.disk_usage(path)
            return int(100 * used / total)
        except:
            return -1

    def find_mount_in_device(dev):
        mounts = []
        def collect_mounts(d):
            m = d.get('mountpoint')
            if m:
                mounts.append(m)
            for child in (d.get('children') or []):
                collect_mounts(child)
        collect_mounts(dev)
        
        if not mounts:
            return -1
            
        if '/' in mounts:
            return get_path_usage('/')
            
        for m in mounts:
            if not m.startswith('/boot') and m != '/efi':
                usage = get_path_usage(m)
                if usage >= 0:
                    return usage
                    
        for m in mounts:
            usage = get_path_usage(m)
            if usage >= 0:
                return usage
        return -1

    try:
        result = subprocess.run(
            ['lsblk', '-o', 'NAME,TYPE,SIZE,ROTA,MOUNTPOINT', '--json'],
            capture_output=True, text=True, check=True
        )
        data = json.loads(result.stdout)
        
        physical_disks = []
        for dev in (data.get('blockdevices') or []):
            name = dev.get('name', '')
            if dev.get('type') == 'disk' and is_physical_disk(name):
                physical_disks.append(dev)
                
        # Sort physical disks by name (nvme0n1, sda, sdb, sdc...)
        physical_disks.sort(key=lambda d: d.get('name', ''))
        
        ssd_found = False
        raw_hdds = []
        
        for dev in physical_disks:
            name = dev.get('name', '')
            size = dev.get('size', '0G')
            rota_val = dev.get('rota')
            is_ssd = (str(rota_val).lower() in ['0', 'false']) if rota_val is not None else False
            if 'nvme' in name:
                is_ssd = True
                
            usage = find_mount_in_device(dev)
            if usage == -1 and (name in ['sda', 'nvme0n1', 'vda'] or (not ssd_found and len(raw_hdds) == 0)):
                usage = get_path_usage('/')
            
            size_clean = size.replace(' ', '').replace('B', '')
            if '.' in size_clean:
                parts = size_clean.split('.')
                unit = "".join([c for c in parts[1] if c.isalpha()])
                size_clean = parts[0] + unit
            
            temp = get_disk_temp(name)
            
            disk_info = {
                'name': name,
                'type': 'SSD' if is_ssd else 'HDD',
                'size': size_clean,
                'usage': usage,
                'temp': temp
            }
            
            if is_ssd and not ssd_found:
                ssd = disk_info
                ssd_found = True
            else:
                raw_hdds.append(disk_info)
                
        # Sort HDDs alphabetically by name (sda, sdb, sdc...)
        raw_hdds.sort(key=lambda x: x['name'])
        hdds = raw_hdds[:6]
        
    except Exception as e:
        print(f"Error reading disk list: {e}")
        
    while len(hdds) < 6:
        hdds.append({
            'name': 'None',
            'type': 'HDD',
            'size': '0G',
            'usage': -1,
            'temp': -1
        })
        
    return ssd, hdds[:6]

# 5. CPU Temperature
def get_cpu_temp():
    try:
        hwmon_dir = '/sys/class/hwmon'
        if os.path.exists(hwmon_dir):
            for hwmon in os.listdir(hwmon_dir):
                hwmon_path = os.path.join(hwmon_dir, hwmon)
                name_path = os.path.join(hwmon_path, 'name')
                if os.path.exists(name_path):
                    with open(name_path, 'r') as f:
                        sensor_name = f.read().strip().lower()
                    
                    if sensor_name in ['coretemp', 'k10temp', 'zenpower']:
                        temp_files = [f for f in os.listdir(hwmon_path) if f.startswith('temp') and f.endswith('_input')]
                        temp_values = []
                        for tf in temp_files:
                            tf_path = os.path.join(hwmon_path, tf)
                            label_file = tf.replace('_input', '_label')
                            label_path = os.path.join(hwmon_path, label_file)
                            label_name = ""
                            if os.path.exists(label_path):
                                with open(label_path, 'r') as lf:
                                    label_name = lf.read().strip().lower()
                            
                            try:
                                with open(tf_path, 'r') as f:
                                    val = int(f.read().strip())
                                if val > 0:
                                    temp_c = val / 1000.0
                                    if any(kw in label_name for kw in ['package', 'tctl', 'tdie']):
                                        return round(temp_c, 1)
                                    temp_values.append(temp_c)
                            except:
                                pass
                        
                        if temp_values:
                            return round(max(temp_values), 1)
    except Exception as e:
        pass

    try:
        thermal_dir = '/sys/class/thermal'
        if os.path.exists(thermal_dir):
            for zone in os.listdir(thermal_dir):
                if zone.startswith('thermal_zone'):
                    path = f'/sys/class/thermal/{zone}/temp'
                    if os.path.exists(path):
                        with open(path, 'r') as f:
                            temp_raw = int(f.read().strip())
                        if temp_raw > 0:
                            if temp_raw > 1000:
                                return round(temp_raw / 1000.0, 1)
                            else:
                                return float(temp_raw)
    except Exception as e:
        pass

    return 0.0

# 6. Proxmox VM, LXC Container & Docker Status
def get_workload_statuses():
    vm_run, vm_stop = 0, 0
    ct_run, ct_stop = 0, 0
    doc_run, doc_stop = 0, 0
    
    # Query Proxmox VE (QEMU VMs and LXC Containers)
    try:
        result = subprocess.run(
            ['pvesh', 'get', '/cluster/resources', '--output-format', 'json'],
            capture_output=True, text=True, check=True
        )
        resources = json.loads(result.stdout)
        for res in resources:
            res_type = res.get('type')
            status = res.get('status')
            if res_type == 'qemu':
                if status == 'running':
                    vm_run += 1
                else:
                    vm_stop += 1
            elif res_type == 'lxc':
                if status == 'running':
                    ct_run += 1
                else:
                    ct_stop += 1
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"Error querying Proxmox status: {e}")
        
    # Query Docker Containers
    try:
        result = subprocess.run(
            ['docker', 'info', '--format', '{{.ContainersRunning}}|{{.ContainersStopped}}'],
            capture_output=True, text=True, check=True
        )
        parts = result.stdout.strip().split('|')
        if len(parts) == 2:
            doc_run = int(parts[0])
            doc_stop = int(parts[1])
    except FileNotFoundError:
        pass
    except Exception as e:
        try:
            res_run = subprocess.run(['docker', 'ps', '-q'], capture_output=True, text=True)
            if res_run.returncode == 0:
                doc_run = len([line for line in res_run.stdout.splitlines() if line.strip()])
            res_all = subprocess.run(['docker', 'ps', '-a', '-q'], capture_output=True, text=True)
            if res_all.returncode == 0:
                total_doc = len([line for line in res_all.stdout.splitlines() if line.strip()])
                doc_stop = max(0, total_doc - doc_run)
        except:
            pass

    return vm_run, vm_stop, ct_run, ct_stop, doc_run, doc_stop

# 7. Uptime (from /proc/uptime)
def get_uptime():
    try:
        with open('/proc/uptime', 'r') as f:
            uptime_seconds = float(f.read().split()[0])
        days = int(uptime_seconds // (24 * 3600))
        hours = int((uptime_seconds % (24 * 3600)) // 3600)
        minutes = int((uptime_seconds % 3600) // 60)
        if days > 0:
            return f"{days}d {hours}h"
        elif hours > 0:
            return f"{hours}h {minutes}m"
        else:
            return f"{minutes}m"
    except Exception as e:
        return "N/A"

def main():
    print("Starting Proxmox Node Monitor Agent...")
    
    cpu_idle, cpu_total = 0, 0
    
    # Warm up CPU stats
    _, cpu_idle, cpu_total = get_cpu_usage(0, 0)
    time.sleep(0.5)
    
    print("Agent is starting up. Preparing to monitor Proxmox server stats...")
    
    while True:
        port = find_arduino_port()
        if not port:
            print("Error: No Arduino/Serial device detected. Retrying connection in 5 seconds...")
            time.sleep(5.0)
            continue
            
        print(f"Connecting to Arduino on: {port} at 115200 baud...")
        
        try:
            ser = serial.Serial(port, 115200, timeout=1)
            time.sleep(2)
            print("Connection established! Sending data every 2 seconds. Press Ctrl+C to exit.")
            
            while True:
                cpu, cpu_idle, cpu_total = get_cpu_usage(cpu_idle, cpu_total)
                ram = get_ram_usage()
                cpu_temp = get_cpu_temp()
                ssd, hdds = get_disks_info()
                vm_run, vm_stop, ct_run, ct_stop, doc_run, doc_stop = get_workload_statuses()
                uptime = get_uptime()
                
                # Format packet: CPU|RAM|CPU_TEMP|SSD...|HDD1..6...|VM_RUN|VM_STOP|CT_RUN|CT_STOP|DOC_RUN|DOC_STOP|UPTIME\n
                packet = f"{cpu}|{ram}|{cpu_temp:.1f}|"
                packet += f"{ssd['name']}|{ssd['size']}|{ssd['usage']}|{ssd['temp']}|"
                for h in hdds:
                    packet += f"{h['name']}|{h['size']}|{h['usage']}|{h['temp']}|"
                packet += f"{vm_run}|{vm_stop}|{ct_run}|{ct_stop}|{doc_run}|{doc_stop}|{uptime}\n"
                
                ser.write(packet.encode('utf-8'))
                ser.flush()
                
                time.sleep(2.0)
                
        except (serial.SerialException, OSError) as e:
            print(f"Serial connection error: {e}. Attempting to reconnect in 5 seconds...")
            try:
                ser.close()
            except:
                pass
            time.sleep(5.0)
        except KeyboardInterrupt:
            print("\nExiting monitor agent...")
            try:
                ser.close()
            except:
                pass
            break

if __name__ == '__main__':
    main()
