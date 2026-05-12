# mcpplibs-tinyhttps

Minimal C++23 HTTP/HTTPS client library with SSE (Server-Sent Events) streaming support. Uses mbedTLS for TLS, zero external dependencies beyond that.

## Features

- HTTP/HTTPS client with connection pooling (keep-alive)
- SSE (Server-Sent Events) streaming
- Proxy support (HTTP CONNECT)
- C++23 modules

## Usage

```lua
-- xmake.lua
add_requires("mcpplibs-tinyhttps")
target("myapp")
    add_packages("mcpplibs-tinyhttps")
```

```cpp
import mcpplibs.tinyhttps;

auto client = mcpplibs::tinyhttps::HttpClient({});
auto resp = client.send(mcpplibs::tinyhttps::HttpRequest::post(
    "https://api.example.com/data",
    R"({"key": "value"})"
));
```

## 使用 mcpp 构建

### 添加依赖

```bash
mcpp add tinyhttps@0.2.2
```

或在 `mcpp.toml` 中手动添加：

```toml
[dependencies]
tinyhttps = "0.2.2"
```

### 构建

```bash
mcpp build
```

### 代码示例

```cpp
import mcpplibs.tinyhttps;
// ... usage example
```

## License

Apache-2.0
