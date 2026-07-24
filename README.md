# MixMind

**AI-assisted mixing plugin. VST3 / AU / Standalone.**

MixMind is a utility audio plugin that analyzes your mix in real time and delivers context-aware recommendations through a conversational interface. It runs as a standard DAW plugin with no audio output -- it listens, processes, and advises.

---

## Architecture

```
DAW (Ableton / Logic / etc.)
    |
    |--- MixMind.vst3 / .component
    |       |
    |       |-- AudioAnalyzer    Real-time FFT, LUFS, spectral bands
    |       |-- ApiClient        Sends analysis to proxy server
    |       |-- ChatComponent    Conversational UI in the DAW
    |       |-- ContextPanel     DAW transport info (BPM, time sig)
    |
    |--- MixMind Proxy (Node.js)
            |
            |-- POST /api/chat   Routes to Claude (Premium) or Groq (Standard)
            |-- POST /validate   License key validation
            |-- POST /admin/*    License generation / revocation
```

### DSP Engine (AudioAnalyzer)
- Real-time FFT analysis (2048-point, 11th order)
- Spectral energy bands: Bass (20-250 Hz), Mid (250-2000 Hz), High (2000+ Hz)
- LUFS integrated loudness and true-peak tracking
- Stereo width estimation
- DAW transport sync (BPM, time signature, playback state)

### AI Backend (Proxy Server)
- Tiered model routing: Claude Sonnet (Premium) or Groq/Llama (Standard)
- License key management with JSON-based store
- Audio analysis context injected into every AI request
- Health check endpoint for plugin connectivity

---

## Getting Started

### Prerequisites

| Tool | Minimum Version |
|------|----------------|
| CMake | 3.22 |
| JUCE | 8.x |
| Xcode (macOS) | 15+ |
| Node.js (proxy) | 18+ |

### Build the Plugin

```bash
# 1. Clone JUCE alongside this project
git clone https://github.com/juce-framework/JUCE.git

# 2. Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --target MixMind_VST3 --config Release
cmake --build build --target MixMind_AU --config Release

# 4. (Optional) Package installer
./build_pkg.sh
```

### Configure the Proxy

```bash
cd proxy
cp env.example .env
# Set ANTHROPIC_API_KEY and/or GROQ_API_KEY in .env
npm install
node server.js
```

The plugin connects to `localhost:3000` by default. Configure the proxy URL in `ApiClient.cpp` if deploying remotely.

---

## Project Structure

```
mixmind/
├── Source/                  C++ plugin source
│   ├── PluginProcessor.*    AudioProcessor entry point
│   ├── PluginEditor.*       Editor UI
│   ├── AudioAnalyzer.*      Real-time FFT / spectral analysis
│   ├── ApiClient.*          HTTP client for proxy server
│   ├── ChatComponent.*      AI chat interface
│   ├── ContextPanel.*        DAW context display
│   └── LookAndFeel.h        Custom UI theme
├── proxy/                   Node.js backend
│   ├── server.js            Express API server
│   ├── package.json         Dependencies
│   └── env.example          Environment template
├── CMakeLists.txt           Build configuration
├── build_pkg.sh             Installer packaging
└── README.md
```

---

## License Management

The proxy includes a built-in licensing system for distributing paid licenses:

```bash
# Generate a license (standard tier)
curl -X POST http://localhost:3000/admin/generate-license \
  -H "x-admin-secret: YOUR_SECRET" \
  -H "Content-Type: application/json" \
  -d '{"tier": "standard", "count": 1}'

# Premium tier
curl -X POST http://localhost:3000/admin/generate-license \
  -H "x-admin-secret: YOUR_SECRET" \
  -H "Content-Type: application/json" \
  -d '{"tier": "premium", "count": 1}'
```

---

## Roadmap

- Persistent user sessions across DAW restarts
- A/B comparison of EQ/compression settings
- Stem-level analysis for multi-track projects
- Cloud sync for session history
- Plugin-to-plugin communication for automated parameter adjustment

---

Built with JUCE. Powered by Claude and Groq.
