# RemoteOps – Remote System Monitoring and Management Tool

**Course:** IE3090 Network Programming
**Registration Number:** IT24103500

## Project Configuration

* **Agent Port:** 9410
* **UDP Monitoring Port:** 9411
* **Agent Source:** `agent_500.c`
* **Controller Source:** `controller_500.c`
* **Session ID (SID):** 0053
* **Authentication Token:** `OPS-3500`
* **Log File:** `remoteops.log`
* **Storage Directory:** `./agentfiles/`

## Project Description

RemoteOps is a TCP/IP-based remote system monitoring and management tool consisting of an Agent and a Controller.

The Agent runs on the managed machine, while the Controller is used by the administrator to connect to and manage the Agent.

## Implemented Features

* Token-based authentication
* Session ID handling
* TCP client-server communication
* Multiple concurrent controller connections using POSIX threads
* System information monitoring
* Process listing
* Whitelisted command execution
* PUT file upload
* GET file download
* Byte-level file transfer verification
* UDP-based monitoring
* Persistent event logging
* Error responses for invalid or unsupported commands

## Compilation

Compile the Agent:

```bash
gcc agent_500.c -o agent_500 -lpthread
```

Compile the Controller:

```bash
gcc controller_500.c -o controller_500
```

## Execution

Start the Agent:

```bash
./agent_500
```

Start the Controller:

```bash
./controller_500
```

The Agent listens for TCP controller connections on port `9410`.

UDP monitoring uses port `9411`.

## File Transfer

Uploaded files are stored under:

```text
./agentfiles/
```

## Logging

Runtime events are persistently recorded in:

```text
remoteops.log
```
