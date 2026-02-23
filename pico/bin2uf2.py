"""Convert a raw .bin to a UF2 file for RP2040."""
import struct, sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
FLASH_BASE       = 0x10000000
PAYLOAD_SIZE     = 256

def convert(bin_path, uf2_path):
    with open(bin_path, "rb") as f:
        data = f.read()
    blocks = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    with open(uf2_path, "wb") as f:
        for i in range(blocks):
            addr = FLASH_BASE + i * PAYLOAD_SIZE
            chunk = data[i*PAYLOAD_SIZE:(i+1)*PAYLOAD_SIZE]
            chunk = chunk.ljust(PAYLOAD_SIZE, b'\x00')
            # 32 byte header + 476 bytes (256 payload + 220 padding) + 4 byte end magic = 512
            hdr = struct.pack("<IIIIIIII",
                UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_FLAG_FAMILY,
                addr, PAYLOAD_SIZE, i, blocks, RP2040_FAMILY_ID)
            padding = b'\x00' * (512 - 32 - 4 - PAYLOAD_SIZE)
            end = struct.pack("<I", UF2_MAGIC_END)
            f.write(hdr + chunk + padding + end)
    print(f"Converted {len(data)} bytes -> {blocks} UF2 blocks -> {uf2_path}")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
