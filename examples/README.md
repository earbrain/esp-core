# ESP Core Examples

## Build and Run

```bash
# Build and flash all examples
idf.py build flash monitor

# Run specific example
idf.py -DEXAMPLE_WIFI=1 build flash monitor     # WiFi & mDNS demo
idf.py -DEXAMPLE_LOGGING=1 build flash monitor
idf.py -DEXAMPLE_METRICS=1 build flash monitor
idf.py -DEXAMPLE_TASKS=1 build flash monitor
```
