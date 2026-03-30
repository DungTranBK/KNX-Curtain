#!/usr/bin/env python3
"""
KNX UART Capture Tool (Optimized — No Drop)
============================================
Đọc toàn bộ UART log từ nRF54L15, ghi thẳng ra file, tối thiểu xử lý.
Dùng thread riêng để ghi file, main thread chỉ đọc serial.

Cách dùng:
    python3 tools/knx_uart_capture.py
    python3 tools/knx_uart_capture.py -p /dev/ttyUSB0
    python3 tools/knx_uart_capture.py -p /dev/ttyUSB0 -q   # Quiet, không in ra console

Nhấn Ctrl+C để dừng và lưu file.
"""

import sys
import os
import time
import threading
import queue
import argparse
from datetime import datetime

try:
    import serial
except ImportError:
    print("ERROR: pip install pyserial")
    sys.exit(1)


def writer_thread(q, output_path, quiet):
    """Thread ghi file — tách khỏi main loop để không làm chậm serial read."""
    count = 0
    with open(output_path, "wb") as f:
        while True:
            try:
                data = q.get(timeout=0.5)
                if data is None:  # Poison pill
                    break
                f.write(data)
                count += 1
                # Flush mỗi 500 dòng
                if count % 500 == 0:
                    f.flush()
                # In ra console (chỉ khi không quiet)
                if not quiet:
                    try:
                        sys.stdout.buffer.write(data)
                    except Exception:
                        pass
            except queue.Empty:
                f.flush()
                continue
        f.flush()


def main():
    parser = argparse.ArgumentParser(description="KNX UART Capture (No Drop)")
    parser.add_argument("-p", "--port", default="/dev/ttyUSB0")
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    parser.add_argument("-o", "--output", default=None)
    parser.add_argument("-q", "--quiet", action="store_true",
                        help="Không in ra console (nhanh nhất)")
    parser.add_argument("-l", "--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        from serial.tools.list_ports import comports

        for p in comports():
            print(f"  {p.device} - {p.description}")
        return

    # Output file
    if args.output:
        output_file = args.output
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"knx_capture_{ts}.txt"

    log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
    os.makedirs(log_dir, exist_ok=True)
    output_path = os.path.join(log_dir, output_file)

    print(f"Port: {args.port} @ {args.baudrate}")
    print(f"Output: {output_path}")
    print(f"Mode: {'QUIET (file only)' if args.quiet else 'LIVE (console + file)'}")
    print("Ctrl+C to stop.\n")

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            timeout=0.1,          # Timeout ngắn để read nhanh
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        # Tăng buffer size OS-level nếu có thể
        try:
            ser.set_buffer_size(rx_size=65536)
        except Exception:
            pass
    except serial.SerialException as e:
        print(f"ERROR: {e}")
        from serial.tools.list_ports import comports
        print("Available:")
        for p in comports():
            print(f"  {p.device} - {p.description}")
        sys.exit(1)

    # Queue + writer thread
    q = queue.Queue(maxsize=100000)
    wt = threading.Thread(target=writer_thread, args=(q, output_path, args.quiet),
                          daemon=True)
    wt.start()

    line_count = 0
    byte_count = 0
    start = time.monotonic()

    try:
        while True:
            # Đọc toàn bộ data có sẵn trong buffer (nhanh hơn readline)
            raw = ser.readline()
            if raw:
                byte_count += len(raw)
                line_count += 1
                try:
                    q.put_nowait(raw)
                except queue.Full:
                    pass  # Drop nếu writer quá chậm (hiếm khi xảy ra)
    except KeyboardInterrupt:
        pass
    finally:
        # Dừng writer thread
        q.put(None)
        wt.join(timeout=5)
        ser.close()

        elapsed = time.monotonic() - start
        print(f"\n{'='*50}")
        print(f"  Done! {line_count} lines, {byte_count:,} bytes, {elapsed:.1f}s")
        print(f"  File: {output_path}")
        print(f"{'='*50}")


if __name__ == "__main__":
    main()
