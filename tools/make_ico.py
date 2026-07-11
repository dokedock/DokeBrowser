import struct
from pathlib import Path


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    src = repo / "DKIcon.png"
    out = repo / "assets" / "DKIcon.ico"
    png = src.read_bytes()

    reserved = 0
    icon_type = 1
    count = 1
    header = struct.pack("<HHH", reserved, icon_type, count)

    width = 0
    height = 0
    color_count = 0
    entry_reserved = 0
    planes = 1
    bit_count = 32
    bytes_in_res = len(png)
    image_offset = 6 + 16
    entry = struct.pack(
        "<BBBBHHII",
        width,
        height,
        color_count,
        entry_reserved,
        planes,
        bit_count,
        bytes_in_res,
        image_offset,
    )

    out.write_bytes(header + entry + png)


if __name__ == "__main__":
    main()

