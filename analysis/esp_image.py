"""Minimal ESP-IDF application-image reader.

No esptool dependency: the app image format is stable and small, and every other
script here needs the same three things -- the header facts, the segment table,
and a load-address -> bytes mapping we can read constants out of.

Layout (esp_image_format.h):
    esp_image_header_t          24 bytes
    for each of header.segment_count:
        esp_image_segment_header_t  { u32 load_addr; u32 data_len; }
        data_len bytes of payload
    padding to a 16-byte boundary, 1 checksum byte
    32-byte SHA-256 when header.hash_appended
"""
import struct
import sys

CHIP_IDS = {
    0x0000: "esp32", 0x0002: "esp32s2", 0x0005: "esp32c3", 0x0009: "esp32s3",
    0x000C: "esp32c2", 0x000D: "esp32c6", 0x0010: "esp32h2", 0x0012: "esp32p4",
}

FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB"}
FLASH_MODES = {0: "QIO", 1: "QOUT", 2: "DIO", 3: "DOUT"}


class Segment:
    def __init__(self, index, load_addr, data, file_offset):
        self.index = index
        self.load_addr = load_addr
        self.data = data
        self.file_offset = file_offset

    @property
    def end(self):
        return self.load_addr + len(self.data)

    def contains(self, addr):
        return self.load_addr <= addr < self.end

    def __repr__(self):
        return (f"<seg{self.index} load=0x{self.load_addr:08x} "
                f"len=0x{len(self.data):x} file=0x{self.file_offset:x}>")


class AppImage:
    def __init__(self, path):
        self.path = path
        blob = open(path, "rb").read()
        self.blob = blob
        (magic, seg_count, self.spi_mode, spi_sz_sp, self.entry_addr,
         self.wp_pin, d0, d1, d2, self.chip_id, self.min_rev,
         self.min_rev_full, self.max_rev_full,
         r0, r1, r2, r3, self.hash_appended) = struct.unpack("<BBBBIBBBBHBHHBBBBB", blob[:24])
        if magic != 0xE9:
            raise ValueError(f"{path}: not an ESP image (magic 0x{magic:02x})")
        self.flash_size = FLASH_SIZES.get(spi_sz_sp >> 4, f"?{spi_sz_sp >> 4}")
        self.flash_speed_nibble = spi_sz_sp & 0xF
        self.chip = CHIP_IDS.get(self.chip_id, f"unknown(0x{self.chip_id:04x})")

        self.segments = []
        off = 24
        for i in range(seg_count):
            load_addr, data_len = struct.unpack("<II", blob[off:off + 8])
            off += 8
            self.segments.append(Segment(i, load_addr, blob[off:off + data_len], off))
            off += data_len
        self.trailer_offset = off

        # esp_app_desc_t lives at the head of the first (DROM) segment.
        d = self.segments[0].data
        self.app_version = _cstr(d[0x10:0x30])
        self.project_name = _cstr(d[0x30:0x50])
        self.build_time = _cstr(d[0x50:0x60])
        self.build_date = _cstr(d[0x60:0x70])
        self.idf_version = _cstr(d[0x70:0x90])

    def read(self, addr, size):
        """Bytes at a load address, or None when no segment covers the range."""
        for seg in self.segments:
            if seg.contains(addr) and seg.contains(addr + size - 1):
                start = addr - seg.load_addr
                return seg.data[start:start + size]
        return None

    def u32(self, addr):
        raw = self.read(addr, 4)
        return None if raw is None else struct.unpack("<I", raw)[0]

    def cstr(self, addr, limit=128):
        raw = self.read(addr, limit)
        return None if raw is None else _cstr(raw)

    def describe(self):
        out = [
            f"file          {self.path}",
            f"chip          {self.chip}",
            f"flash         {self.flash_size} {FLASH_MODES.get(self.spi_mode, self.spi_mode)}",
            f"entry         0x{self.entry_addr:08x}",
            f"project       {self.project_name}",
            f"app version   {self.app_version}",
            f"built         {self.build_date} {self.build_time}",
            f"idf           {self.idf_version}",
            "",
            f"{'seg':>3}  {'load addr':<12} {'length':<10} {'file off':<10} region",
        ]
        for s in self.segments:
            out.append(f"{s.index:>3}  0x{s.load_addr:08x}   0x{len(s.data):<8x} "
                       f"0x{s.file_offset:<8x} {region_of(self.chip, s.load_addr)}")
        return "\n".join(out)


def _cstr(raw):
    return raw.split(b"\0")[0].decode("utf-8", "replace")


# Load-address regions. Only the targets we actually handle are listed; anything
# else falls through to "?" rather than guessing.
REGIONS = {
    "esp32c2": [
        (0x3C000000, 0x3C400000, "DROM (flash rodata)"),
        (0x3FCA0000, 0x3FCE0000, "DRAM (SRAM data)"),
        (0x40378000, 0x403C0000, "IRAM (SRAM code)"),
        (0x42000000, 0x42400000, "IROM (flash code)"),
        (0x50000000, 0x50000800, "RTC RAM"),
    ],
    "esp32c3": [
        (0x3C000000, 0x3C800000, "DROM (flash rodata)"),
        (0x3FC80000, 0x3FCE0000, "DRAM (SRAM data)"),
        (0x40380000, 0x403E0000, "IRAM (SRAM code)"),
        (0x42000000, 0x42800000, "IROM (flash code)"),
    ],
}


def region_of(chip, addr):
    for lo, hi, name in REGIONS.get(chip, []):
        if lo <= addr < hi:
            return name
    return "?"


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(AppImage(path).describe())
        print()
