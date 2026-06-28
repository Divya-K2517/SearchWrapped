# Search Wrapped 🔍

**Spotify Wrapped, but for your Google search history.**
Built with a C++17 REST backend and a vanilla HTML/CSS/JS frontend.

---

## Stack

| Layer     | Tech                                      |
|-----------|-------------------------------------------|
| Backend   | C++17, cpp-httplib (header-only HTTP server) |
| JSON      | nlohmann/json (header-only)               |
| Frontend  | Vanilla HTML + CSS + JS (zero deps)       |
| Analysis  | Custom C++ NLP + personality engine       |

---

## Features

- **Upload real data** — paste your Google Takeout `MyActivity.json`
- **7 story slides** — full Spotify Wrapped-style narrative flow
- **C++ analysis engine** including:
  - Query frequency ranking
  - Monthly activity heatmap (12 months)
  - Hourly search distribution (24 hours)
  - Day-of-week patterns
  - Theme detection across 9 categories (AI, Food, Science, Travel, etc.)
  - Top word extraction with stop-word filtering
  - Night owl % calculation (10pm–3am searches)
  - Question search detection (how/why/what/when/where)
  - **8 personality archetypes** detected from behavior patterns:
    - The Midnight Engineer
    - The Deep Diver
    - The Sensory Explorer
    - The Restless Wanderer
    - The Informed Citizen
    - The Strategic Mind
    - The Philosopher
    - The Generalist
- **Shareable card** — copy your identity card to clipboard

---

## Requirements

```bash
# Ubuntu/Debian
sudo apt-get install g++ make

# macOS
xcode-select --install
```

---

## Run

```bash
chmod +x start.sh
./start.sh
# Open http://localhost:8080
```

Or manually:

```bash
make
./search_wrapped_server
```

---

## Get Your Data

1. Go to [takeout.google.com](https://takeout.google.com)
2. Click "Deselect all", then check **My Activity**
3. Click "Multiple formats" → set My Activity format to **JSON**
4. Download and extract the archive
5. Find `Takeout/My Activity/Search/MyActivity.json`
6. Drop it into the app

---

## API

```
GET  /api/health       → { status, engine }
GET  /api/demo         → WrappedResult (demo data)
POST /api/analyze      → WrappedResult
     body: Google Takeout JSON array
```

---

## Project Structure

```
search_wrapped/
├── src/
│   └── main.cpp          # C++ backend (HTTP server + analysis engine)
├── frontend/
│   └── index.html        # Single-file frontend
├── include/
│   ├── httplib.h         # cpp-httplib (header-only)
│   └── json.hpp          # nlohmann/json (header-only)
├── Makefile
├── start.sh
└── README.md
```

---

*Made with C++ and Claude*
