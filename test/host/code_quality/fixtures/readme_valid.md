# Test Project

## Testing

```bash
# Configure host tests
cmake -S test/host -B build/host

# Build host tests
cmake --build build/host

# Run all tests via CTest
ctest --test-dir build/host --output-on-failure
```

## Project Structure

```
project/
├── components/
│   ├── hw_hal/                 # Hardware abstraction layer
│   │   ├── include/
│   │   └── src/
```

The component at `components/hw_hal` provides hardware abstraction.
