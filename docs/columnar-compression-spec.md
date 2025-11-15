# BTOON Columnar Compression Specification

This document specifies the format for columnar compression in BTOON.

## Overview

Columnar compression is a technique that can be used to reduce the size of tabular data. It works by storing the data for each column separately, and then compressing each column using a codec that is well-suited for the data in that column.

## Format

Columnar data is stored in a BTOON extension type. The extension type is a map with the following keys:

- `c`: The number of columns.
- `r`: The number of rows.
- `d`: An array of the data for each column.

Each element in the `d` array is a map with the following keys:

- `t`: The BTOON type of the data in the column.
- `c`: The compression codec used for the column.
- `d`: The compressed data for the column.

## Compression Codecs

The following compression codecs are supported:

- `0`: No compression.
- `1`: Delta compression.
- `2`: Run-length encoding (RLE).
- `3`: Dictionary compression.

## Example

The following is an example of a BTOON document that contains columnar data:

```json
{
  "c": 2,
  "r": 3,
  "d": [
    {
      "t": "int",
      "c": 1,
      "d": [1, 1, 1]
    },
    {
      "t": "string",
      "c": 3,
      "d": [0, 1, 0]
    }
  ]
}
```

This document contains a table with two columns and three rows. The first column is an integer column that is compressed using delta compression. The second column is a string column that is compressed using dictionary compression.
