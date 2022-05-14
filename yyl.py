
from importlib.resources import path
from time import time
import zstandard
import tarfile
import os
import bz2


def zstd_compress(path):
    cctx = zstandard.ZstdCompressor(level=20, threads=-1)
    with open(path, "rb") as input_f:
        with open(f"{path}.zst", "wb") as output_f:
            cctx.copy_stream(input_f, output_f)
    return f"{path}.zst"

def bz2_compress(path):
    with open(path, "rb") as input_f:
        data = input_f.read()
        bz2fileobj = bz2.BZ2File(f"{path}.bz2", "w", compresslevel=9)
        bz2fileobj.write(data)
    return path+".bz2"

def bz2_decompress(path):
    with open(path, "rb") as input_f:
        with open(path+".dat", "wb") as f:
            f.write(bz2.decompress(input_f.read()))



path = "shore_public.dat"
s = time()
input_size = os.stat(path).st_size
# output = zstd_compress(path)

output = bz2_compress(path)
e = time()

output_size = os.stat(output).st_size
print("input file size: " + str(input_size))
print("output file size: " + str(output_size))

print("压缩时间：" + str(e-s)+"秒")
print("压缩吞吐量：" + str(input_size/(e-s)/1024/1024) + "MB/s")
print("压缩率："+ str(input_size/output_size))

bz2_decompress(output)

