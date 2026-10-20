import socket
import struct
import hashlib
import sys
import time
import argparse

HOST = "192.168.4.1"
PORT = 8888
MAGIC = 0xA5
TAIL = 0x5A
AUTH_TOKEN = b"PSU2025"

T_AUTH, T_AUTH_ACK, T_START, T_START_ACK = 0x01, 0x02, 0x10, 0x11
T_DATA, T_ACK, T_NAK, T_END, T_END_ACK, T_ERR = 0x20, 0x21, 0x22, 0x30, 0x31, 0xFF

CHUNK = 4096
WINDOW = 8
TIMEOUT = 60          # 修复2：超时 60 秒（发送阻塞期间不误超时）

RX_BUF = b""         # 持久接收缓冲


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(ftype, seq, payload=b""):
    header = struct.pack("<BBHHH", MAGIC, ftype, seq, len(payload), 0)
    body = header + payload
    crc = crc16(body)
    header = struct.pack("<BBHHH", MAGIC, ftype, seq, len(payload), crc)
    return header + payload + bytes([TAIL])


def parse_frame(buf):
    if len(buf) < 9:
        return None, 0
    if buf[0] != MAGIC:
        return None, -1
    magic, ftype, seq, ln, crc = struct.unpack("<BBHHH", buf[:8])
    total = 8 + ln + 1
    if len(buf) < total:
        return None, 0
    if buf[total-1] != TAIL:
        return None, -1
    return (ftype, seq, buf[8:8+ln]), total


def recv_frame(s):
    """从持久缓冲取帧，剩余的保留，不丢数据"""
    global RX_BUF
    while True:
        frame, n = parse_frame(RX_BUF)
        if n > 0:
            RX_BUF = RX_BUF[n:]
            return frame
        if n < 0:
            RX_BUF = RX_BUF[1:]
            continue
        try:
            chunk = s.recv(65536)
            if not chunk:
                return None
            RX_BUF += chunk
        except socket.timeout:
            return None


def recv_ack_all(s, next_ack):
    """消化缓冲里所有 ACK，返回最新进度"""
    got_any = False
    while True:
        try:
            s.settimeout(0.05)     # 50ms 快速轮询，不阻塞
            frame = recv_frame(s)
        except Exception:
            return next_ack, got_any

        if frame is None:
            return next_ack, got_any
        if frame[0] == T_ACK:
            if frame[1] >= next_ack:
                next_ack = frame[1] + 1
            got_any = True
        elif frame[0] == T_ERR:
            print("设备报错")
            return -1, got_any


def send_and_wait(s, ftype, seq, payload, expect):
    s.sendall(build_frame(ftype, seq, payload))
    s.settimeout(TIMEOUT)
    frame = recv_frame(s)
    if frame and frame[0] == expect:
        return True
    return False


def ota(bin_path):
    global RX_BUF
    data = open(bin_path, "rb").read()
    sha = hashlib.sha256(data).digest()
    print(f"固件: {bin_path}")
    print(f"大小: {len(data)/1024:.1f} KB")

    s = socket.socket()
    s.settimeout(TIMEOUT)
    print(f"连接 {HOST}:{PORT} ...")
    s.connect((HOST, PORT))
    print("已连接")
    RX_BUF = b""

    # 认证
    print("认证...")
    if not send_and_wait(s, T_AUTH, 0, AUTH_TOKEN, T_AUTH_ACK):
        print("认证失败")
        return
    print("认证通过")

    # 开始
    print("发送升级开始...")
    start_payload = struct.pack("<I", len(data)) + sha
    if not send_and_wait(s, T_START, 0, start_payload, T_START_ACK):
        print("开始失败")
        return
    print("设备就绪，开始传输")

    chunks = [data[i:i+CHUNK] for i in range(0, len(data), CHUNK)]
    total_chunks = len(chunks)
    print(f"总数据块: {total_chunks}, 窗口: {WINDOW}")
    start_time = time.time()

    send_idx = 0
    next_ack = 0
    last_print = time.time()

    while next_ack < total_chunks:
        # 发满窗口
        while send_idx < total_chunks and send_idx < next_ack + WINDOW:
            s.sendall(build_frame(T_DATA, send_idx, chunks[send_idx]))
            send_idx += 1

        # 收 ACK（只收一个，立刻推进）
        s.settimeout(TIMEOUT)
        frame = recv_frame(s)
        if frame is None:
            print("连接超时，升级中止")
            return
        if frame[0] == T_ERR:
            print("设备报错")
            return
        if frame[0] == T_ACK:
            if frame[1] >= next_ack:
                next_ack = frame[1] + 1

            # 修复3：把缓冲里已有的 ACK 全部消化
            next_ack, _ = recv_ack_all(s, next_ack)
            if next_ack < 0:
                return

        if time.time() - last_print > 1.0:
            last_print = time.time()
            pct = next_ack * 100 // total_chunks
            elapsed = time.time() - start_time
            speed = (next_ack * CHUNK) / elapsed / 1024 if elapsed > 0 else 0
            print(f"进度 {pct}%  {next_ack}/{total_chunks}  {speed:.0f} KB/s")

    elapsed = time.time() - start_time
    speed = (total_chunks * CHUNK) / elapsed / 1024
    print(f"传输完成，耗时 {elapsed:.1f}s，平均 {speed:.0f} KB/s")

    # 结束
    print("发送结束+整包校验...")
    s.settimeout(60)
    if not send_and_wait(s, T_END, 0, sha, T_END_ACK):
        print("结束确认失败")
        return

    print("OTA 成功！设备将重启")
    s.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="可调电源 WiFi OTA 升级工具")
    parser.add_argument("bin", help="固件 .bin 路径")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    HOST, PORT = args.host, args.port
    ota(args.bin)