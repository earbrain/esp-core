# ESP Core Examples

## Build and Run

```bash
# Run specific example
idf.py -DEXAMPLE=wifi build flash monitor          # WiFi & mDNS demo
idf.py -DEXAMPLE=smartconfig build flash monitor   # SmartConfig demo
idf.py -DEXAMPLE=logging build flash monitor       # Logging demo
idf.py -DEXAMPLE=metrics build flash monitor       # System metrics demo
idf.py -DEXAMPLE=tasks build flash monitor         # Task helpers demo
```

## Examples

- **wifi**: WiFi access point and mDNS service demonstration
- **smartconfig**: WiFi provisioning via smartphone app (ESPTouch)
- **logging**: Logging system with formatted output
- **metrics**: System heap metrics and memory monitoring
- **tasks**: FreeRTOS task helpers and parallel execution
