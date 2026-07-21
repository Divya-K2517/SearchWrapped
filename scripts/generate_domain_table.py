#!/usr/bin/env python3
"""
generate_domain_table.py
─────────────────────────────────────────────────────────────
Offline, one-off build script (NOT compiled/run as part of the
app at runtime).

Cross-references TWO public datasets, build-time only:

  1. UT1 Blacklists (Université Toulouse Capitole, CC BY-SA 4.0) —
     the actively-maintained public successor to the now-defunct
     Shallalist — for category labels.
     Mirror:    https://github.com/olbat/ut1-blacklists
     Original:  https://dsi.ut-capitole.fr/blacklists/

  2. DNSFilter/Webshrinker Top Domains ranking — a real popularity
     signal (one of the rankings Tranco itself blends in) — used to
     filter each UT1 category down to domains people actually
     recognize, instead of the long tail of low-traffic/spammy
     sites a raw blocklist is dominated by.
     Source: https://github.com/DNSFilter/topdomains
     Snapshot used: lists/2023-06/TopDomains_2023-06_250k.csv

  This mirrors the original plan: cross-reference a popularity list
  x a categorized list, at build time, into one static header with
  no runtime dependency on either source.

Usage:
    python3 generate_domain_table.py
Output:
    ../include/generated_domain_table.h

Re-run this whenever you want to refresh the table from the latest
UT1 snapshot — it is intentionally NOT part of the C++ build.
"""

import re
import csv
import urllib.request

# ─────────────────────────────────────────────────────────────
# UT1 category -> (your Browser DNA category, palette color)
# Every UT1 source maps onto one of your existing 19 SEARCH_CATS
# categories — no orphan categories introduced.
# Budget = max domains to pull from that UT1 list (capped by however
# many of that category's domains actually appear in the popularity
# ranking — quality over hitting the exact number).
# ─────────────────────────────────────────────────────────────
CATEGORY_MAP = {
    "shopping":        {"category": "Shopping",               "budget": 85},
    "jobsearch":       {"category": "Job Hunting",             "budget": 80},
    "press":           {"category": "News & Research",         "budget": 60},
    "bank":            {"category": "Personal Finance",        "budget": 40},
    "financial":       {"category": "Personal Finance",        "budget": 20},
    "cooking":         {"category": "Food & Nutrition",        "budget": 11},
    "dating":          {"category": "Family & Relationships",  "budget": 45},
    "audio-video":     {"category": "Entertainment",           "budget": 45},
    "games":           {"category": "Entertainment",           "budget": 50},
    "social_networks": {"category": "Entertainment",           "budget": 30},
    "astrology":       {"category": "Religion & Spirituality", "budget": 2},
    "sports":          {"category": "Health & Fitness",        "budget": 45},
}

POPULARITY_URL = ("https://raw.githubusercontent.com/DNSFilter/topdomains/"
                   "main/lists/2023-06/TopDomains_2023-06_250k.csv")

# Generic CDN/cloud-infra domains show up inside content categories
# (e.g. a dating app's asset bucket on cloudfront) but aren't themselves
# recognizable end-user sites, so they're excluded outright.
INFRA_SUFFIXES = (
    "cloudfront.net", "akamaized.net", "akamaihd.net", "edgekey.net",
    "amazonaws.com", "googleusercontent.com", "azureedge.net",
    "fastly.net", "herokuapp.com", "cloudflare.net", "cloudflare.com",
    "windows.net", "azurewebsites.net",
)


def is_infra_domain(domain: str) -> bool:
    return any(domain == s or domain.endswith("." + s) for s in INFRA_SUFFIXES)


# Some UT1 "content" categories (jobsearch especially) cross-contaminate
# with gambling/malware/phishing/piracy domains. Any domain that shows up
# in one of these noise lists gets excluded from every category, even if
# it also appears in a legitimate one — a bad classification anywhere
# disqualifies the domain.
NOISE_CATEGORIES = [
    "gambling", "malware", "phishing", "redirector",
    "warez", "dynamic-dns", "cryptojacking", "doh", "vpn", "hacking",
]

# Fixed color palette keyed by your existing category names
# (colors assigned programmatically per category, not per domain)
CATEGORY_COLORS = {
    "Shopping":               "#FF9900",
    "Job Hunting":             "#0A66C2",
    "News & Research":         "#4A5A8A",
    "Personal Finance":        "#0CAA41",
    "Food & Nutrition":        "#FFD166",
    "Family & Relationships":  "#FF6B9D",
    "Entertainment":           "#E50914",
    "Religion & Spirituality": "#8B5CF6",
    "Health & Fitness":        "#00D4FF",
}

UT1_BASE = "https://raw.githubusercontent.com/olbat/ut1-blacklists/master/blacklists"

# UT1 categorizes by filter-list intent, not always by what a site is
# actually for. LinkedIn lands in "social_networks" (true, technically)
# but functionally it's a career platform — worth a manual correction
# for a handful of universally-recognized brands rather than silently
# shipping a wrong category. This is NOT personal domain data — every
# entry here is a globally recognized brand, kept intentionally short.
MANUAL_OVERRIDES = {
    "linkedin.com": "Job Hunting",
}


def fetch_popularity_ranking() -> dict:
    """rank (int, 1 = most popular) keyed by domain"""
    print("Fetching popularity ranking (DNSFilter/Webshrinker top 250k)...")
    with urllib.request.urlopen(POPULARITY_URL) as resp:
        text = resp.read().decode("utf-8", errors="ignore")
    rank_by_domain = {}
    for row in csv.reader(text.splitlines()):
        if len(row) != 2:
            continue
        r, domain = row
        rank_by_domain[domain.strip().lower()] = int(r)
    print(f"  -> {len(rank_by_domain)} ranked domains")
    return rank_by_domain


def fetch_category(ut1_name: str) -> set:
    url = f"{UT1_BASE}/{ut1_name}/domains"
    with urllib.request.urlopen(url) as resp:
        text = resp.read().decode("utf-8", errors="ignore")
    return {line.strip().lower() for line in text.splitlines() if line.strip()}


def label_from_domain(domain: str) -> str:
    root = domain.split(".")[0]
    return root.replace("-", " ").title()


def fetch_noise_set() -> set:
    noise = set()
    for cat in NOISE_CATEGORIES:
        print(f"Fetching UT1 noise list '{cat}'...")
        noise |= fetch_category(cat)
    print(f"  -> {len(noise)} domains flagged as noise (gambling/malware/phishing/etc.)")
    return noise


def main():
    popularity = fetch_popularity_ranking()
    noise = fetch_noise_set()
    seen_domains: set[str] = set()
    entries: list[tuple[str, str, str, str]] = []  # domain, label, category, color

    for ut1_name, cfg in CATEGORY_MAP.items():
        category = cfg["category"]
        color = CATEGORY_COLORS[category]
        budget = cfg["budget"]

        print(f"Fetching UT1 '{ut1_name}' -> '{category}' (budget {budget})...")
        ut1_domains = {d for d in fetch_category(ut1_name) - noise if not is_infra_domain(d)}

        # keep only domains that also appear in the popularity ranking —
        # this is the cross-reference step: category label from UT1,
        # "is this actually a recognizable site" from the ranking.
        ranked_hits = [d for d in ut1_domains if d in popularity]
        ranked_hits.sort(key=lambda d: popularity[d])

        picked = 0
        for d in ranked_hits:
            if picked >= budget:
                break
            if d in seen_domains:
                continue
            seen_domains.add(d)
            final_category = MANUAL_OVERRIDES.get(d, category)
            final_color = CATEGORY_COLORS[final_category]
            entries.append((d, label_from_domain(d), final_category, final_color))
            picked += 1

        print(f"  -> {len(ranked_hits)} recognizable candidates, picked {picked}")

    entries.sort(key=lambda e: e[0])
    write_header(entries)
    print(f"\nTotal domains in table: {len(entries)}")
    print("Wrote include/generated_domain_table.h")


def write_header(entries):
    lines = []
    lines.append("// ══════════════════════════════════════════════════════════")
    lines.append("//  GENERATED PUBLIC DOMAIN TABLE — DO NOT HAND-EDIT")
    lines.append("//")
    lines.append("//  Source:  UT1 Blacklists (Université Toulouse Capitole)")
    lines.append("//           https://github.com/olbat/ut1-blacklists")
    lines.append("//           CC BY-SA 4.0 — actively maintained public successor")
    lines.append("//           to the now-defunct Shallalist project.")
    lines.append("//")
    lines.append("//  Generated by: scripts/generate_domain_table.py")
    lines.append(f"//  Domain count: {len(entries)}")
    lines.append("// ══════════════════════════════════════════════════════════")
    lines.append("#pragma once")
    lines.append('#include <string>')
    lines.append('#include <unordered_map>')
    lines.append("")
    lines.append("struct DomainInfo { std::string label; std::string category; std::string color; };")
    lines.append("")
    lines.append("static const std::unordered_map<std::string, DomainInfo> PUBLIC_DOMAIN_DB = {")
    for domain, label, category, color in entries:
        lines.append(f'    {{"{domain}", {{"{label}", "{category}", "{color}"}}}},')
    lines.append("};")
    lines.append("")

    with open("../include/generated_domain_table.h", "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()