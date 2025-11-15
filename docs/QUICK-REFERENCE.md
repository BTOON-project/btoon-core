# BTOON Quick Reference

## Type Encoding Summary

### Fixed-Size Types (1 byte)
```
nil:     0xc0
false:   0xc2  
true:    0xc3
fixint+: 0x00-0x7f (0 to 127)
fixint-: 0xe0-0xff (-32 to -1)
```

### Integers
```
uint8:  0xcc [1 byte]
uint16: 0xcd [2 bytes BE]
uint32: 0xce [4 bytes BE]
uint64: 0xcf [8 bytes BE]
int8:   0xd0 [1 byte]
int16:  0xd1 [2 bytes BE]
int32:  0xd2 [4 bytes BE]
int64:  0xd3 [8 bytes BE]
```

### Floating Point
```
float32: 0xca [4 bytes IEEE 754]
float64: 0xcb [8 bytes IEEE 754]
```

### Strings (UTF-8)
```
fixstr:  101xxxxx [0-31 bytes]
str8:    0xd9 [len:1] [bytes]
str16:   0xda [len:2] [bytes]
str32:   0xdb [len:4] [bytes]
```

### Binary
```
bin8:    0xc4 [len:1] [bytes]
bin16:   0xc5 [len:2] [bytes]
bin32:   0xc6 [len:4] [bytes]
```

### Arrays
```
fixarray: 1001xxxx [0-15 elements]
array16:  0xdc [count:2] [elements]
array32:  0xdd [count:4] [elements]
```

### Maps
```
fixmap:   1000xxxx [0-15 pairs]
map16:    0xde [count:2] [k,v pairs]
map32:    0xdf [count:4] [k,v pairs]
```

### Extensions
```
fixext1:  0xd4 [type] [1 byte]
fixext2:  0xd5 [type] [2 bytes]
fixext4:  0xd6 [type] [4 bytes]
fixext8:  0xd7 [type] [8 bytes]
fixext16: 0xd8 [type] [16 bytes]
ext8:     0xc7 [len:1] [type] [data]
ext16:    0xc8 [len:2] [type] [data]
ext32:    0xc9 [len:4] [type] [data]
```

## Encoding Decision Tree

```
Value Type?
├─ Nil → 0xc0
├─ Bool → true:0xc3, false:0xc2
├─ Integer
│  ├─ [0,127] → fixint (1 byte)
│  ├─ [-32,-1] → fixint (1 byte)
│  ├─ [128,255] → uint8 (2 bytes)
│  ├─ [-128,-33] → int8 (2 bytes)
│  └─ ... larger sizes
├─ Float → float32/64
├─ String
│  ├─ [0,31] bytes → fixstr
│  ├─ [32,255] → str8
│  ├─ [256,65535] → str16
│  └─ larger → str32
├─ Binary → bin8/16/32
├─ Array
│  ├─ [0,15] → fixarray
│  ├─ [16,65535] → array16
│  └─ larger → array32
└─ Map
   ├─ [0,15] → fixmap
   ├─ [16,65535] → map16
   └─ larger → map32
```

## Common Patterns

### Empty Values
```
Empty string:  0xa0
Empty binary:  0xc4 0x00
Empty array:   0x90
Empty map:     0x80
```

### Small Numbers
```
0:    0x00
1:    0x01
-1:   0xff
127:  0x7f
-32:  0xe0
128:  0xcc 0x80
```

### Common Strings
```
"a":     0xa1 0x61
"abc":   0xa3 0x61 0x62 0x63
32 char: 0xd9 0x20 [32 bytes]
```

## Extension Types Quick Reference

| Type | ID | Format | Description |
|------|----|--------|-------------|
| Timestamp | -1 | fixext8 | Unix time + nanos |
| Date | -2 | fixext4 | Year/Month/Day |
| DateTime | -3 | ext16 | Date + Time + TZ |
| BigInt | -4 | ext* | Arbitrary precision |
| Tabular | -10 | ext32 | Columnar data |

## Compression Header

```
struct {
    uint32_t magic;     // 0x42544F4E
    uint32_t version;   // 0x00000001
    uint32_t comp_size;
    uint32_t orig_size;
    uint8_t  algorithm;
}
```

## Validation Limits

| Check | Default | Maximum |
|-------|---------|---------|
| Depth | 128 | 1000 |
| String | 10 MB | 100 MB |
| Binary | 100 MB | 1 GB |
| Array | 1M items | 100M |
| Map | 100K pairs | 10M |

## Error Codes

| Code | Description |
|------|-------------|
| E001 | Invalid type marker |
| E002 | Truncated data |
| E003 | Invalid UTF-8 |
| E004 | Depth exceeded |
| E005 | Size limit exceeded |
| E006 | Invalid extension type |
| E007 | Schema validation failed |
| E008 | Compression error |

## Performance Tips

1. **Use fixints** for small numbers (0-127, -32 to -1)
2. **Use fixstr** for strings ≤31 bytes
3. **Use fixarray/fixmap** for small containers
4. **Enable compression** for data >1KB
5. **Use tabular extension** for homogeneous data
6. **Pool memory** for repeated encoding/decoding
7. **Validate once** and reuse validated data

## Common Mistakes

❌ Using int32 for small positive numbers (use fixint)
❌ Not validating untrusted input
❌ Ignoring compression for large data
❌ Deep nesting without depth checks
❌ Not checking UTF-8 validity
❌ Assuming little-endian (BTOON is big-endian)
