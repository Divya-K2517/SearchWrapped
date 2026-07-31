# Search Wrapped

*A self-contained C++ engine that turns your Chrome history into a data-driven personality profile.*

---

## Demo

screenshot/short GIF

---

## What It Does

Browser DNA is a self-contained web server that parses a Chrome (or Google Takeout) `History.json` export and analyzes it to show what you browse, when you browse it, and how your habits break down by category. It includes browsing patterns by time of day and category, a search-query fingerprint, and a personality archetype match — all computed server-side by a from-scratch C++ analysis engine, with no LLM calls and no external API dependency at runtime.
 
Two things distinguish it from a typical "wrapped"-style analytics script:
 
- **A three-tier domain classification cascade.** Every domain in a user's history is categorized by first checking a 505-domain public reference table, then falling back to a keyword scorer that runs the same scoring function across three different input types — search queries, hostname tokens, and aggregated page titles — before labeling anything left over as "Other".
- **A TF-IDF search fingerprint and cosine-similarity personality engine.** Search queries are tokenized, stop-word filtered, and scored with a TF-IDF metric that blends each user's own query against an embedded general-English frequency prior, surfacing the terms statistically unique to that person. Behavioral signals are then reduced to a 13-dimensional vector and matched via cosine similarity against 8 hand-tuned archetypes across 19 content categories, with a reported confidence percentage and runner-up "shadow" archetype.

---

## Tech Stack

C++17, cpp-httplib, nlohmann/json (backend, both header-only/no runtime deps), vanilla HTML/CSS/JS (frontend, no framework)

---

## Architecture

Data follows a straightforward pipeline: a history export goes in, is cleaned/standardized, is analyzed through 4 main methods, and comes back out as one JSON response that the frontend renders into the 3D gallery.
 
```mermaid
graph TD
    A["Chrome / Google Takeout<br/>History.json export"] --> B["Frontend<br/>(file upload)"]
    B -->|"POST /api/analyze"| C["Parser<br/>normalizes Chrome vs. Takeout format"]
    C --> D["Analysis Engine<br/>analyze()"]
 
    D --> D1["Time-pattern analysis"]
    D --> D2["3-tier domain classifier"]
    D --> D3["Search category scoring & clustering"]
    D --> D4["TF-IDF fingerprint +<br/>cosine-similarity archetype matching"]
 
    D1 --> E["JSON response"]
    D2 --> E
    D3 --> E
    D4 --> E
 
    E --> F["Frontend<br/>3D museum-gallery UI"]
```
 

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
