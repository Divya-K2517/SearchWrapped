# Search Wrapped

*A self-contained C++ engine that turns your Chrome history into a data-driven personality profile.*

---

## Demo

screenshot/short GIF

---

## What It Does

what the tool does (parses a Chrome/Google Takeout History.json, classifies browsing into categories, generates a narrative "personality" report)
no LLM calls, no external APIs at runtime, everything is hand-built statistics and heuristics
TF-IDF search fingerprinting with a blended IDF prior, and the three-tier domain classification cascade

---

## Tech Stack

C++17, cpp-httplib, nlohmann/json (backend, both header-only/no runtime deps), vanilla HTML/CSS/JS (frontend, no framework)

---

## Architecture

a diagram (ASCII or Mermaid) showing the pipeline end to end — History.json → parser → analysis engine (TF-IDF / cosine archetype engine / 3-tier domain classifier) → JSON API → 3D frontend.

---

## The Classification Cascade

Tier 1 — public domain table, built offline from UT1 Blacklists cross-referenced against a real popularity ranking. 
Tier 2 — a keyword scorer generalized across three input types (search queries, hostname tokens, and aggregated page titles) using one shared, word-boundary-aware function. 
Tier 3 — honest fallback to "Other" when neither tier is confident.

---

## Data Provenance

how the public domain table was actually built — UT1 Blacklists (the maintained successor to the now-defunct Shallalist) cross-referenced against a DNSFilter/Webshrinker popularity ranking, with a documented data-cleaning step (excluding domains that cross-list into UT1's own gambling/malware/phishing categories, catching real contamination in the raw jobsearch list). 

---

## Search Fingerprinting & Personality Engine

 explains the TF-IDF engine (tokenization, stop-word filtering, TF blended with a corpus IDF and a general-English frequency prior) and the cosine-similarity archetype matcher (13-dimensional behavioral embedding compared against 8 hand-tuned archetype vectors, with match confidence % and a "shadow" second-place archetype). 

---

## Getting Started

clone → build (`make` / `start.sh`) → run → open browser instructions. 

---

## API Reference

a short table of endpoints — `POST /api/analyze`, `GET /api/demo`, `GET /api/health` —  with a one-line description and an example request/response shape

---

## Project Structure

short annotated file tree (`main.cpp`, `include/`, `scripts/`, `frontend/`, etc.) 

---

## Roadmap

2-4 forward-looking bullets on planned work. 
