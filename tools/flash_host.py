#!/usr/bin/env python3
"""Flash the host app via USB ROM download and verify startup on a separate UART.

Use while the board still runs USB-console firmware, or after entering ROM
USB download mode manually. Once USB-OTG hosts a peripheral, subsequent flashes
may require BOOT/reset. Matching bootloader and partition table are installed;
game assets are left intact.
"""
import argparse
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial
from esptool import detect_chip


def open_port(path):
    port = serial.Serial(baudrate=115200, timeout=0.25)
    port.dtr = False
    port.rts = False
    port.port = str(path)
    port.open()
    return port


def close_usb(port):
    # The console disappears when the application switches these pins to OTG.
    # Bound the Linux serial close/drain wait after that disconnect.
    def timeout(signum, frame):
        raise TimeoutError('USB close timed out')
    previous = signal.signal(signal.SIGALRM, timeout)
    signal.setitimer(signal.ITIMER_REAL, 2)
    try:
        port.close()
    except (TimeoutError, OSError):
        port.fd = None
        port.is_open = False
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--flash-port', required=True)
    p.add_argument('--monitor-port', required=True)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--seconds', type=float, default=60)
    a = p.parse_args()
    if a.seconds <= 0 or Path(a.flash_port).resolve() == Path(a.monitor_port).resolve():
        p.error('use distinct USB download and UART monitor ports, and a positive duration')
    # Preserve identity across ttyACM renumbering without embedding a board ID.
    requested = Path(a.flash_port)
    candidates = list(Path('/dev/serial/by-id').glob('*Espressif*USB*'))
    flash_port = next((q for q in candidates if q.resolve() == requested.resolve()), requested)
    a.log.parent.mkdir(parents=True, exist_ok=True)
    uart = open_port(a.monitor_port)
    captured = bytearray()
    read_errors = []
    stop = threading.Event()

    with a.log.open('wb') as log:
        def reader():
            try:
                while not stop.is_set():
                    data = uart.read(4096)
                    if data:
                        captured.extend(data)
                        log.write(data)
                        log.flush()
            except (serial.SerialException, OSError) as error:
                read_errors.append(str(error))
        worker = threading.Thread(target=reader)
        worker.start()
        try:
            with a.log.with_suffix('.flash.log').open('wb') as flash_log:
                subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3',
                    '--port', str(flash_port), '--baud', '921600', '--after', 'no-reset',
                    'write-flash', '--flash-mode', 'dio', '--flash-freq', '80m',
                    '--flash-size', '16MB',
                    '0x0', 'build/bootloader/bootloader.bin',
                    '0x8000', 'build/partition_table/partition-table.bin',
                    '0x10000', 'build/esp32-doom-usb-host.bin'],
                    stdout=flash_log, stderr=subprocess.STDOUT, check=True)
            usb = open_port(flash_port)
            try:
                esp = detect_chip(usb)
                if esp.CHIP_NAME != 'ESP32-S3':
                    raise RuntimeError('Expected ESP32-S3')
                esp.write_reg(esp.RTC_CNTL_OPTION1_REG, 0, esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
                if esp.read_reg(esp.GPIO_STRAP_REG) & esp.GPIO_STRAP_SPI_BOOT_MASK:
                    esp.hard_reset()
                else:
                    esp.watchdog_reset()
            finally:
                close_usb(usb)
            print(f'Host app installed; capturing UART for {a.seconds:g}s into {a.log}', flush=True)
            stop.wait(a.seconds)
        finally:
            stop.set()
            worker.join(timeout=2)
            uart.close()
    output = captured.decode(errors='replace')
    errors = ('Guru Meditation', 'assert failed', 'abort()', 'CORRUPT HEAP',
              'Task watchdog got triggered', 'Z_Malloc: Failure', 'USB host install failed',
              'HID host install failed', 'Cannot create USB host task', 'Cannot create input queue')
    missing = [s for s in ('USB HID host ready', 'I_InitGraphics:', 'ST_Init:') if s not in output]
    found = [s for s in errors if s in output]
    if read_errors or missing or found:
        raise SystemExit(f'Startup check failed: missing={missing}, errors={found + read_errors}; see {a.log}')
    print('PASS: USB HID host and game graphics initialized; no crash/watchdog errors during capture.')


if __name__ == '__main__':
    main()
