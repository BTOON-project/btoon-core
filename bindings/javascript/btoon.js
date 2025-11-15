/**
 * BTOON - Binary Tree Object Notation for JavaScript
 * @version 0.0.1
 * @license MIT
 */

(function (root, factory) {
    if (typeof define === 'function' && define.amd) {
        // AMD
        define([], factory);
    } else if (typeof module === 'object' && module.exports) {
        // Node.js
        module.exports = factory();
    } else {
        // Browser
        root.btoon = factory();
    }
}(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    const VERSION = '0.0.1';
    const FORMAT_VERSION = '0.0.1';

    // Default limits
    const limits = Object.freeze({
        MAX_DEPTH: 128,
        MAX_STRING_LENGTH: 10 * 1024 * 1024,  // 10MB
        MAX_BINARY_LENGTH: 100 * 1024 * 1024, // 100MB
        MAX_ARRAY_SIZE: 1000000,
        MAX_MAP_SIZE: 100000
    });

    /**
     * BTOON Error class
     */
    class BtoonError extends Error {
        constructor(message, code) {
            super(message);
            this.name = 'BtoonError';
            this.code = code;
        }
    }

    /**
     * Type detection helpers
     */
    const TypeDetector = {
        isNull: (v) => v === null,
        isBoolean: (v) => typeof v === 'boolean',
        isNumber: (v) => typeof v === 'number' && !isNaN(v),
        isString: (v) => typeof v === 'string',
        isArray: (v) => Array.isArray(v),
        isObject: (v) => v !== null && typeof v === 'object' && !Array.isArray(v),
        isUint8Array: (v) => v instanceof Uint8Array,
        isArrayBuffer: (v) => v instanceof ArrayBuffer,
        isDate: (v) => v instanceof Date,
        isBigInt: (v) => typeof v === 'bigint',
        isMap: (v) => v instanceof Map,
        isSet: (v) => v instanceof Set
    };

    /**
     * Encoder class
     */
    class Encoder {
        constructor(options = {}) {
            this.options = {
                compress: false,
                compression: 'auto',
                level: 'balanced',
                minCompressionSize: 1024,
                adaptive: false,
                typeEncoder: null,
                ...options
            };
        }

        /**
         * Encode a value to BTOON
         */
        encode(value) {
            const buffer = this._encodeValue(value);
            
            // Apply compression if enabled
            if (this.options.compress && buffer.length >= this.options.minCompressionSize) {
                return this._compress(buffer);
            }
            
            return buffer;
        }

        /**
         * Encode with schema validation
         */
        encodeWithSchema(value, schema) {
            if (!schema.validate(value)) {
                throw new BtoonError('Value does not match schema');
            }
            return this.encode(value);
        }

        /**
         * Internal: Encode a single value
         */
        _encodeValue(value, depth = 0) {
            if (depth > limits.MAX_DEPTH) {
                throw new BtoonError('Maximum nesting depth exceeded');
            }

            // Check custom type encoder
            if (this.options.typeEncoder) {
                const custom = this.options.typeEncoder(value);
                if (custom) {
                    return this._encodeExtension(custom.type, custom.data);
                }
            }

            // Handle native types
            if (TypeDetector.isNull(value)) {
                return new Uint8Array([0xc0]); // nil
            }
            
            if (TypeDetector.isBoolean(value)) {
                return new Uint8Array([value ? 0xc3 : 0xc2]);
            }
            
            if (TypeDetector.isNumber(value)) {
                return this._encodeNumber(value);
            }
            
            if (TypeDetector.isString(value)) {
                return this._encodeString(value);
            }
            
            if (TypeDetector.isUint8Array(value)) {
                return this._encodeBinary(value);
            }
            
            if (TypeDetector.isArrayBuffer(value)) {
                return this._encodeBinary(new Uint8Array(value));
            }
            
            if (TypeDetector.isDate(value)) {
                return this._encodeDate(value);
            }
            
            if (TypeDetector.isBigInt(value)) {
                return this._encodeBigInt(value);
            }
            
            if (TypeDetector.isArray(value)) {
                return this._encodeArray(value, depth);
            }
            
            if (TypeDetector.isMap(value)) {
                return this._encodeMapObject(value, depth);
            }
            
            if (TypeDetector.isSet(value)) {
                return this._encodeArray(Array.from(value), depth);
            }
            
            if (TypeDetector.isObject(value)) {
                return this._encodeObject(value, depth);
            }
            
            throw new BtoonError(`Cannot encode value of type: ${typeof value}`);
        }

        _encodeNumber(num) {
            // Integer encoding
            if (Number.isInteger(num)) {
                // Positive fixint
                if (num >= 0 && num <= 127) {
                    return new Uint8Array([num]);
                }
                // Negative fixint
                if (num >= -32 && num < 0) {
                    return new Uint8Array([0xe0 | (num + 32)]);
                }
                // Larger integers
                if (num >= 0) {
                    if (num <= 255) {
                        return new Uint8Array([0xcc, num]);
                    }
                    if (num <= 65535) {
                        return new Uint8Array([0xcd, (num >> 8) & 0xff, num & 0xff]);
                    }
                    if (num <= 0xffffffff) {
                        return new Uint8Array([
                            0xce,
                            (num >> 24) & 0xff,
                            (num >> 16) & 0xff,
                            (num >> 8) & 0xff,
                            num & 0xff
                        ]);
                    }
                } else {
                    if (num >= -128) {
                        return new Uint8Array([0xd0, num & 0xff]);
                    }
                    if (num >= -32768) {
                        return new Uint8Array([0xd1, (num >> 8) & 0xff, num & 0xff]);
                    }
                    if (num >= -2147483648) {
                        return new Uint8Array([
                            0xd2,
                            (num >> 24) & 0xff,
                            (num >> 16) & 0xff,
                            (num >> 8) & 0xff,
                            num & 0xff
                        ]);
                    }
                }
            }
            
            // Float encoding (float64)
            const buffer = new ArrayBuffer(9);
            const view = new DataView(buffer);
            view.setUint8(0, 0xcb);
            view.setFloat64(1, num, false); // big-endian
            return new Uint8Array(buffer);
        }

        _encodeString(str) {
            if (str.length > limits.MAX_STRING_LENGTH) {
                throw new BtoonError('String exceeds maximum length');
            }
            
            const utf8 = new TextEncoder().encode(str);
            const len = utf8.length;
            
            if (len <= 31) {
                // fixstr
                const buffer = new Uint8Array(1 + len);
                buffer[0] = 0xa0 | len;
                buffer.set(utf8, 1);
                return buffer;
            } else if (len <= 255) {
                // str8
                const buffer = new Uint8Array(2 + len);
                buffer[0] = 0xd9;
                buffer[1] = len;
                buffer.set(utf8, 2);
                return buffer;
            } else if (len <= 65535) {
                // str16
                const buffer = new Uint8Array(3 + len);
                buffer[0] = 0xda;
                buffer[1] = (len >> 8) & 0xff;
                buffer[2] = len & 0xff;
                buffer.set(utf8, 3);
                return buffer;
            } else {
                // str32
                const buffer = new Uint8Array(5 + len);
                buffer[0] = 0xdb;
                buffer[1] = (len >> 24) & 0xff;
                buffer[2] = (len >> 16) & 0xff;
                buffer[3] = (len >> 8) & 0xff;
                buffer[4] = len & 0xff;
                buffer.set(utf8, 5);
                return buffer;
            }
        }

        _encodeBinary(data) {
            if (data.length > limits.MAX_BINARY_LENGTH) {
                throw new BtoonError('Binary data exceeds maximum length');
            }
            
            const len = data.length;
            
            if (len <= 255) {
                // bin8
                const buffer = new Uint8Array(2 + len);
                buffer[0] = 0xc4;
                buffer[1] = len;
                buffer.set(data, 2);
                return buffer;
            } else if (len <= 65535) {
                // bin16
                const buffer = new Uint8Array(3 + len);
                buffer[0] = 0xc5;
                buffer[1] = (len >> 8) & 0xff;
                buffer[2] = len & 0xff;
                buffer.set(data, 3);
                return buffer;
            } else {
                // bin32
                const buffer = new Uint8Array(5 + len);
                buffer[0] = 0xc6;
                buffer[1] = (len >> 24) & 0xff;
                buffer[2] = (len >> 16) & 0xff;
                buffer[3] = (len >> 8) & 0xff;
                buffer[4] = len & 0xff;
                buffer.set(data, 5);
                return buffer;
            }
        }

        _encodeArray(arr, depth) {
            if (arr.length > limits.MAX_ARRAY_SIZE) {
                throw new BtoonError('Array exceeds maximum size');
            }
            
            const elements = arr.map(v => this._encodeValue(v, depth + 1));
            const totalSize = elements.reduce((sum, el) => sum + el.length, 0);
            
            let header;
            const len = arr.length;
            
            if (len <= 15) {
                // fixarray
                header = new Uint8Array([0x90 | len]);
            } else if (len <= 65535) {
                // array16
                header = new Uint8Array([0xdc, (len >> 8) & 0xff, len & 0xff]);
            } else {
                // array32
                header = new Uint8Array([
                    0xdd,
                    (len >> 24) & 0xff,
                    (len >> 16) & 0xff,
                    (len >> 8) & 0xff,
                    len & 0xff
                ]);
            }
            
            const buffer = new Uint8Array(header.length + totalSize);
            buffer.set(header, 0);
            let offset = header.length;
            
            for (const element of elements) {
                buffer.set(element, offset);
                offset += element.length;
            }
            
            return buffer;
        }

        _encodeObject(obj, depth) {
            const entries = Object.entries(obj);
            return this._encodeMap(entries, depth);
        }

        _encodeMapObject(map, depth) {
            const entries = Array.from(map);
            return this._encodeMap(entries, depth);
        }

        _encodeMap(entries, depth) {
            if (entries.length > limits.MAX_MAP_SIZE) {
                throw new BtoonError('Map exceeds maximum size');
            }
            
            const pairs = [];
            let totalSize = 0;
            
            for (const [key, value] of entries) {
                const keyBuffer = this._encodeString(String(key));
                const valueBuffer = this._encodeValue(value, depth + 1);
                pairs.push(keyBuffer, valueBuffer);
                totalSize += keyBuffer.length + valueBuffer.length;
            }
            
            let header;
            const len = entries.length;
            
            if (len <= 15) {
                // fixmap
                header = new Uint8Array([0x80 | len]);
            } else if (len <= 65535) {
                // map16
                header = new Uint8Array([0xde, (len >> 8) & 0xff, len & 0xff]);
            } else {
                // map32
                header = new Uint8Array([
                    0xdf,
                    (len >> 24) & 0xff,
                    (len >> 16) & 0xff,
                    (len >> 8) & 0xff,
                    len & 0xff
                ]);
            }
            
            const buffer = new Uint8Array(header.length + totalSize);
            buffer.set(header, 0);
            let offset = header.length;
            
            for (const pair of pairs) {
                buffer.set(pair, offset);
                offset += pair.length;
            }
            
            return buffer;
        }

        _encodeDate(date) {
            // Encode as timestamp extension (type -1)
            const seconds = Math.floor(date.getTime() / 1000);
            const buffer = new ArrayBuffer(8);
            const view = new DataView(buffer);
            view.setUint32(0, Math.floor(seconds / 0x100000000), false);
            view.setUint32(4, seconds >>> 0, false);
            return this._encodeExtension(-1, new Uint8Array(buffer));
        }

        _encodeBigInt(bigint) {
            // Encode as BigInt extension (type -4)
            const hex = bigint.toString(16);
            const bytes = [];
            for (let i = 0; i < hex.length; i += 2) {
                bytes.push(parseInt(hex.substr(i, 2), 16));
            }
            return this._encodeExtension(-4, new Uint8Array(bytes));
        }

        _encodeExtension(type, data) {
            const len = data.length;
            
            if (len === 1) {
                // fixext1
                const buffer = new Uint8Array(3);
                buffer[0] = 0xd4;
                buffer[1] = type;
                buffer.set(data, 2);
                return buffer;
            } else if (len === 2) {
                // fixext2
                const buffer = new Uint8Array(4);
                buffer[0] = 0xd5;
                buffer[1] = type;
                buffer.set(data, 2);
                return buffer;
            } else if (len === 4) {
                // fixext4
                const buffer = new Uint8Array(6);
                buffer[0] = 0xd6;
                buffer[1] = type;
                buffer.set(data, 2);
                return buffer;
            } else if (len === 8) {
                // fixext8
                const buffer = new Uint8Array(10);
                buffer[0] = 0xd7;
                buffer[1] = type;
                buffer.set(data, 2);
                return buffer;
            } else if (len === 16) {
                // fixext16
                const buffer = new Uint8Array(18);
                buffer[0] = 0xd8;
                buffer[1] = type;
                buffer.set(data, 2);
                return buffer;
            } else if (len <= 255) {
                // ext8
                const buffer = new Uint8Array(3 + len);
                buffer[0] = 0xc7;
                buffer[1] = len;
                buffer[2] = type;
                buffer.set(data, 3);
                return buffer;
            } else if (len <= 65535) {
                // ext16
                const buffer = new Uint8Array(4 + len);
                buffer[0] = 0xc8;
                buffer[1] = (len >> 8) & 0xff;
                buffer[2] = len & 0xff;
                buffer[3] = type;
                buffer.set(data, 4);
                return buffer;
            } else {
                // ext32
                const buffer = new Uint8Array(6 + len);
                buffer[0] = 0xc9;
                buffer[1] = (len >> 24) & 0xff;
                buffer[2] = (len >> 16) & 0xff;
                buffer[3] = (len >> 8) & 0xff;
                buffer[4] = len & 0xff;
                buffer[5] = type;
                buffer.set(data, 6);
                return buffer;
            }
        }

        _compress(data) {
            // Compression would be implemented with pako or similar library
            // For now, return data as-is
            console.warn('Compression not implemented in pure JavaScript version');
            return data;
        }
    }

    /**
     * Main API
     */
    const btoon = {
        VERSION,
        FORMAT_VERSION,
        limits,
        BtoonError,
        Encoder,

        /**
         * Encode a value
         */
        encode(value, options) {
            const encoder = new Encoder(options);
            return encoder.encode(value);
        },

        /**
         * Convenience function like JSON.stringify
         */
        stringify(value, options) {
            return btoon.encode(value, options);
        },

        /**
         * Export for module systems
         */
        Error: BtoonError
    };

    return btoon;
}));
