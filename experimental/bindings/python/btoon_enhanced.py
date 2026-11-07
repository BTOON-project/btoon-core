"""
Enhanced Python bindings for BTOON with improved features and Pythonic API.
"""

from typing import Any, Dict, List, Optional, Union, BinaryIO, AsyncIterator, TypeVar, Generic
from datetime import datetime, timezone, timedelta
from decimal import Decimal as PyDecimal
from pathlib import Path
from contextlib import contextmanager, asynccontextmanager
from enum import Enum
import asyncio
import json
import io
import struct
import warnings
from dataclasses import dataclass
from collections.abc import Mapping, Sequence

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    
try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False

# Import the C++ bindings
from _btoon_core import *

__version__ = "0.0.1"
__all__ = [
    # Core functions
    'encode', 'decode', 'dumps', 'loads',
    # Classes
    'Encoder', 'Decoder', 'StreamEncoder', 'StreamDecoder',
    'Schema', 'SchemaBuilder', 'Validator',
    'Timestamp', 'Decimal', 'Currency',
    # Compression
    'CompressionAlgorithm', 'CompressionLevel', 'CompressionProfile',
    # Context managers
    'open_btoon', 'async_stream',
    # DataFrame integration
    'from_dataframe', 'to_dataframe',
    # NumPy integration
    'from_numpy', 'to_numpy'
]

# ============= Enums =============

class CompressionAlgorithm(Enum):
    """Compression algorithms supported by BTOON."""
    NONE = 255
    ZLIB = 0
    LZ4 = 1
    ZSTD = 2
    BROTLI = 3
    SNAPPY = 4
    AUTO = 254

class CompressionLevel(Enum):
    """Preset compression levels."""
    FASTEST = 1
    FAST = 2
    BALANCED = 3
    HIGH = 4
    MAXIMUM = 5

# ============= Enhanced Timestamp =============

class Timestamp:
    """
    Enhanced timestamp with nanosecond precision and timezone support.
    """
    
    def __init__(self, seconds: int = 0, nanoseconds: int = 0, 
                 timezone_offset: Optional[int] = None):
        """
        Create a timestamp.
        
        Args:
            seconds: Unix epoch seconds
            nanoseconds: Additional nanoseconds (0-999999999)
            timezone_offset: Timezone offset in minutes from UTC
        """
        self.seconds = seconds
        self.nanoseconds = nanoseconds
        self.timezone_offset = timezone_offset
        
    @classmethod
    def now(cls, tz: Optional[timezone] = None) -> 'Timestamp':
        """Get current timestamp."""
        dt = datetime.now(tz or timezone.utc)
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        delta = dt - epoch
        
        seconds = int(delta.total_seconds())
        nanoseconds = delta.microseconds * 1000
        
        tz_offset = None
        if tz:
            offset = tz.utcoffset(dt)
            if offset:
                tz_offset = int(offset.total_seconds() / 60)
                
        return cls(seconds, nanoseconds, tz_offset)
    
    @classmethod
    def from_datetime(cls, dt: datetime) -> 'Timestamp':
        """Convert from Python datetime."""
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        
        delta = dt - epoch
        seconds = int(delta.total_seconds())
        nanoseconds = delta.microseconds * 1000
        
        tz_offset = None
        if dt.tzinfo:
            offset = dt.tzinfo.utcoffset(dt)
            if offset:
                tz_offset = int(offset.total_seconds() / 60)
                
        return cls(seconds, nanoseconds, tz_offset)
    
    def to_datetime(self) -> datetime:
        """Convert to Python datetime."""
        dt = datetime.fromtimestamp(self.seconds, tz=timezone.utc)
        dt = dt.replace(microsecond=self.nanoseconds // 1000)
        
        if self.timezone_offset is not None:
            tz = timezone(timedelta(minutes=self.timezone_offset))
            dt = dt.astimezone(tz)
            
        return dt
    
    def __repr__(self) -> str:
        return f"Timestamp({self.seconds}, {self.nanoseconds}, {self.timezone_offset})"

# ============= Decimal Type =============

class Decimal:
    """
    Arbitrary precision decimal for financial calculations.
    """
    
    def __init__(self, value: Union[str, int, float, PyDecimal]):
        """Create a decimal value."""
        if isinstance(value, PyDecimal):
            self._value = value
        else:
            self._value = PyDecimal(str(value))
    
    @property
    def value(self) -> PyDecimal:
        return self._value
    
    def __add__(self, other: 'Decimal') -> 'Decimal':
        return Decimal(self._value + other._value)
    
    def __sub__(self, other: 'Decimal') -> 'Decimal':
        return Decimal(self._value - other._value)
    
    def __mul__(self, other: 'Decimal') -> 'Decimal':
        return Decimal(self._value * other._value)
    
    def __truediv__(self, other: 'Decimal') -> 'Decimal':
        return Decimal(self._value / other._value)
    
    def __str__(self) -> str:
        return str(self._value)
    
    def __repr__(self) -> str:
        return f"Decimal('{self._value}')"
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for encoding."""
        # Store as string to preserve precision
        return {"__decimal__": str(self._value)}
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'Decimal':
        """Create from dictionary."""
        return cls(data["__decimal__"])

class Currency(Decimal):
    """
    Currency type with fixed precision.
    """
    
    def __init__(self, amount: Union[str, int, float, PyDecimal], 
                 currency_code: str = "USD", precision: int = 2):
        super().__init__(amount)
        self.currency_code = currency_code
        self.precision = precision
        # Round to specified precision
        self._value = self._value.quantize(PyDecimal(10) ** -precision)
    
    def __repr__(self) -> str:
        return f"Currency('{self._value}', '{self.currency_code}')"
    
    def format(self, with_symbol: bool = True) -> str:
        """Format currency for display."""
        symbols = {
            "USD": "$", "EUR": "€", "GBP": "£", "JPY": "¥",
            "CNY": "¥", "INR": "₹", "KRW": "₩", "BTC": "₿"
        }
        symbol = symbols.get(self.currency_code, self.currency_code + " ")
        
        if with_symbol:
            return f"{symbol}{self._value:,.{self.precision}f}"
        else:
            return f"{self._value:,.{self.precision}f}"

# ============= Enhanced Encoder/Decoder =============

class EnhancedEncoder:
    """
    Enhanced encoder with additional Python types support.
    """
    
    def __init__(self, compression: Optional[CompressionAlgorithm] = None,
                 compression_level: CompressionLevel = CompressionLevel.BALANCED):
        self.compression = compression
        self.compression_level = compression_level
        
    def encode(self, obj: Any) -> bytes:
        """
        Encode Python object to BTOON.
        
        Supports additional types:
        - datetime -> Timestamp
        - Decimal -> encoded as extension
        - numpy arrays -> optimized encoding
        - pandas DataFrames -> tabular encoding
        """
        # Convert Python types to BTOON-compatible types
        converted = self._convert_python_types(obj)
        
        # Use base encoder
        options = {}
        if self.compression:
            options['compress'] = True
            options['compression_algorithm'] = self.compression.value
            options['compression_level'] = self.compression_level.value
            
        return encode(converted, **options)
    
    def _convert_python_types(self, obj: Any) -> Any:
        """Convert Python-specific types to BTOON types."""
        # Handle datetime
        if isinstance(obj, datetime):
            return Timestamp.from_datetime(obj).to_dict()
        
        # Handle Decimal
        if isinstance(obj, (Decimal, Currency)):
            return obj.to_dict()
        
        # Handle numpy arrays
        if HAS_NUMPY and isinstance(obj, np.ndarray):
            return self._encode_numpy(obj)
        
        # Handle pandas DataFrames
        if HAS_PANDAS and isinstance(obj, pd.DataFrame):
            return self._encode_dataframe(obj)
        
        # Handle collections recursively
        if isinstance(obj, Mapping):
            return {k: self._convert_python_types(v) for k, v in obj.items()}
        if isinstance(obj, Sequence) and not isinstance(obj, (str, bytes)):
            return [self._convert_python_types(item) for item in obj]
            
        return obj
    
    def _encode_numpy(self, arr: np.ndarray) -> Dict[str, Any]:
        """Encode numpy array efficiently."""
        return {
            "__numpy__": {
                "shape": arr.shape,
                "dtype": str(arr.dtype),
                "data": arr.tobytes(),
                "order": "C" if arr.flags['C_CONTIGUOUS'] else "F"
            }
        }
    
    def _encode_dataframe(self, df: pd.DataFrame) -> Dict[str, Any]:
        """Encode pandas DataFrame as tabular data."""
        return {
            "__dataframe__": {
                "columns": df.columns.tolist(),
                "index": df.index.tolist(),
                "data": df.to_dict('records')
            }
        }

class EnhancedDecoder:
    """
    Enhanced decoder with Python types reconstruction.
    """
    
    def decode(self, data: bytes) -> Any:
        """Decode BTOON data to Python objects."""
        # Use base decoder
        obj = decode(data)
        
        # Convert to Python types
        return self._convert_to_python_types(obj)
    
    def _convert_to_python_types(self, obj: Any) -> Any:
        """Convert BTOON types to Python types."""
        if isinstance(obj, dict):
            # Check for special types
            if "__decimal__" in obj:
                return Decimal.from_dict(obj)
            if "__numpy__" in obj and HAS_NUMPY:
                return self._decode_numpy(obj["__numpy__"])
            if "__dataframe__" in obj and HAS_PANDAS:
                return self._decode_dataframe(obj["__dataframe__"])
            
            # Recurse for regular dicts
            return {k: self._convert_to_python_types(v) for k, v in obj.items()}
            
        if isinstance(obj, list):
            return [self._convert_to_python_types(item) for item in obj]
            
        return obj
    
    def _decode_numpy(self, data: Dict[str, Any]) -> np.ndarray:
        """Decode numpy array."""
        arr = np.frombuffer(data["data"], dtype=data["dtype"])
        arr = arr.reshape(data["shape"])
        if data["order"] == "F":
            arr = np.asfortranarray(arr)
        return arr
    
    def _decode_dataframe(self, data: Dict[str, Any]) -> pd.DataFrame:
        """Decode pandas DataFrame."""
        df = pd.DataFrame(data["data"], columns=data["columns"])
        if data["index"]:
            df.index = data["index"]
        return df

# ============= Async Streaming =============

class AsyncStreamEncoder:
    """
    Asynchronous streaming encoder.
    """
    
    def __init__(self, writer: asyncio.StreamWriter, **options):
        self.writer = writer
        self.options = options
        
    async def write(self, obj: Any) -> None:
        """Write object to stream."""
        encoder = EnhancedEncoder(**self.options)
        data = encoder.encode(obj)
        
        # Write length prefix
        length = len(data)
        self.writer.write(struct.pack('>I', length))
        self.writer.write(data)
        await self.writer.drain()
        
    async def close(self) -> None:
        """Close the stream."""
        self.writer.close()
        await self.writer.wait_closed()

class AsyncStreamDecoder:
    """
    Asynchronous streaming decoder.
    """
    
    def __init__(self, reader: asyncio.StreamReader):
        self.reader = reader
        self.decoder = EnhancedDecoder()
        
    async def __aiter__(self) -> AsyncIterator[Any]:
        """Iterate over stream objects."""
        while True:
            # Read length prefix
            length_data = await self.reader.read(4)
            if not length_data:
                break
                
            length = struct.unpack('>I', length_data)[0]
            
            # Read data
            data = await self.reader.read(length)
            if len(data) != length:
                raise IOError("Incomplete read from stream")
                
            yield self.decoder.decode(data)

@asynccontextmanager
async def async_stream(host: str, port: int, mode: str = 'r', **options):
    """
    Async context manager for BTOON streaming.
    
    Usage:
        async with async_stream('localhost', 8888, 'w') as stream:
            await stream.write({'key': 'value'})
    """
    if mode == 'w':
        reader, writer = await asyncio.open_connection(host, port)
        encoder = AsyncStreamEncoder(writer, **options)
        try:
            yield encoder
        finally:
            await encoder.close()
    else:
        reader, writer = await asyncio.open_connection(host, port)
        decoder = AsyncStreamDecoder(reader)
        try:
            yield decoder
        finally:
            writer.close()
            await writer.wait_closed()

# ============= Context Managers =============

@contextmanager
def open_btoon(path: Union[str, Path], mode: str = 'r', **options):
    """
    Open a BTOON file for reading or writing.
    
    Usage:
        with open_btoon('data.btoon', 'w') as f:
            f.write({'key': 'value'})
            
        with open_btoon('data.btoon', 'r') as f:
            data = f.read()
    """
    path = Path(path)
    
    if 'r' in mode:
        with open(path, 'rb') as f:
            decoder = EnhancedDecoder()
            
            class Reader:
                def read(self):
                    return decoder.decode(f.read())
                
                def read_all(self):
                    results = []
                    while True:
                        try:
                            # Read length-prefixed messages
                            length_data = f.read(4)
                            if not length_data:
                                break
                            length = struct.unpack('>I', length_data)[0]
                            data = f.read(length)
                            results.append(decoder.decode(data))
                        except:
                            break
                    return results
            
            yield Reader()
    else:
        with open(path, 'wb' if 'w' in mode else 'ab') as f:
            encoder = EnhancedEncoder(**options)
            
            class Writer:
                def write(self, obj):
                    data = encoder.encode(obj)
                    if options.get('streaming', False):
                        # Write with length prefix for streaming
                        f.write(struct.pack('>I', len(data)))
                    f.write(data)
                    
                def write_many(self, objects):
                    for obj in objects:
                        self.write(obj)
            
            yield Writer()

# ============= DataFrame Integration =============

def from_dataframe(df: pd.DataFrame, **options) -> bytes:
    """
    Convert pandas DataFrame to BTOON with optimized tabular encoding.
    """
    if not HAS_PANDAS:
        raise ImportError("pandas is required for DataFrame support")
        
    options['use_tabular'] = True
    encoder = EnhancedEncoder(**options)
    return encoder.encode(df)

def to_dataframe(data: bytes) -> pd.DataFrame:
    """
    Convert BTOON data to pandas DataFrame.
    """
    if not HAS_PANDAS:
        raise ImportError("pandas is required for DataFrame support")
        
    decoder = EnhancedDecoder()
    obj = decoder.decode(data)
    
    if isinstance(obj, pd.DataFrame):
        return obj
    elif isinstance(obj, dict) and "__dataframe__" in obj:
        return decoder._decode_dataframe(obj["__dataframe__"])
    elif isinstance(obj, list) and obj and isinstance(obj[0], dict):
        # Convert list of records to DataFrame
        return pd.DataFrame(obj)
    else:
        raise ValueError("Data cannot be converted to DataFrame")

# ============= NumPy Integration =============

def from_numpy(arr: np.ndarray, **options) -> bytes:
    """
    Convert numpy array to BTOON with optimized encoding.
    """
    if not HAS_NUMPY:
        raise ImportError("numpy is required for array support")
        
    encoder = EnhancedEncoder(**options)
    return encoder.encode(arr)

def to_numpy(data: bytes, dtype: Optional[np.dtype] = None) -> np.ndarray:
    """
    Convert BTOON data to numpy array.
    """
    if not HAS_NUMPY:
        raise ImportError("numpy is required for array support")
        
    decoder = EnhancedDecoder()
    obj = decoder.decode(data)
    
    if isinstance(obj, np.ndarray):
        return obj
    elif isinstance(obj, dict) and "__numpy__" in obj:
        return decoder._decode_numpy(obj["__numpy__"])
    elif isinstance(obj, list):
        # Convert list to array
        arr = np.array(obj)
        if dtype:
            arr = arr.astype(dtype)
        return arr
    else:
        raise ValueError("Data cannot be converted to numpy array")

# ============= Convenience Functions =============

def dumps(obj: Any, **options) -> bytes:
    """Serialize object to BTOON bytes."""
    encoder = EnhancedEncoder(**options)
    return encoder.encode(obj)

def loads(data: bytes) -> Any:
    """Deserialize BTOON bytes to object."""
    decoder = EnhancedDecoder()
    return decoder.decode(data)

def dump(obj: Any, fp: BinaryIO, **options) -> None:
    """Serialize object to BTOON file."""
    data = dumps(obj, **options)
    fp.write(data)

def load(fp: BinaryIO) -> Any:
    """Deserialize BTOON file to object."""
    data = fp.read()
    return loads(data)

# ============= Schema Integration =============

@dataclass
class SchemaField:
    """Schema field definition."""
    name: str
    type: str
    required: bool = True
    default: Any = None
    description: Optional[str] = None
    constraints: Optional[Dict[str, Any]] = None

class SchemaBuilder:
    """
    Fluent schema builder.
    
    Usage:
        schema = (SchemaBuilder()
                  .name("UserProfile")
                  .version("1.0.0")
                  .field("id", "integer", required=True)
                  .field("name", "string", required=True, max_length=100)
                  .field("email", "string", pattern=r"^[^@]+@[^@]+$")
                  .field("balance", "decimal", precision=2)
                  .build())
    """
    
    def __init__(self):
        self._name = ""
        self._version = "1.0.0"
        self._fields = []
        self._description = ""
        
    def name(self, name: str) -> 'SchemaBuilder':
        self._name = name
        return self
        
    def version(self, version: str) -> 'SchemaBuilder':
        self._version = version
        return self
        
    def description(self, desc: str) -> 'SchemaBuilder':
        self._description = desc
        return self
        
    def field(self, name: str, field_type: str, **kwargs) -> 'SchemaBuilder':
        field = SchemaField(name=name, type=field_type, **kwargs)
        self._fields.append(field)
        return self
        
    def build(self) -> 'Schema':
        # Return Schema object (would be C++ binding)
        return Schema(self._name, self._version, self._fields)

# ============= Export enhanced API =============

# Override base module functions with enhanced versions
_original_encode = encode
_original_decode = decode

def encode(obj: Any, **options) -> bytes:
    """Enhanced encode with Python types support."""
    encoder = EnhancedEncoder(**options)
    return encoder.encode(obj)

def decode(data: bytes) -> Any:
    """Enhanced decode with Python types support."""
    decoder = EnhancedDecoder()
    return decoder.decode(data)
