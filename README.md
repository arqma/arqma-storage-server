# arqma-storage-server
Storage server for Arqma Service Nodes

Requirements:

Boost >= 1.66 (for boost.beast)

OpenSSL >= 1.1.1a (for X25519 curves)

sodium >= 1.0.16 (for ed25119 to curve25519 conversion)

Building from git clone:
```
git submodule update --init
mkdir build && cd build
USE_SINGLE_BUILDDIR=1 make release-all
cd build/release/binaries
```

To run the storage-server:
```
./arqma-storage 0.0.0.0 19996 --arqmq-port 19996
```
Replace the 0.0.0.0 with your IP of the hardware running the Storage-Server
Ensure your ports are open to allow communication to other network Storage-servers
The paths for Boost and OpenSSL can be specified by exporting the variables in the terminal before running make:

```
export OPENSSL_ROOT_DIR = ...
export BOOST_ROOT= ...
```

To get command line options just run:
```
./arqma-storage --help
```

Then using something like Postman (https://www.getpostman.com/) you can hit the API:

# post data
```
HTTP POST http://127.0.0.1/store
body: "hello world"
headers:
- X-Arqma-recipient: "mypubkey"
- X-Arqma-ttl: "86400"
- X-Arqma-timestamp: "1540860811000"
- X-Arqma-pow-nonce: "xxxx..."
```
# get data
```
HTTP GET http://127.0.0.1/retrieve
headers:
- X-Arqma-recipient: "mypubkey"
- X-Arqma-last-hash: "" (optional)
```
