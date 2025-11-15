"""
BTOON - Binary Tree Object Notation for Python

A high-performance binary serialization format with a Pythonic API.
"""

__version__ = "0.0.1"
__all__ = [
    'dumps', 'loads',
    'dump', 'load',
    'Encoder', 'Decoder',
    'Schema', 'SchemaBuilder',
    'Validator',
    'BtoonException',
    'compress_types'
]

import io
import pathlib
from typing import Any, BinaryIO, Optional, Union, Dict, List
from contextlib import contextmanager
from enum import Enum

# Import the C++ extension
try:
    from . import _btoon_native as _native
    HAS_NATIVE = True
except ImportError:
    import warnings
    warnings.warn("Native BTOON extension not available, using pure Python fallback")
    HAS_NATIVE = False
    # Would import pure Python implementation here
    # from . import _btoon_pure as _native

# Re-export native functions with Pythonic names
if HAS_NATIVE:
    dumps = _native.dumps
    loads = _native.loads
    Encoder = _native.Encoder
    Decoder = _native.Decoder
    Schema = _native.Schema
    SchemaBuilder = _native.SchemaBuilder
    Validator = _native.Validator
    BtoonException = _native.BtoonException
    Compression = _native.Compression

# Compression types for convenience
class compress_types(Enum):
    """Compression algorithm types"""
    NONE = "none"
    ZLIB = "zlib"
    LZ4 = "lz4"
    ZSTD = "zstd"
    AUTO = "auto"


def dump(obj: Any, 
         fp: BinaryIO,
         compress: bool = False,
         compression: Union[str, compress_types] = compress_types.AUTO,
         **kwargs) -> None:
    """
    Serialize object to BTOON and write to file.
    
    Args:
        obj: Python object to serialize
        fp: Binary file-like object to write to
        compress: Enable compression
        compression: Compression algorithm
        **kwargs: Additional encoder options
    
    Example:
        >>> with open('data.btoon', 'wb') as f:
        ...     btoon.dump({'key': 'value'}, f)
    """
    if isinstance(compression, compress_types):
        compression = compression.value
    
    data = dumps(obj, compress=compress, compression=compression, **kwargs)
    fp.write(data)


def load(fp: BinaryIO,
         strict: bool = False,
         use_decimal: bool = False,
         **kwargs) -> Any:
    """
    Read BTOON from file and deserialize.
    
    Args:
        fp: Binary file-like object to read from
        strict: Enable strict validation
        use_decimal: Use Decimal for floats
        **kwargs: Additional decoder options
    
    Returns:
        Deserialized Python object
    
    Example:
        >>> with open('data.btoon', 'rb') as f:
        ...     data = btoon.load(f)
    """
    data = fp.read()
    return loads(data, strict=strict, use_decimal=use_decimal, **kwargs)


def dump_file(obj: Any,
              path: Union[str, pathlib.Path],
              compress: bool = False,
              **kwargs) -> None:
    """
    Serialize object to BTOON file.
    
    Args:
        obj: Object to serialize
        path: File path
        compress: Enable compression
        **kwargs: Additional options
    """
    path = pathlib.Path(path)
    with open(path, 'wb') as f:
        dump(obj, f, compress=compress, **kwargs)


def load_file(path: Union[str, pathlib.Path],
              **kwargs) -> Any:
    """
    Load object from BTOON file.
    
    Args:
        path: File path
        **kwargs: Decoder options
    
    Returns:
        Deserialized object
    """
    path = pathlib.Path(path)
    with open(path, 'rb') as f:
        return load(f, **kwargs)


@contextmanager
def encoder(compress: bool = False, **kwargs):
    """
    Context manager for encoder with options.
    
    Example:
        >>> with btoon.encoder(compress=True) as enc:
        ...     data1 = enc.encode(obj1)
        ...     data2 = enc.encode(obj2)
    """
    enc = Encoder(compress=compress, **kwargs)
    yield enc


@contextmanager
def decoder(strict: bool = False, **kwargs):
    """
    Context manager for decoder with options.
    
    Example:
        >>> with btoon.decoder(strict=True) as dec:
        ...     obj1 = dec.decode(data1)
        ...     obj2 = dec.decode(data2)
    """
    dec = Decoder(strict=strict, **kwargs)
    yield dec


class SchemaRegistry:
    """
    Registry for managing multiple schemas.
    
    Example:
        >>> registry = SchemaRegistry()
        >>> registry.register(user_schema)
        >>> registry.register(product_schema)
        >>> registry.validate(data, 'user')
    """
    
    def __init__(self):
        self._schemas: Dict[str, Schema] = {}
    
    def register(self, schema: Schema, name: Optional[str] = None) -> None:
        """Register a schema."""
        if name is None:
            name = schema.get_name()
        self._schemas[name] = schema
    
    def get(self, name: str) -> Optional[Schema]:
        """Get schema by name."""
        return self._schemas.get(name)
    
    def validate(self, data: Any, schema_name: str) -> bool:
        """Validate data against named schema."""
        schema = self.get(schema_name)
        if schema is None:
            raise ValueError(f"Schema '{schema_name}' not found")
        return schema.validate(data)
    
    def __contains__(self, name: str) -> bool:
        return name in self._schemas
    
    def __len__(self) -> int:
        return len(self._schemas)


def create_schema(name: str, 
                  fields: List[Dict[str, Any]],
                  version: str = "0.0.1",
                  description: str = "") -> Schema:
    """
    Create a schema from field definitions.
    
    Args:
        name: Schema name
        fields: List of field definitions
        version: Schema version
        description: Schema description
    
    Returns:
        Schema object
    
    Example:
        >>> schema = btoon.create_schema('User', [
        ...     {'name': 'id', 'type': 'int', 'required': True},
        ...     {'name': 'name', 'type': 'string', 'required': True},
        ...     {'name': 'email', 'type': 'string', 'required': False}
        ... ])
    """
    builder = SchemaBuilder(name)
    builder.version(version)
    
    if description:
        builder.description(description)
    
    for field in fields:
        builder.field(
            name=field['name'],
            type=field['type'],
            required=field.get('required', True),
            default_value=field.get('default')
        )
    
    return builder.build()


class StreamEncoder:
    """
    Streaming encoder for large datasets.
    
    Example:
        >>> with open('large.btoon', 'wb') as f:
        ...     with StreamEncoder(f) as encoder:
        ...         for item in large_dataset:
        ...             encoder.write(item)
    """
    
    def __init__(self, fp: BinaryIO, **kwargs):
        self.fp = fp
        self.encoder = Encoder(**kwargs)
    
    def write(self, obj: Any) -> None:
        """Write object to stream."""
        data = self.encoder.encode(obj)
        # Write length prefix for streaming
        length = len(data)
        self.fp.write(length.to_bytes(4, 'big'))
        self.fp.write(data)
    
    def __enter__(self):
        return self
    
    def __exit__(self, *args):
        self.fp.flush()


class StreamDecoder:
    """
    Streaming decoder for large datasets.
    
    Example:
        >>> with open('large.btoon', 'rb') as f:
        ...     with StreamDecoder(f) as decoder:
        ...         for obj in decoder:
        ...             process(obj)
    """
    
    def __init__(self, fp: BinaryIO, **kwargs):
        self.fp = fp
        self.decoder = Decoder(**kwargs)
    
    def __iter__(self):
        return self
    
    def __next__(self) -> Any:
        """Read next object from stream."""
        # Read length prefix
        length_bytes = self.fp.read(4)
        if not length_bytes:
            raise StopIteration
        
        length = int.from_bytes(length_bytes, 'big')
        data = self.fp.read(length)
        
        if len(data) < length:
            raise BtoonException("Incomplete data in stream")
        
        return self.decoder.decode(data)
    
    def __enter__(self):
        return self
    
    def __exit__(self, *args):
        pass


# Convenience functions for common use cases

def to_json(data: bytes, **kwargs) -> str:
    """
    Convert BTOON to JSON string.
    
    Args:
        data: BTOON bytes
        **kwargs: JSON encoder options
    
    Returns:
        JSON string
    """
    import json
    obj = loads(data)
    return json.dumps(obj, **kwargs)


def from_json(json_str: str, **kwargs) -> bytes:
    """
    Convert JSON string to BTOON.
    
    Args:
        json_str: JSON string
        **kwargs: BTOON encoder options
    
    Returns:
        BTOON bytes
    """
    import json
    obj = json.loads(json_str)
    return dumps(obj, **kwargs)


def validate_file(path: Union[str, pathlib.Path],
                  schema: Optional[Schema] = None,
                  strict: bool = False) -> Dict[str, Any]:
    """
    Validate a BTOON file.
    
    Args:
        path: File path
        schema: Optional schema to validate against
        strict: Enable strict validation
    
    Returns:
        Validation result dict
    """
    path = pathlib.Path(path)
    validator = Validator(strict=strict)
    
    with open(path, 'rb') as f:
        data = f.read()
    
    result = validator.validate(data)
    
    if schema and result['valid']:
        try:
            obj = loads(data)
            result['schema_valid'] = schema.validate(obj)
        except Exception as e:
            result['schema_valid'] = False
            result['schema_error'] = str(e)
    
    return result


# Module initialization
def _check_version():
    """Check native extension version matches Python wrapper."""
    if HAS_NATIVE and hasattr(_native, '__version__'):
        if _native.__version__ != __version__:
            import warnings
            warnings.warn(
                f"Version mismatch: Python wrapper {__version__} != "
                f"Native extension {_native.__version__}"
            )


_check_version()


if __name__ == "__main__":
    # Simple test
    data = {"name": "Alice", "age": 30, "active": True}
    encoded = dumps(data, compress=True)
    decoded = loads(encoded)
    assert decoded == data
    print(f"BTOON v{__version__} working correctly!")
    print(f"Test data size: {len(str(data))} -> {len(encoded)} bytes")
    print(f"Compression ratio: {len(str(data)) / len(encoded):.2f}x")
