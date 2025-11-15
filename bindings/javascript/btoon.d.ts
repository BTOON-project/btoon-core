/**
 * BTOON TypeScript Definitions
 * Binary Tree Object Notation for JavaScript/TypeScript
 * @version 0.0.1
 */

declare module 'btoon' {
    /**
     * BTOON value types matching JavaScript types
     */
    export type BtoonValue = 
        | null
        | boolean
        | number
        | string
        | Uint8Array
        | BtoonValue[]
        | { [key: string]: BtoonValue }
        | Date
        | BtoonExtension;

    /**
     * Custom extension type
     */
    export interface BtoonExtension {
        type: number;
        data: Uint8Array;
    }

    /**
     * Compression algorithm options
     */
    export type CompressionAlgorithm = 'none' | 'zlib' | 'lz4' | 'zstd' | 'auto';

    /**
     * Compression level presets
     */
    export type CompressionLevel = 
        | 'fastest'
        | 'fast'
        | 'balanced'
        | 'high'
        | 'maximum'
        | number;  // Custom level 0-9

    /**
     * Encoding options
     */
    export interface EncodeOptions {
        /** Enable compression */
        compress?: boolean;
        /** Compression algorithm */
        compression?: CompressionAlgorithm;
        /** Compression level */
        level?: CompressionLevel;
        /** Minimum size to compress */
        minCompressionSize?: number;
        /** Use adaptive compression */
        adaptive?: boolean;
        /** Custom type encoder */
        typeEncoder?: (value: any) => BtoonExtension | undefined;
    }

    /**
     * Decoding options
     */
    export interface DecodeOptions {
        /** Auto-decompress if compressed */
        autoDecompress?: boolean;
        /** Strict validation */
        strict?: boolean;
        /** Convert to native types (Date, BigInt, etc.) */
        nativeTypes?: boolean;
        /** Custom type decoder */
        typeDecoder?: (ext: BtoonExtension) => any;
        /** Use BigInt for large integers */
        useBigInt?: boolean;
    }

    /**
     * Schema field definition
     */
    export interface SchemaField {
        name: string;
        type: 'null' | 'boolean' | 'int' | 'uint' | 'float' | 'string' | 
               'binary' | 'array' | 'map' | 'timestamp' | 'any';
        required?: boolean;
        default?: any;
        description?: string;
        constraints?: {
            min?: number;
            max?: number;
            pattern?: string;
            enum?: any[];
            minLength?: number;
            maxLength?: number;
        };
    }

    /**
     * Schema evolution strategy
     */
    export type EvolutionStrategy = 
        | 'strict'           // No changes allowed
        | 'additive'        // Only new optional fields
        | 'backward'        // New fields with defaults
        | 'flexible';       // Any changes

    /**
     * Schema definition
     */
    export interface SchemaDefinition {
        name: string;
        version?: string;
        description?: string;
        evolution?: EvolutionStrategy;
        fields: SchemaField[];
    }

    /**
     * Validation result
     */
    export interface ValidationResult {
        valid: boolean;
        errors: string[];
        warnings: string[];
        stats?: {
            totalSize: number;
            maxDepth: number;
            stringCount: number;
            binaryCount: number;
            arrayCount: number;
            mapCount: number;
        };
    }

    /**
     * Validation options
     */
    export interface ValidationOptions {
        maxDepth?: number;
        maxStringLength?: number;
        maxBinaryLength?: number;
        maxArraySize?: number;
        maxMapSize?: number;
        maxTotalSize?: number;
        requireUTF8?: boolean;
        allowNaN?: boolean;
        allowInfinity?: boolean;
    }

    /**
     * Main encoding function
     */
    export function encode(value: BtoonValue, options?: EncodeOptions): Uint8Array;

    /**
     * Main decoding function
     */
    export function decode(data: Uint8Array | ArrayBuffer, options?: DecodeOptions): BtoonValue;

    /**
     * Convenience function for encoding (like JSON.stringify)
     */
    export function stringify(value: any, options?: EncodeOptions): Uint8Array;

    /**
     * Convenience function for decoding (like JSON.parse)
     */
    export function parse(data: Uint8Array | ArrayBuffer, options?: DecodeOptions): any;

    /**
     * Encoder class for reusable encoding with options
     */
    export class Encoder {
        constructor(options?: EncodeOptions);
        
        /** Encode a value */
        encode(value: BtoonValue): Uint8Array;
        
        /** Encode with schema validation */
        encodeWithSchema(value: BtoonValue, schema: Schema): Uint8Array;
        
        /** Set compression options */
        setCompression(algorithm: CompressionAlgorithm, level?: CompressionLevel): this;
        
        /** Enable/disable compression */
        compress(enable: boolean): this;
    }

    /**
     * Decoder class for reusable decoding with options
     */
    export class Decoder {
        constructor(options?: DecodeOptions);
        
        /** Decode BTOON data */
        decode(data: Uint8Array | ArrayBuffer): BtoonValue;
        
        /** Decode with schema validation */
        decodeWithSchema(data: Uint8Array | ArrayBuffer, schema: Schema): BtoonValue;
        
        /** Enable strict mode */
        strict(enable: boolean): this;
    }

    /**
     * Schema class for data validation
     */
    export class Schema {
        constructor(definition: SchemaDefinition);
        
        /** Validate a value against the schema */
        validate(value: BtoonValue): boolean;
        
        /** Validate with detailed errors */
        validateWithErrors(value: BtoonValue): ValidationResult;
        
        /** Get schema name */
        getName(): string;
        
        /** Get schema version */
        getVersion(): string;
        
        /** Convert to JSON representation */
        toJSON(): SchemaDefinition;
        
        /** Create from JSON */
        static fromJSON(json: SchemaDefinition): Schema;
        
        /** Infer schema from data */
        static infer(value: BtoonValue, options?: {
            sampleSize?: number;
            inferConstraints?: boolean;
            strictTypes?: boolean;
        }): Schema;
    }

    /**
     * Schema builder with fluent interface
     */
    export class SchemaBuilder {
        constructor(name: string);
        
        /** Set version */
        version(version: string): this;
        
        /** Set description */
        description(desc: string): this;
        
        /** Set evolution strategy */
        evolution(strategy: EvolutionStrategy): this;
        
        /** Add a field */
        field(name: string, type: string, required?: boolean): this;
        
        /** Add required field */
        requiredField(name: string, type: string): this;
        
        /** Add optional field */
        optionalField(name: string, type: string, defaultValue?: any): this;
        
        /** Add field with constraints */
        constrainedField(
            name: string,
            type: string,
            constraints: SchemaField['constraints']
        ): this;
        
        /** Build the schema */
        build(): Schema;
    }

    /**
     * Schema registry for managing multiple schemas
     */
    export class SchemaRegistry {
        constructor();
        
        /** Register a schema */
        register(schema: Schema): void;
        
        /** Get schema by name */
        get(name: string): Schema | undefined;
        
        /** Get schema by name and version */
        getVersion(name: string, version: string): Schema | undefined;
        
        /** List all schemas */
        list(): string[];
        
        /** Validate against named schema */
        validate(value: BtoonValue, schemaName: string): boolean;
    }

    /**
     * Validator for BTOON data
     */
    export class Validator {
        constructor(options?: ValidationOptions);
        
        /** Quick validation check */
        isValid(data: Uint8Array | ArrayBuffer): boolean;
        
        /** Full validation with details */
        validate(data: Uint8Array | ArrayBuffer): ValidationResult;
        
        /** Validate a decoded value */
        validateValue(value: BtoonValue): ValidationResult;
    }

    /**
     * Stream encoder for large datasets
     */
    export class StreamEncoder {
        constructor(options?: EncodeOptions);
        
        /** Write a value to the stream */
        write(value: BtoonValue): Uint8Array;
        
        /** Flush any buffered data */
        flush(): Uint8Array | null;
        
        /** Reset the encoder */
        reset(): void;
    }

    /**
     * Stream decoder for large datasets
     */
    export class StreamDecoder {
        constructor(options?: DecodeOptions);
        
        /** Feed data to the decoder */
        feed(data: Uint8Array | ArrayBuffer): void;
        
        /** Try to read a value */
        read(): BtoonValue | null;
        
        /** Check if a complete value is available */
        hasValue(): boolean;
        
        /** Reset the decoder */
        reset(): void;
    }

    /**
     * Convert BTOON to JSON
     */
    export function toJSON(data: Uint8Array | ArrayBuffer): any;

    /**
     * Convert JSON to BTOON
     */
    export function fromJSON(value: any, options?: EncodeOptions): Uint8Array;

    /**
     * Convert BTOON to JSON string
     */
    export function toJSONString(
        data: Uint8Array | ArrayBuffer,
        space?: string | number
    ): string;

    /**
     * Convert JSON string to BTOON
     */
    export function fromJSONString(
        json: string,
        options?: EncodeOptions
    ): Uint8Array;

    /**
     * Compare two BTOON values for equality
     */
    export function equals(a: Uint8Array, b: Uint8Array): boolean;

    /**
     * Get size of encoded value without encoding
     */
    export function sizeof(value: BtoonValue): number;

    /**
     * Version information
     */
    export const version: string;
    export const formatVersion: string;

    /**
     * Default limits
     */
    export const limits: {
        readonly MAX_DEPTH: number;
        readonly MAX_STRING_LENGTH: number;
        readonly MAX_BINARY_LENGTH: number;
        readonly MAX_ARRAY_SIZE: number;
        readonly MAX_MAP_SIZE: number;
    };

    /**
     * BTOON specific error class
     */
    export class BtoonError extends Error {
        constructor(message: string, code?: string);
        readonly code?: string;
    }

    /**
     * Export all types for external use
     */
    export {
        BtoonValue as Value,
        BtoonExtension as Extension,
        BtoonError as Error
    };
}
