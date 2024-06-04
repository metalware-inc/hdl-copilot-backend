# HDL Copilot

VSCode plugin backed by a C++ LSP server.

Front-end source code [here](https://github.com/metalware-inc/hdl-copilot-frontend/blob/main/bin/linux/unpack.sh#L7-L12).

## Building

```
cd server
./build.sh
```

For linux, create the app image:
```
./make_linux_app.sh
```

Name the resulting AppImage appropriately so it can be found by the [VSCode extension](https://github.com/metalware-inc/hdl-copilot-frontend/blob/main/bin/linux/unpack.sh#L7-L12).

## Unit tests

```
./build/tests/hdl_copilot_server_tests
```

## Integration tests

From the `tests` directory:

```
./run_integration_tests.sh
```
