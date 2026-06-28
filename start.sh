#!/bin/bash
set -e

echo "╔══════════════════════════════════════════╗"
echo "║        Search Wrapped — Full Stack       ║"
echo "║        C++ Backend + HTML Frontend       ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Build if needed
if [ ! -f "./search_wrapped_server" ]; then
  echo "🔨 Building C++ backend..."
  make
  echo ""
fi

echo "🚀 Starting server at http://localhost:8080"
echo "   Open http://localhost:8080 in your browser"
echo ""
echo "   API endpoints:"
echo "   GET  /api/health   — server status"
echo "   GET  /api/demo     — demo data (no file needed)"
echo "   POST /api/analyze  — upload your Takeout JSON"
echo ""
echo "   Press Ctrl+C to stop"
echo ""
./search_wrapped_server
