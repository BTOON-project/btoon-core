# BTOON Changelog

All notable changes to the BTOON format specification and reference implementation will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1] - 2025-11-15

### Added
- Initial BTOON format specification v0.1
- Complete type system with primitives, containers, and extensions
- MessagePack-compatible base encoding
- Built-in compression support (ZLIB, LZ4, ZSTD)
- Schema definition and validation system
- Schema versioning with evolution strategies
- Schema inference from data
- Tabular data extension for columnar encoding
- Security features:
  - Input validation framework
  - Size and depth limits
  - UTF-8 validation
  - Zip bomb detection
  - Buffer overflow protection
- Performance optimizations:
  - Memory pooling
  - SIMD acceleration support
  - Zero-copy APIs
- CLI tools:
  - btoon: Basic encoder/decoder
  - btoon-schema: Schema compiler
  - btoon-convert: Format converter
- Comprehensive test suite
- Fuzzing infrastructure
- Documentation:
  - Format specification
  - Quick reference guide
  - Implementation guide
  - Test vectors

### Security
- Implemented strict input validation
- Added protection against common attack vectors
- Integrated fuzzing for security testing

## [0.0.1] - 2025-10-01 (Pre-release)

### Added
- Basic type system implementation
- Core encoding/decoding functionality
- Initial C++ reference implementation
- Basic test coverage

### Known Issues
- No compression support
- Limited validation
- No schema support
- Missing documentation

## Versioning Policy

BTOON follows Semantic Versioning 2.0.0:

### Version Format: MAJOR.MINOR.PATCH

- **MAJOR**: Incompatible format changes
- **MINOR**: Backwards-compatible additions
- **PATCH**: Backwards-compatible bug fixes

### Compatibility Promise

1. **Within Major Version**: Full backward compatibility
   - v1.x decoders can read v1.0 data
   - v1.0 decoders can skip unknown v1.x extensions

2. **Across Major Versions**: Migration required
   - Tools provided for version migration
   - Clear migration guides

3. **Extension Types**: Always forward compatible
   - Unknown extensions can be preserved
   - Round-trip safety guaranteed

### Release Cycle

- **Patch releases**: As needed for bug fixes
- **Minor releases**: Quarterly for new features
- **Major releases**: Yearly or as needed

## Roadmap

### Version 0.1 (Q1 2025)
- [ ] Streaming support for large data
- [ ] Additional compression algorithms (Brotli, Snappy)
- [ ] Extended precision timestamps
- [ ] Decimal type for financial data
- [ ] Improved Python bindings
- [ ] GraphQL schema integration
- [ ] JSON Schema compatibility
- [ ] WebAssembly reference implementation
- [ ] Batch processing optimizations
- [ ] Differential encoding
- [ ] Native SIMD types
- [ ] Graph data structures
- [ ] Streaming protocol
- [ ] Built-in encryption
- [ ] Time-series optimizations

## Migration Notes

### From MessagePack
BTOON is largely compatible with MessagePack, with these differences:
- Extension type format (additional header)
- Compression support (built-in)
- Schema validation (optional)
- Security features (mandatory)

### From JSON
Use the btoon-convert tool:
```bash
btoon-convert input.json output.btoon
```

### From Protocol Buffers
Requires schema mapping:
```bash
btoon-schema import-proto schema.proto > schema.btoon
btoon-convert --schema schema.btoon data.pb data.btoon
```

## Deprecation Policy

1. **Deprecation Notice**: 6 months before removal
2. **Migration Tools**: Provided with deprecation
3. **Support Period**: 1 year after deprecation
4. **Clear Documentation**: Migration guides and examples

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for details on:
- Code of Conduct
- Development setup
- Submission process
- Coding standards
- Testing requirements

## Support

- **Bug Reports**: GitHub Issues
- **Security Issues**: security@btoon.net
- **Questions**: Stack Overflow tag `btoon`
- **Discussion**: Discord/Slack community

## License

The BTOON specification is released under the MIT License.
See [LICENSE](../LICENSE) for details.
