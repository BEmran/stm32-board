# Deployment notes

## Build
```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run
```bash
./build/gateway_main
```

## systemd (recommended)
1. Copy project to `/opt/cpp_gateway`
2. Build under `/opt/cpp_gateway/build`
3. Install the service:
```bash
sudo cp deploy/systemd/gateway.service /etc/systemd/system/yahboom-gateway.service
sudo systemctl daemon-reload
sudo systemctl enable --now yahboom-gateway.service
sudo journalctl -u yahboom-gateway.service -f
```

If you use `ctrl_thread_priority` (FIFO realtime), you may need:
- run service as root, or
- enable CAP_SYS_NICE as commented in the service file.
