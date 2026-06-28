from __future__ import annotations

import os
import pathlib
import select
import time
from collections.abc import Iterable

import serial
import usb.core
import usb.util


class SerialDiagSource:
    def __init__(self, path: str, baudrate: int = 115200, timeout: float = 0.2):
        self.dev = serial.Serial(path, baudrate=baudrate, timeout=timeout)

    def write(self, data: bytes) -> None:
        self.dev.write(data)
        self.dev.flush()

    def chunks(self, duration: float | None = None, size: int = 4096) -> Iterable[bytes]:
        start = time.monotonic()
        while duration is None or time.monotonic() - start < duration:
            yield self.dev.read(size)

    def close(self) -> None:
        self.dev.close()


class RawDeviceDiagSource:
    def __init__(self, path: str, timeout: float = 0.2):
        self.path = path
        self.timeout = timeout
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def write(self, data: bytes) -> None:
        os.write(self.fd, data)

    def chunks(self, duration: float | None = None, size: int = 4096) -> Iterable[bytes]:
        start = time.monotonic()
        while duration is None or time.monotonic() - start < duration:
            ready, _, _ = select.select([self.fd], [], [], self.timeout)
            if ready:
                try:
                    yield os.read(self.fd, size)
                except BlockingIOError:
                    yield b""
            else:
                yield b""

    def close(self) -> None:
        os.close(self.fd)


class FileDiagSource:
    def __init__(self, paths: list[str]):
        self.paths = [pathlib.Path(p) for p in paths]

    def chunks(self, size: int = 1024 * 1024) -> Iterable[bytes]:
        for path in self.paths:
            with path.open("rb") as fh:
                while True:
                    data = fh.read(size)
                    if not data:
                        break
                    yield data


class UsbDiagSource:
    def __init__(
        self,
        vid: int = 0x05C6,
        pid: int | None = None,
        interface: int = 0,
        ep_out: int = 0x01,
        ep_in: int = 0x81,
        timeout_ms: int = 200,
    ):
        self.timeout_ms = timeout_ms
        self.dev = usb.core.find(idVendor=vid, idProduct=pid) if pid is not None else usb.core.find(idVendor=vid)
        if self.dev is None:
            raise RuntimeError(f"USB DIAG device not found for VID 0x{vid:04x}" + (f":PID 0x{pid:04x}" if pid else ""))
        self.interface = interface
        self.ep_out = ep_out
        self.ep_in = ep_in
        self.detached = False
        try:
            self.dev.get_active_configuration()
        except usb.core.USBError:
            self.dev.set_configuration()
        if self.dev.is_kernel_driver_active(interface):
            self.dev.detach_kernel_driver(interface)
            self.detached = True
        usb.util.claim_interface(self.dev, interface)

    def write(self, data: bytes) -> None:
        self.dev.write(self.ep_out, data, timeout=self.timeout_ms)

    def chunks(self, duration: float | None = None, size: int = 4096) -> Iterable[bytes]:
        start = time.monotonic()
        while duration is None or time.monotonic() - start < duration:
            try:
                data = self.dev.read(self.ep_in, size, timeout=self.timeout_ms)
                yield bytes(data)
            except usb.core.USBTimeoutError:
                yield b""

    def close(self) -> None:
        usb.util.release_interface(self.dev, self.interface)
        if self.detached:
            self.dev.attach_kernel_driver(self.interface)


def default_device_candidates() -> list[str]:
    paths = [
        "/dev/diag",
        "/dev/diagchar",
        "/dev/smd11",
        "/dev/smd7",
        "/dev/smd8",
        "/dev/smd21",
        "/dev/smd22",
    ]
    paths.extend(sorted(str(p) for p in pathlib.Path("/dev").glob("ttyUSB*")))
    return [p for p in paths if pathlib.Path(p).exists()]


def open_device_source(path: str, raw: bool = False, baudrate: int = 115200, timeout: float = 0.2):
    if raw or pathlib.Path(path).name.startswith("smd") or pathlib.Path(path).name.startswith("diag"):
        return RawDeviceDiagSource(path, timeout=timeout)
    return SerialDiagSource(path, baudrate=baudrate, timeout=timeout)
