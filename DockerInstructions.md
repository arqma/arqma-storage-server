# Docker.md — Running `arqma-storage-server` in Docker

## 🧱 Requirements

- Docker (v20.10+)
- Internet access (to detect public IP and install packages)
- Optional: UFW or `iptables` installed for port management

---

## 🛠️ Build the Docker Image

Clone the repository and navigate to its root:

```bash
git clone https://github.com/arqma/arqma-storage-server.git
cd arqma-storage-server
```

Then build the Docker image:

```bash
docker build -t arqma-storage-server .
```

---

## 🚀 Run the Container

Simply run the container using:

```bash
docker run --rm -p 19996:19996 arqma-storage-server
```

This will:
- Expose port `19996` from the container to your host
- Automatically detect your server's **public IP**
- Start `arqma-storage-server` with default RPC port `19994`

---

## 🌐 Open Port 19996 on Firewall

### Option 1: Using **UFW** (Ubuntu/Debian)

```bash
sudo ufw allow 19996/tcp
sudo ufw reload
```

### Option 2: Using **iptables**

```bash
sudo iptables -A INPUT -p tcp --dport 19996 -j ACCEPT
sudo iptables-save > /etc/iptables/rules.v4
```

> You can confirm it's open using:
```bash
sudo netstat -tulnp | grep 19996
```

---

## 📡 Public IP Detection

The container will automatically fetch the host’s public IP using:

```bash
curl -s https://api.ipify.org
```

This is used in the command:

```bash
arqma-storage-server <public-ip> 19996 --arqmad-rpc-port 19994
```

> ⚠️ **If you're behind NAT**, this IP may not be reachable externally. You can customize the IP by modifying `entrypoint.sh`.

---

## 🧹 Optional: Run in Background

You can detach and run the container in the background:

```bash
docker run -d -p 19996:19996 --name arqma arqma-storage-server
```

To stop it later:

```bash
docker stop arqma
```

---
