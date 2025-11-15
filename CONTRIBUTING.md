# Contributing to BTOON

Thank you for your interest in contributing to BTOON! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

By participating in this project, you agree to abide by our Code of Conduct:
- Be respectful and inclusive
- Welcome newcomers and help them get started
- Focus on constructive criticism
- Respect differing opinions and experiences

## How to Contribute

### Reporting Issues

1. **Check existing issues** first to avoid duplicates
2. **Use issue templates** when available
3. **Provide details**:
   - BTOON version
   - Platform/OS
   - Minimal reproducible example
   - Expected vs actual behavior
   - Error messages/stack traces

### Suggesting Features

1. **Open a discussion** before implementing major features
2. **Explain the use case** and benefits
3. **Consider compatibility** with existing implementations
4. **Propose specification changes** if needed

### Submitting Code

#### Development Setup

```bash
# Clone the repository
git clone https://github.com/BTOON-project/btoon-core.git
cd btoon-core

# Create a build directory
mkdir build && cd build

# Configure with all features
cmake .. -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_TOOLS=ON

# Build
make -j$(nproc)

# Run tests
make test
```

#### Code Style

**C++ Style Guide:**
- Use modern C++ features (C++20)
- Follow Google C++ Style Guide with exceptions:
  - 4 spaces for indentation
  - 120 character line limit
  - Underscores for member variables (e.g., `member_var_`)

**General Guidelines:**
- Write self-documenting code
- Add comments for complex logic
- Include unit tests for new features
- Update documentation

#### Testing Requirements

All code must include:
1. **Unit tests** covering normal cases
2. **Edge case tests** for boundary conditions
3. **Error tests** for invalid inputs
4. **Fuzz tests** for security-critical code
5. **Performance benchmarks** for optimizations

Run all tests:
```bash
# Unit tests
./btoon_tests

# Fuzz tests (if available)
./fuzz_decoder corpus/

# Benchmarks
./btoon_benchmark
```

#### Commit Guidelines

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): description

[optional body]

[optional footer]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation
- `style`: Code style
- `refactor`: Refactoring
- `perf`: Performance improvement
- `test`: Testing
- `build`: Build system
- `ci`: CI/CD
- `chore`: Maintenance

Examples:
```
feat(encoder): add SIMD optimization for memcpy
fix(decoder): handle truncated string data
docs(spec): clarify extension type format
```

### Pull Request Process

1. **Fork the repository** and create a feature branch
2. **Make your changes** following the guidelines
3. **Add tests** for your changes
4. **Update documentation** if needed
5. **Run tests locally** and ensure they pass
6. **Submit a pull request** with:
   - Clear title and description
   - Reference to related issues
   - Test results
   - Performance impact (if applicable)

#### PR Checklist

- [ ] Code follows style guidelines
- [ ] Self-review completed
- [ ] Comments added for complex code
- [ ] Documentation updated
- [ ] Tests added and passing
- [ ] No new compiler warnings
- [ ] Performance impact assessed

### Specification Changes

Changes to the BTOON specification require:

1. **Proposal document** with:
   - Motivation and use cases
   - Detailed technical specification
   - Compatibility analysis
   - Implementation complexity

2. **Reference implementation**
3. **Test vectors**
4. **Documentation updates**
5. **Community review period** (minimum 2 weeks)

## Development Guidelines

### Security Considerations

- Always validate untrusted input
- Use bounds checking for buffer operations
- Avoid dynamic memory allocation in hot paths
- Implement defense in depth
- Add fuzzing for new parsers

### Performance Guidelines

- Profile before optimizing
- Prefer simplicity over micro-optimizations
- Document performance characteristics
- Provide benchmarks for optimizations
- Consider memory usage, not just speed

### Compatibility Guidelines

- Maintain backward compatibility within major versions
- Provide migration tools for breaking changes
- Test cross-platform compatibility
- Document platform-specific behavior
- Support common architectures (x86_64, ARM64)

## Project Structure

```
btoon-core/
├── include/btoon/     # Public headers
├── src/               # Implementation files
├── tests/            # Test suite
├── fuzz/             # Fuzzing harnesses
├── examples/         # Usage examples
├── tools/            # CLI tools
├── docs/             # Documentation
└── cmake/            # Build configuration
```

## Review Process

### Code Review

All contributions require review:
1. **Automated checks** must pass (CI/CD)
2. **One maintainer approval** for minor changes
3. **Two maintainer approvals** for major changes
4. **Specification changes** require broader review

### Review Criteria

- Correctness and completeness
- Test coverage
- Documentation quality
- Performance impact
- Security implications
- Code maintainability

## Release Process

1. **Version bump** following semantic versioning
2. **Update changelog** with all changes
3. **Run full test suite** including fuzzing
4. **Build binaries** for supported platforms
5. **Tag release** and create GitHub release
6. **Update documentation** website
7. **Announce** to community

## Getting Help

- **Discord/Slack**: Real-time discussion
- **GitHub Discussions**: Questions and ideas
- **Stack Overflow**: Tag `btoon`
- **Email**: dev@btoon.org

## Recognition

Contributors are recognized in:
- Git history
- CONTRIBUTORS file
- Release notes
- Project website

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
