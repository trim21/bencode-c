import gc
import secrets
import sys
import tracemalloc

from bencode_c import BencodeEncodeError, bencode

s = tracemalloc.start()

cases = [
    b"d3:foo4:spam3:bar",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
    b"d3:foo4:spam3:bari42e",
]


while True:
    for c in cases:
        try:
            bencode(c)
        except BencodeEncodeError:
            pass
    gc.collect()
    v = tracemalloc.get_tracemalloc_memory()
    print(v)
    if v > 100000000:
        sys.exit(1)
