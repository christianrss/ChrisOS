import struct
print(hex(270540), 270540)
print(hex(269836), 269836)
print(struct.pack("<I", 270540).hex())
print(list(struct.pack("<I", 270540)))
