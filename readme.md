bencode serialize/deserialize written in native c extension.

```shell
pip install bencode-c
```

```python
import bencode_c

assert bencode_c.bdecode(b'...') == ...

assert bencode_c.bencode(...) == b'...'
```
