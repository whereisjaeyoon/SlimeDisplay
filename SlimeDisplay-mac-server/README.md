# SlimeDisplayServer

SlimeDisplayServer is a lightweight Swift-based server for the SlimeDisplay macOS virtual display system.

It acts as the central coordinator between the macOS display framework and remote browser clients. It manages display operations, local network discovery, WebRTC connections, and serves the React frontend.

## 1. Architecture

The server follows a lightweight modular architecture.

```text
SlimeDisplayServer/
│
├── SlimeDisplayServer.xcodeproj
│
├── SlimeDisplayServer/
│   │
│   ├── main.swift
│   │
│   ├── Server/
│   │   ├── Server.swift
│   │   └── Routes.swift
│   │
│   ├── Services/
│   │   ├── DisplayService.swift
│   │   └── StreamService.swift
│   │
│   ├── Network/
│   │   └── BonjourAdvertiser.swift
│   │
│   ├── WebRTC/
│   │   ├── PeerConnectionManager.swift
│   │   └── SignalingService.swift
│   │
│   ├── Adapters/
│   │   └── SDAdapter.swift
│   │
│   ├── Models/
│   │   ├── DisplayState.swift
│   │   └── StreamState.swift
│   │
│   └── Resources/
│       └── Public/
│           └── React Build Output
│
└── Tests/
```

## 2. Modules

### Server

Responsible for server initialization, HTTP routing, and static file serving.

| File | Responsibility |
|---|---|
| `main.swift` | Application entry point |
| `Server.swift` | Initializes and configures the server |
| `Routes.swift` | Defines HTTP API endpoints |

### Services

Contains application-level operations and coordinates the underlying components.

| File | Responsibility |
|---|---|
| `DisplayService.swift` | Manages virtual display lifecycle |
| `StreamService.swift` | Coordinates screen streaming lifecycle |

Services should not depend on HTTP request details. They should expose operations that can be called by the server layer.

### Network

Responsible for local network discovery.

| File | Responsibility |
|---|---|
| `BonjourAdvertiser.swift` | Publishes the SlimeDisplay service through Bonjour/mDNS |

Example service advertisement:

```text
Service Name: SlimeDisplay
Service Type: _slimedisplay._tcp.
Domain: local.
Port: 8080
```

The server advertises its availability so that compatible clients can discover it on the local network.

### WebRTC

Responsible for real-time peer connections and signaling.

| File | Responsibility |
|---|---|
| `PeerConnectionManager.swift` | Creates and manages WebRTC peer connections |
| `SignalingService.swift` | Coordinates SDP and ICE candidate exchange |

WebRTC is responsible for media transport. Signaling is used to establish and manage peer connections.

The signaling transport can be implemented independently using WebSocket or HTTP.

### Adapters

Provides an interface between the server and the existing macOS Framework.

| File | Responsibility |
|---|---|
| `SDAdapter.swift` | Wraps Framework operations for server-side services |

This separation prevents application services from depending directly on Framework implementation details.

### Models

Contains server-side state and data models.

| File | Responsibility |
|---|---|
| `DisplayState.swift` | Represents virtual display state |
| `StreamState.swift` | Represents streaming state |

Display and streaming states should be managed independently.

## 3. Network Architecture

The server uses separate technologies for discovery, control, and media transport.

| Technology | Responsibility |
|---|---|
| Bonjour / mDNS | Local network service discovery |
| HTTP | API requests and frontend delivery |
| WebRTC | Real-time video transport |
| SDP | Session negotiation |
| ICE | Network connectivity establishment |
| STUN / TURN | NAT traversal when required |

### Bonjour / mDNS

The server publishes a Bonjour service using the following service type:

```text
_slimedisplay._tcp.
```

Clients can browse the local network to discover available SlimeDisplay servers.

Useful terminal commands:

```bash
# Advertise a test service
dns-sd -R "SlimeDisplay-Test" _slimedisplay._tcp local. 8080

# Browse available services
dns-sd -B _slimedisplay._tcp local.

# Resolve a service
dns-sd -L "SlimeDisplay-Test" _slimedisplay._tcp local.
```

Note: Standard browser JavaScript cannot directly browse Bonjour services. Browser-based clients need a separate discovery mechanism or a native discovery component.

### WebRTC

WebRTC handles the actual media connection between the server and client.

The server is responsible for:

- PeerConnection lifecycle.
- SDP Offer / Answer processing.
- ICE candidate exchange.
- Video track transmission.
- Connection state management.

The signaling mechanism is separate from WebRTC media transport.

## 4. HTTP API

The following endpoints represent the planned server API.

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/display` | Get display information |
| POST | `/api/display` | Create virtual display |
| DELETE | `/api/display` | Destroy virtual display |
| POST | `/api/capture/start` | Start screen capture |
| POST | `/api/capture/stop` | Stop screen capture |
| POST | `/api/stream/start` | Start streaming |
| POST | `/api/stream/stop` | Stop streaming |

The exact request and response schemas will be defined as the API implementation evolves.

## 5. React Static File Serving

The server also hosts the production React frontend.

The frontend is built separately using Vite. Its build output is placed in:

```text
Resources/
└── Public/
    ├── index.html
    └── assets/
```

The HTTP server serves these files to browser clients.

For production deployment, the React build output must be included in the server's resources.

Client-side routing should use an `index.html` fallback for frontend routes, while `/api/*` paths remain reserved for API requests.

## 6. Application Lifecycle

```text
Server Startup
      │
      ▼
Initialize Services
      │
      ▼
Start HTTP Server
      │
      ▼
Publish Bonjour Service
      │
      ▼
Wait for Client Connection
      │
      ▼
Create Virtual Display
      │
      ▼
Start Screen Capture
      │
      ▼
Establish WebRTC Connection
      │
      ▼
Transmit Video
      │
      ▼
Stop Streaming
      │
      ▼
Stop Capture
      │
      ▼
Destroy Virtual Display
      │
      ▼
Server Shutdown
```

The shutdown sequence must respect the asynchronous lifecycle of display and capture operations.

## 7. Dependencies

| Dependency | Purpose |
|---|---|
| Swift | Server implementation |
| Vapor | HTTP server and static file serving |
| macOS Framework | Display management and video encoding |
| Bonjour / NetService | Local network service advertisement |
| WebRTC implementation | Peer connection and media transport |

Bonjour can use Apple's built-in APIs without requiring an additional third-party mDNS library.

## 8. Build and Run

### Build Frontend

```bash
cd ../frontend
npm install
npm run build
```

Copy the build output into the server resources:

```bash
rm -rf ../SlimeDisplayServer/SlimeDisplayServer/Resources/Public/*

cp -R dist/* ../SlimeDisplayServer/SlimeDisplayServer/Resources/Public/
```

### Build Server

Open the Xcode project:

```text
SlimeDisplayServer.xcodeproj
```

Select the `SlimeDisplayServer` scheme and build the project.

### Run

Run the generated server executable or launch it through Xcode.

The server starts its HTTP service, serves the frontend, and advertises its availability on the local network.

## 9. Design Principles

- **Lightweight:** Keep the architecture simple and avoid unnecessary abstractions.
- **Modular:** Separate networking, application services, WebRTC, and Framework integration.
- **Separation of Concerns:** Keep HTTP routing independent from application logic.
- **Dependency Injection:** Use protocols to decouple services from concrete implementations.
- **Independent Lifecycle Management:** Manage display, capture, and streaming states separately.
- **Reusable:** Keep the server independent of frontend implementation details.

