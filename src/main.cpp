// Raise payload limit to 100 MB so large History.json files are accepted
#ifndef CPPHTTPLIB_PAYLOAD_MAX_LENGTH
#define CPPHTTPLIB_PAYLOAD_MAX_LENGTH (100 * 1024 * 1024)
#endif
// Do NOT define CPPHTTPLIB_OPENSSL_SUPPORT — leaving it undefined disables TLS
#include "../include/httplib.h"
#include "../include/json.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <regex>
#include <numeric>
#include <cmath>
#include <ctime>
#include <functional>

using json = nlohmann::json;

// ══════════════════════════════════════════════════════════
//  DATA STRUCTURES
// ══════════════════════════════════════════════════════════

struct HistoryEntry {
    //stores one line of the history.json file

    std::string url;
    std::string title;
    std::string domain;
    int64_t time_usec = 0;
    int year  = 0;
    int month = 0; // 0-based
    int hour  = 0;
    int dow   = 0; // 0=Mon (ISO)
    bool is_search = false;
    std::string search_query;
};

struct TopSite {
    //defining a domain
    std::string domain;
    int visits;
    std::string category;
    std::string label;   
    std::string color;
};

struct SearchCluster {
    //one type of theme of searches
    std::string theme;
    std::string icon;
    std::vector<std::string> queries; //some top example queries
    int total;
};

struct PersonalityResult {
    //the result for what type of personality the user is
    std::string archetype;
    std::string icon;
    std::string tagline;
    std::string narrative;
    std::vector<std::string> evidence;
    std::string color;
    //cosine similarity fields
    double match_pct = 0.0;                  // 0-100 confidence
    std::string shadow_archetype;            //second-closest
    std::string shadow_icon;
    double shadow_pct = 0.0;
    std::vector<double> radar;               //8 normalized dimension scores for chart
    std::vector<std::string> radar_labels;
};

struct WrappedResult {
    //the output, everything frontend cares abt
    int total_visits = 0;
    int total_searches = 0;
    int unique_queries = 0;
    int unique_domains = 0;
    int year_start = 0;
    int year_end = 0;

    // Time patterns
    std::vector<int> month_counts;     // 12
    std::vector<int> hour_counts;      // 24
    std::vector<int> dow_counts;       // 7
    std::string peak_hour_label;
    std::string peak_day_label;
    std::string peak_month_label;
    double night_owl_pct = 0.0;
    double weekend_pct = 0.0;

    // Sites
    std::vector<TopSite> top_sites;    // top 12

    // Searches
    std::vector<std::pair<std::string,int>> top_searches;
    std::vector<SearchCluster> clusters;
    std::string longest_query;
    int longest_query_words = 0;
    int question_count = 0;
    double avg_query_words = 0.0;

    //categories
    std::vector<std::pair<std::string,int>> category_breakdown;

    //personality
    PersonalityResult personality;

    //fun stats
    double searches_per_day = 0.0;
    double visits_per_day = 0.0;
    int binge_sessions;        // sessions > 30 consecutive minutes
    std::string alter_ego;     // based on peak hour label

    //TF-IDF fingerprint — top terms for this user
    std::vector<std::pair<std::string,double>> tfidf_terms; // (word, score)
};

// ══════════════════════════════════════════════════════════
//  DOMAIN KNOWLEDGE BASE
// ══════════════════════════════════════════════════════════

struct DomainInfo { std::string label; std::string category; std::string color; };

static const std::unordered_map<std::string, DomainInfo> DOMAIN_DB = {
    //domain database, hardcoded for now but should be auto categorized in the future
    //career
    {"linkedin.com",          {"LinkedIn",       "Career",        "#0A66C2"}},
    {"careers.purdue.edu",    {"Purdue Careers",  "Career",       "#CEB888"}},
    {"glassdoor.com",         {"Glassdoor",      "Career",        "#0CAA41"}},
    {"indeed.com",            {"Indeed",         "Career",        "#2164F3"}},
    {"handshake.com",         {"Handshake",      "Career",        "#E95234"}},
    //school
    {"purdue.brightspace.com",{"Brightspace",    "School",        "#CEB888"}},
    {"sso.purdue.edu",        {"Purdue SSO",     "School",        "#9D7535"}},
    {"purdue.edu",            {"Purdue",         "School",        "#CEB888"}},
    {"piazza.com",            {"Piazza",         "School",        "#4285F4"}},
    {"gradescope.com",        {"Gradescope",     "School",        "#009BDE"}},
    //coding
    {"leetcode.com",          {"LeetCode",       "Coding",        "#FFA116"}},
    {"github.com",            {"GitHub",         "Coding",        "#24292E"}},
    {"stackoverflow.com",     {"Stack Overflow", "Coding",        "#F48024"}},
    {"replit.com",            {"Replit",         "Coding",        "#F26207"}},
    {"codesandbox.io",        {"CodeSandbox",    "Coding",        "#151515"}},
    {"docs.google.com",       {"Google Docs",    "Productivity",  "#4285F4"}},
    //shopping
    {"amazon.com",            {"Amazon",         "Shopping",      "#FF9900"}},
    {"aeropostale.com",       {"Aeropostale",    "Shopping",      "#EC1C24"}},
    {"us.shein.com",          {"SHEIN",          "Shopping",      "#E83E70"}},
    {"etsy.com",              {"Etsy",           "Shopping",      "#F1641E"}},
    {"pinterest.com",         {"Pinterest",      "Social",        "#E60023"}},
    //social / Entertainment
    {"youtube.com",           {"YouTube",        "Entertainment", "#FF0000"}},
    {"instagram.com",         {"Instagram",      "Social",        "#E1306C"}},
    {"reddit.com",            {"Reddit",         "Social",        "#FF4500"}},
    {"tiktok.com",            {"TikTok",         "Social",        "#010101"}},
    {"spotify.com",           {"Spotify",        "Entertainment", "#1DB954"}},
    {"netflix.com",           {"Netflix",        "Entertainment", "#E50914"}},
    {"nytimes.com",           {"NY Times",       "News",          "#000000"}},
    {"theweeknd.com",         {"The Weeknd",     "Entertainment", "#8B0000"}},
    {"ticketmaster.com",      {"Ticketmaster",   "Entertainment", "#026CDF"}},
    //productivity
    {"mail.google.com",       {"Gmail",          "Productivity",  "#EA4335"}},
    {"calendar.google.com",   {"Calendar",       "Productivity",  "#4285F4"}},
    {"google.com",            {"Google",         "Search",        "#4285F4"}},
    //other
    {"duosecurity.com",       {"Duo Auth",       "Security",      "#6BBE4E"}},
    {"onboarding-us10.hr.cloud.sap",{"SAP HR",  "Work",          "#008FD3"}},
    {"xo.store",              {"XO Store",       "Shopping",      "#8B0000"}},
};

// ══════════════════════════════════════════════════════════
//  SEARCH CATEGORY KEYWORDS
// ══════════════════════════════════════════════════════════

static const std::vector<std::pair<std::string, std::vector<std::string>>> SEARCH_CATS = {
    //search categories, hardcoded for now
    //matches keywords to a category
    {"Job Hunting",      {"job","internship","career","resume","linkedin","salary","hiring","interview","glassdoor","offer","apply","position","recruiter","cover letter"}},
    {"Coding & Tech",    {"leetcode","algorithm","python","javascript","sql","api","code","programming","react","data structure","typescript","backend","frontend","system design","topological","binary","sort","tree","graph","recursion"}},
    {"School",           {"purdue","course","exam","grade","professor","gpa","assignment","credit","major","class","lecture","midterm","final","brightspace","piazza"}},
    {"Food & Nutrition", {"calories","recipe","nutrition","restaurant","food","eat","cook","bake","menu","protein","carbs","diet","meal","chicken","paneer","subway","dutch bros","chick"}},
    {"Health & Fitness", {"workout","gym","run","trail","hike","exercise","fitness","rec center","steps","weight","yoga","stretch","palmer park","sport","swim"}},
    {"Shopping",         {"amazon","buy","cheap","sale","price","review","shein","aeropostale","store","discount","order","shipping","return","deals"}},
    {"Entertainment",    {"spotify","youtube","netflix","the weeknd","music","movie","show","concert","ticketmaster","album","song","artist","anime","game","stream"}},
    {"News & Research",  {"nytimes","news","study","research","history","explained","why","how does","what is","wiki","science","report","analysis","2024","2025"}},
    {"Navigation",       {"near me","directions","hours","address","open","closed","location","map","parking","transit","schedule","flight","uber","lyft"}},
};

// ══════════════════════════════════════════════════════════
//  UTILITIES
// ══════════════════════════════════════════════════════════

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string trim(const std::string& s) {
    //cuts off leading/trailing whitespace
    auto start = s.find_first_not_of(" \t\n\r");
    auto end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

//extract domain from URL string (fast, no library)
std::string extract_domain(const std::string& url) {
    //strip scheme
    size_t start = url.find("://");
    if (start == std::string::npos) start = 0;
    else start += 3;
    //strip www.
    if (url.substr(start, 4) == "www.") start += 4;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}

//extract query param from URL
std::string extract_query_param(const std::string& url, const std::string& param) {
    std::string needle = param + "=";
    size_t pos = url.find('?');
    if (pos == std::string::npos) return "";
    pos = url.find(needle, pos);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = url.find_first_of("&#", pos);
    if (end == std::string::npos) end = url.size();
    std::string encoded = url.substr(pos, end - pos);
    //URL-decode common encodings
    std::string out;
    for (size_t i = 0; i < encoded.size(); i++) {
        if (encoded[i] == '+') { out += ' '; }
        else if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char hex[3] = {encoded[i+1], encoded[i+2], 0};
            out += (char)std::strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += encoded[i];
        }
    }
    return out;
}

int count_words(const std::string& s) {
    std::istringstream iss(s);
    int n = 0; std::string w;
    while (iss >> w) n++;
    return n;
}

// Microsecond timestamp → broken-down time
struct BrokenTime { int year, month, hour, dow; }; // month 0-based, dow 0=Mon

BrokenTime usec_to_broken(int64_t usec) {
    time_t t = (time_t)(usec / 1000000LL);
    struct tm* gm = gmtime(&t);
    BrokenTime bt;
    bt.year  = gm->tm_year + 1900;
    bt.month = gm->tm_mon;        // 0-based
    bt.hour  = gm->tm_hour;
    bt.dow   = (gm->tm_wday + 6) % 7; // convert Sun=0 → Mon=0
    return bt;
}

// ══════════════════════════════════════════════════════════
//  PARSE CHROME HISTORY JSON
// ══════════════════════════════════════════════════════════

// Parse RFC 3339 / ISO 8601 timestamp string → BrokenTime
// Handles: "2024-03-15T02:14:00.000Z"  and  "2024-03-15T02:14:00Z"
BrokenTime rfc3339_to_broken(const std::string& s) {
    BrokenTime bt{0,0,0,0};
    if (s.size() < 19) return bt;
    struct tm t{};
    t.tm_year = std::stoi(s.substr(0,4))  - 1900;
    t.tm_mon  = std::stoi(s.substr(5,2))  - 1;
    t.tm_mday = std::stoi(s.substr(8,2));
    t.tm_hour = std::stoi(s.substr(11,2));
    t.tm_min  = std::stoi(s.substr(14,2));
    t.tm_sec  = std::stoi(s.substr(17,2));
    t.tm_isdst = 0;
#ifdef _WIN32
    time_t epoch = _mkgmtime(&t);
#else
    time_t epoch = timegm(&t);
#endif
    if (epoch == (time_t)-1) return bt;
    struct tm* gm = gmtime(&epoch);
    bt.year  = gm->tm_year + 1900;
    bt.month = gm->tm_mon;
    bt.hour  = gm->tm_hour;
    bt.dow   = (gm->tm_wday + 6) % 7;
    // store back as usec so callers can use time_usec
    // (we return the epoch via a side-channel in bt — callers recompute)
    return bt;
}

// Returns epoch seconds from RFC3339 string, or 0 on failure
int64_t rfc3339_to_usec(const std::string& s) {
    if (s.size() < 19) return 0;
    struct tm t{};
    t.tm_year = std::stoi(s.substr(0,4))  - 1900;
    t.tm_mon  = std::stoi(s.substr(5,2))  - 1;
    t.tm_mday = std::stoi(s.substr(8,2));
    t.tm_hour = std::stoi(s.substr(11,2));
    t.tm_min  = std::stoi(s.substr(14,2));
    t.tm_sec  = std::stoi(s.substr(17,2));
    t.tm_isdst = 0;
#ifdef _WIN32
    time_t epoch = _mkgmtime(&t);
#else
    time_t epoch = timegm(&t);
#endif
    if (epoch == (time_t)-1) return 0;
    return (int64_t)epoch * 1000000LL;
}

std::vector<HistoryEntry> parse_history(const json& root) {
    std::vector<HistoryEntry> entries;

    // ── Format 1: Chrome BrowserHistory export — raw array or wrapped
    //    {"url":..., "title":..., "time_usec":...}
    // ── Format 2: Google Takeout MyActivity.json (Search)
    //    array of {"header":"Search","title":"Searched for X",
    //              "titleUrl":"https://google.com/search?q=X",
    //              "time":"2024-03-15T02:14:00.000Z", ...}
    // ── Format 3: Google Takeout wrapped in {"Browser History":[...]}

    const json* items = nullptr;
    bool is_takeout = false;

    if (root.is_array()) {
        items = &root;
        // Detect Takeout format by checking first element
        if (!root.empty()) {
            const auto& first = root[0];
            if (first.contains("time") && first.contains("title") &&
                first["time"].is_string()) {
                is_takeout = true;
            }
        }
    } else if (root.contains("Browser History") && root["Browser History"].is_array()) {
        items = &root["Browser History"];
    } else {
        return entries;
    }

    for (const auto& item : *items) {
        HistoryEntry e;

        if (is_takeout) {
            // ── Google Takeout MyActivity format ──────────────────────
            // Title looks like "Searched for how to stop overthinking"
            // or just the page title for non-search activity
            if (item.contains("title")) {
                std::string raw_title = item["title"].get<std::string>();
                e.title = raw_title;

                // Extract the search query from "Searched for X"
                const std::string prefix = "Searched for ";
                if (raw_title.rfind(prefix, 0) == 0) {
                    e.is_search = true;
                    e.search_query = trim(raw_title.substr(prefix.size()));
                }
            }

            // URL comes from titleUrl
            if (item.contains("titleUrl") && item["titleUrl"].is_string()) {
                e.url = item["titleUrl"].get<std::string>();
                // Also try to extract query from URL as fallback
                if (!e.is_search && e.url.find("google.com/search") != std::string::npos) {
                    std::string q = extract_query_param(e.url, "q");
                    if (!q.empty()) {
                        e.is_search = true;
                        e.search_query = trim(q);
                    }
                }
            }

            // Parse RFC 3339 timestamp
            if (item.contains("time") && item["time"].is_string()) {
                std::string ts = item["time"].get<std::string>();
                e.time_usec = rfc3339_to_usec(ts);
                if (e.time_usec > 0) {
                    auto bt = usec_to_broken(e.time_usec);
                    e.year  = bt.year;
                    e.month = bt.month;
                    e.hour  = bt.hour;
                    e.dow   = bt.dow;
                }
            }

            // Use header as a domain proxy when URL is absent
            if (e.url.empty() && item.contains("header") && item["header"].is_string()) {
                e.domain = item["header"].get<std::string>();
            }

        } else {
            // ── Chrome BrowserHistory format ──────────────────────────
            if (item.contains("url"))   e.url   = item["url"].get<std::string>();
            if (item.contains("title")) e.title = item["title"].get<std::string>();
            if (item.contains("time_usec")) {
                e.time_usec = item["time_usec"].get<int64_t>();
                auto bt = usec_to_broken(e.time_usec);
                e.year  = bt.year;
                e.month = bt.month;
                e.hour  = bt.hour;
                e.dow   = bt.dow;
            }
            // Detect Google search from URL
            if (e.url.find("google.com/search") != std::string::npos) {
                std::string q = extract_query_param(e.url, "q");
                if (!q.empty()) {
                    e.is_search = true;
                    e.search_query = trim(q);
                }
            }
        }

        if (e.domain.empty()) e.domain = extract_domain(e.url);
        entries.push_back(std::move(e));
    }

    return entries;
}

// ══════════════════════════════════════════════════════════
//  TF-IDF SEARCH FINGERPRINT ENGINE
//
//  Each unique search query is treated as a "document".
//  TF  = fraction of times this term appears across all queries
//  IDF = log(N / df) where df = number of queries containing the term,
//        floored by a general-English prior so ultra-common words stay low
//        even if they appear in every query.
//
//  Terms are tokenized (alpha only, ≥3 chars), stop-word filtered, then
//  ranked by TF * IDF. The top results are words statistically unique to
//  THIS user — their search DNA.
// ══════════════════════════════════════════════════════════

static const std::set<std::string> STOPWORDS = {
    "the","and","for","that","this","with","are","was","have","from",
    "not","but","you","your","they","what","how","why","when","where",
    "who","which","can","does","did","will","would","could","should",
    "its","has","had","been","more","also","into","than","then","them",
    "about","some","there","their","here","just","like","get","use",
    "using","used","make","best","good","need","want","all","any","new",
    "one","two","three","first","last","long","time","year","day","way",
    "com","www","http","https","org","net","app","top","list","find",
    "google","search","result","site","page","link","free","online",
    "download","install","review","price","buy","near","open","hours"
};

static std::vector<std::string> tokenize_query(const std::string& s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : s) {
        if (std::isalpha((unsigned char)c)) {
            cur += std::tolower((unsigned char)c);
        } else {
            if (cur.size() >= 3 && !STOPWORDS.count(cur)) tokens.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3 && !STOPWORDS.count(cur)) tokens.push_back(cur);
    return tokens;
}

// General English word frequency prior: log(corpus_size / estimated_df_in_web_corpus)
// Calibrated so domain-specific terms score much higher than generic English.
// Words not in this table get idf_prior = log(1000) ≈ 6.9 (rare/technical).
static const std::unordered_map<std::string,double> IDF_PRIOR = {
    {"algorithm",5.2},{"leetcode",7.5},{"python",4.8},{"javascript",4.9},
    {"typescript",5.4},{"react",5.1},{"backend",5.3},{"frontend",5.2},
    {"internship",5.8},{"resume",5.5},{"linkedin",5.6},{"glassdoor",6.2},
    {"salary",5.4},{"recruiter",6.0},{"interview",5.2},{"leetcode",7.5},
    {"github",5.3},{"stackoverflow",6.1},{"recursion",5.9},{"binary",5.0},
    {"sorting",5.5},{"dynamic",4.9},{"programming",4.7},{"structure",4.6},
    {"calories",5.1},{"recipe",4.8},{"nutrition",5.2},{"protein",5.0},
    {"restaurant",4.7},{"paneer",7.2},{"subway",5.8},{"workout",5.3},
    {"fitness",5.1},{"exercise",5.0},{"running",4.9},{"trail",5.6},
    {"hiking",5.7},{"spotify",5.8},{"netflix",5.5},{"youtube",4.5},
    {"concert",5.6},{"ticket",5.3},{"anime",5.9},{"playlist",5.7},
    {"climate",5.3},{"election",5.4},{"policy",5.2},{"research",4.8},
    {"science",4.7},{"history",4.6},{"explained",5.0},{"study",4.8},
    {"purdue",7.8},{"brightspace",7.9},{"gradescope",7.9},{"piazza",7.1},
    {"professor",5.4},{"midterm",6.2},{"assignment",5.3},{"gpa",6.5},
    {"amazon",4.9},{"shipping",5.1},{"discount",5.3},{"shein",6.8},
    {"aeropostale",7.2},{"etsy",6.3},{"review",4.6},{"comparison",5.0},
};

std::vector<std::pair<std::string,double>> compute_tfidf(
    const std::unordered_map<std::string,int>& query_freq,
    int total_queries
) {
    if (total_queries == 0) return {};

    // Step 1: term frequencies across all queries (treat corpus as one doc per unique query)
    std::unordered_map<std::string, int> term_total_tf;  // sum of occurrences across corpus
    std::unordered_map<std::string, int> term_df;        // # queries containing this term
    int total_terms = 0;

    for (const auto& [q, freq] : query_freq) {
        auto tokens = tokenize_query(q);
        std::set<std::string> seen_in_query;
        for (const auto& tok : tokens) {
            term_total_tf[tok] += freq; // weight by how often the query was searched
            total_terms += freq;
            if (!seen_in_query.count(tok)) {
                term_df[tok]++;
                seen_in_query.insert(tok);
            }
        }
    }

    if (total_terms == 0) return {};

    // Step 2: score each term
    double N = (double)query_freq.size();
    std::vector<std::pair<std::string,double>> scored;

    for (const auto& [term, tf_raw] : term_total_tf) {
        double tf = (double)tf_raw / total_terms;

        // IDF: use user's corpus first, then blend with general-English prior
        int df = term_df.count(term) ? term_df[term] : 1;
        double idf_corpus = std::log((N + 1.0) / (df + 0.5));

        // Blend with prior: if term is common in English, penalize it
        double idf_prior = 6.9; // default: rare/unknown
        auto it = IDF_PRIOR.find(term);
        if (it != IDF_PRIOR.end()) idf_prior = it->second;

        // Geometric blend — corpus IDF floors out common terms, prior lifts domain terms
        double idf = 0.6 * idf_corpus + 0.4 * idf_prior;

        scored.push_back({term, tf * idf * 1000.0}); // scale for readability
    }

    // Step 3: sort, keep top 14
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){
        return a.second > b.second;
    });
    if (scored.size() > 14) scored.resize(14);
    return scored;
}


// ══════════════════════════════════════════════════════════
//  COSINE SIMILARITY ARCHETYPE ENGINE
//
//  User behavior is embedded as a normalized float vector:
//    dims[0..7] = category scores (Job, Code, School, Food, Fitness, Shop, Entmt, News)
//    dims[8]    = night_owl fraction (0-1)
//    dims[9]    = weekend fraction (0-1)
//    dims[10]   = question ratio (questions / total_searches)
//    dims[11]   = avg_query_words normalized (÷8)
//    dims[12]   = peak_hour_slot (0=morning, 0.5=afternoon, 1=night)
//
//  Each archetype is a hand-tuned ideal vector. Similarity = dot(u,a) / (|u||a|).
//  Returns best match + confidence %, plus shadow archetype (second closest).
// ══════════════════════════════════════════════════════════

struct ArchetypeSpec {
    std::string name;
    std::string icon;
    std::string tagline;
    std::string narrative;
    std::vector<std::string> evidence;
    std::string color;
    std::vector<double> vec; // 13-dim ideal vector
    // dims: job, code, school, food, fitness, shop, entmt, news,
    //       night_owl, weekend, question_ratio, query_words_norm, peak_slot
};

static const std::vector<ArchetypeSpec> ARCHETYPES = {
    {
        "The Midnight Coder", "⌨️",
        "Ships code while the world sleeps.",
        "Your search fingerprint doesn't lie: LeetCode, GitHub, and algorithm deep-dives clustered in the small hours. You've essentially built a second CS education inside your browser history. The dark mode isn't a preference — it's a lifestyle.",
        {"LeetCode at 2am is normal", "Algorithm rabbit holes", "README completionist", "Commits before sunrise", "Stack Overflow power user"},
        "#FFA116",
        {0.3, 0.9, 0.4, 0.1, 0.1, 0.05, 0.1, 0.15,  0.85, 0.2, 0.35, 0.7, 0.9}
    },
    {
        "The Ambitious Applicant", "🎯",
        "LinkedIn is basically your second home.",
        "Career, career, career. The ratio of job-hunt searches to everything else is a tell: you're not casually browsing — you're executing a strategy. Glassdoor salary checks, recruiter names, tailored cover letters. You're building a launchpad, not just finding a job.",
        {"Knows the recruiter's name", "Salary research at midnight", "Open to opportunities (always)", "Tailors every cover letter", "Job alert inbox: chaos"},
        "#0A66C2",
        {0.95, 0.5, 0.4, 0.1, 0.1, 0.1, 0.1, 0.2,  0.5, 0.3, 0.3, 0.5, 0.6}
    },
    {
        "The Scholar", "📚",
        "Studying isn't a phase — it's a personality.",
        "Your browser history reads like a course syllabus. Brightspace, Gradescope, Piazza — the academic stack lives in your top sites. You search with the intent of someone who actually wants to understand, not just pass. The question ratio gives you away.",
        {"Brightspace daily driver", "Reads the actual paper", "Piazza before asking prof", "GPA tracker installed", "Highlights digital PDFs"},
        "#CEB888",
        {0.2, 0.5, 0.95, 0.2, 0.2, 0.05, 0.15, 0.5,  0.4, 0.25, 0.7, 0.8, 0.5}
    },
    {
        "The Balanced Optimizer", "⚖️",
        "Counts macros. Still orders the samosa.",
        "A beautiful contradiction lives in your data: calorie tracking side-by-side with restaurant menus, workout schedules next to recipe searches. You approach both fitness and food with the same analytical rigor — but you know when to put the spreadsheet down and just eat.",
        {"Meal preps on Sunday", "Knows the macro of everything", "Still gets the dessert", "Gym schedule > social calendar", "Dutch Bros is non-negotiable"},
        "#00E5A0",
        {0.1, 0.2, 0.3, 0.75, 0.85, 0.1, 0.2, 0.2,  0.3, 0.55, 0.4, 0.55, 0.4}
    },
    {
        "The Culinary Curator", "🍜",
        "Every search is a flavor quest.",
        "Food is not sustenance — it's research. Recipes, nutrition panels, restaurant menus, delivery apps, ingredient substitutions. Your search history is essentially a food journal curated by someone who takes eating seriously. Others browse for fun. You browse hungry.",
        {"Reads menus before deciding", "Has a ranking for every spot", "Recipe bookmarker, rare cooker", "Calorie-aware, not calorie-stopped", "Knows the secret menu"},
        "#FFD166",
        {0.05, 0.1, 0.2, 0.95, 0.35, 0.2, 0.3, 0.15,  0.35, 0.5, 0.3, 0.5, 0.45}
    },
    {
        "The Night Browser", "🌙",
        "The internet is quieter at 3am.",
        "Peak activity: well past midnight. Whether it's NYT rabbit holes, 30-tab YouTube spirals, or regrettable online shopping, you've found that your best and worst browsing decisions happen when the rest of the world is asleep. No judgement. The algorithm knows.",
        {"Adds to cart at 2am", "NYT at midnight hits different", "Tomorrow's problem: tomorrow", "Tabs: always too many", "Sleep schedule: fluid"},
        "#A78BFA",
        {0.1, 0.2, 0.15, 0.3, 0.15, 0.4, 0.6, 0.4,  0.95, 0.35, 0.3, 0.45, 0.95}
    },
    {
        "The Informed Citizen", "📰",
        "You actually read past the headline.",
        "News, research, explainers — your search history has a higher question-word density than almost any other archetype. You don't just consume; you verify. NYTimes, Wikipedia rabbit holes, academic sources. In an era of hot takes, you want the actual context.",
        {"Reads the full article", "Cross-references sources", "Wikipedia: starting point, not end", "Sends people links as gifts", "Knows the backstory"},
        "#00C8F0",
        {0.1, 0.2, 0.35, 0.15, 0.2, 0.05, 0.2, 0.95,  0.4, 0.3, 0.8, 0.75, 0.4}
    },
    {
        "The Renaissance Browser", "🌐",
        "You contain multitudes.",
        "No single obsession dominates your history — and that's rare data. Code, food, school, news, fitness, shopping: you move between domains with the fluidity of someone who refuses to be categorized. The cosine similarity engine had to think hard. You're genuinely hard to pin down.",
        {"Context-switches effortlessly", "Tabs span 6 different topics", "No algorithmic bubble", "Curious about everything", "Unpredictable, in the best way"},
        "#FF6B9D",
        {0.4, 0.4, 0.4, 0.4, 0.4, 0.35, 0.4, 0.4,  0.45, 0.45, 0.5, 0.55, 0.5}
    },
};

static double cosine_sim(const std::vector<double>& a, const std::vector<double>& b) {
    double dot = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    if (na < 1e-9 || nb < 1e-9) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

PersonalityResult compute_personality_cosine(
    const std::vector<std::pair<std::string,int>>& cats,
    double night_owl_frac,   // 0-1
    double weekend_frac,     // 0-1
    int question_count,
    int total_searches,
    int peak_hour            // 0-23
) {
    // Build category score map, normalize to 0-1
    std::unordered_map<std::string,double> cm;
    double cat_max = 1.0;
    for (const auto& [n, s] : cats) {
        cm[n] = (double)s;
        if ((double)s > cat_max) cat_max = (double)s;
    }

    static const std::vector<std::string> CAT_ORDER = {
        "Job Hunting","Coding & Tech","School","Food & Nutrition",
        "Health & Fitness","Shopping","Entertainment","News & Research"
    };
    static const std::vector<std::string> RADAR_LABELS = {
        "Career","Code","School","Food","Fitness","Shopping","Entertainment","Research"
    };

    std::vector<double> user_vec;
    std::vector<double> radar_raw;
    for (const auto& cat : CAT_ORDER) {
        double v = cm.count(cat) ? cm[cat] / cat_max : 0.0;
        user_vec.push_back(v);
        radar_raw.push_back(v);
    }

    double q_ratio = total_searches > 0 ? (double)question_count / total_searches : 0.0;
    // peak_hour slot: 0=morning(5-11), 0.5=afternoon(12-18), 1=night(19-4)
    double peak_slot;
    if (peak_hour >= 5  && peak_hour <= 11) peak_slot = 0.0;
    else if (peak_hour >= 12 && peak_hour <= 18) peak_slot = 0.5;
    else peak_slot = 1.0;

    user_vec.push_back(night_owl_frac);
    user_vec.push_back(weekend_frac);
    user_vec.push_back(q_ratio);
    user_vec.push_back(std::min(1.0, q_ratio * 2.0)); // query_words placeholder (normalized)
    user_vec.push_back(peak_slot);

    // Find best and second-best archetype by cosine similarity
    double best_sim = -1.0, second_sim = -1.0;
    int best_idx = 0, second_idx = 1;

    for (int i = 0; i < (int)ARCHETYPES.size(); i++) {
        double sim = cosine_sim(user_vec, ARCHETYPES[i].vec);
        if (sim > best_sim) {
            second_sim = best_sim; second_idx = best_idx;
            best_sim = sim; best_idx = i;
        } else if (sim > second_sim) {
            second_sim = sim; second_idx = i;
        }
    }

    const auto& best = ARCHETYPES[best_idx];
    const auto& shadow = ARCHETYPES[second_idx];

    // Convert cosine sim to a 55-99% "match %" that feels meaningful.
    // Real matches typically land in [0.6, 0.98]; remap [0.3, 1.0] → [55, 99].
    auto sim_to_pct = [](double s) -> double {
        double pct = 55.0 + (s - 0.3) / 0.7 * 44.0;
        return std::max(55.0, std::min(99.0, pct));
    };

    PersonalityResult pr;
    pr.archetype  = best.name;
    pr.icon       = best.icon;
    pr.tagline    = best.tagline;
    pr.narrative  = best.narrative;
    pr.evidence   = best.evidence;
    pr.color      = best.color;
    pr.match_pct  = sim_to_pct(best_sim);
    pr.shadow_archetype = shadow.name;
    pr.shadow_icon      = shadow.icon;
    pr.shadow_pct       = sim_to_pct(second_sim);
    pr.radar        = radar_raw;  // 8 dims
    pr.radar_labels = RADAR_LABELS;

    return pr;
}


// ══════════════════════════════════════════════════════════
//  PERSONALITY ENGINE
// ══════════════════════════════════════════════════════════

PersonalityResult compute_personality(
    const std::vector<std::pair<std::string,int>>& cats,
    double night_owl_pct,
    double weekend_pct,
    int question_count,
    int total_searches,
    const std::string& peak_hour_label,
    const std::vector<TopSite>& top_sites
) {
    std::unordered_map<std::string,int> cm;
    for (const auto& [n,s] : cats) cm[n] = s;

    int career  = cm["Job Hunting"];
    int code    = cm["Coding & Tech"];
    int school  = cm["School"];
    int food    = cm["Food & Nutrition"];
    int fitness = cm["Health & Fitness"];
    int shop    = cm["Shopping"];
    int entmt   = cm["Entertainment"];
    int news    = cm["News & Research"];

    bool grinder    = (career + code) > (food + entmt + shop);
    bool foodie     = food > (code + career);
    bool night_mode = night_owl_pct > 0.35;
    bool athlete    = fitness > code / 2;
    bool shopper    = shop > career;
    bool scholar    = school > career;

    if (grinder && night_mode && code > career) {
        return {"The Midnight Coder", "⌨️",
            "Ships code while the world sleeps.",
            "Your browser history is basically a second CS degree. LeetCode at 2am, GitHub at 3am, LinkedIn at dawn — you're grinding the algorithm grind with the intensity of someone who really, really wants that offer. The dark circles are a badge of honor.",
            {"LeetCode warrior", "Sleeps after AC", "README reader", "Commits to main (sometimes)", "Dark mode only"},
            "#FFA116"};
    } else if (grinder && career > code) {
        return {"The Ambitious Applicant", "🎯",
            "LinkedIn is basically your second home.",
            "Career, career, career. You have Purdue on your resume, LeetCode in your browser history, and a LinkedIn profile that's very much 'open to opportunities'. You're not just looking for a job — you're building a launchpad.",
            {"Profile views obsessed", "Tailors every cover letter", "Knows the recruiter's name", "Job alert at midnight", "Salary spreadsheet exists"},
            "#0A66C2"};
    } else if (foodie && fitness) {
        return {"The Balanced Optimizer", "⚖️",
            "Counts macros, then orders paneer.",
            "A fascinating contradiction lives in your browser history: obsessive calorie tracking right next to punjabi cocktail samosas. You know exactly how many calories are in a peach, and you also know which restaurant has the best kitkat dessert. Balance.",
            {"Checks nutrition before eating", "Hikes Palmer Park", "Still orders the samosa", "Subway sandwich strategist", "Dutch Bros loyalist"},
            "#00E5A0"};
    } else if (foodie) {
        return {"The Culinary Curator", "🍜",
            "Every search is a flavor quest.",
            "Your browser history is a food diary. Recipes, restaurant menus, nutrition info, Uber Eats, Dutch Bros secret menu — you approach eating with the curiosity of a chef and the dedication of a researcher. Life is too short for bad food.",
            {"Reads menus before ordering", "Has a go-to spot for everything", "Recipe bookmarker", "Calorie-aware but not calorie-stopped", "Knows the best item"},
            "#FFD166"};
    } else if (athlete) {
        return {"The Trail Runner", "🏃",
            "Always chasing the next summit.",
            "Palmer Park trails, rec center hours, workout schedules — your digital footprint follows your literal footprint. You balance physical hustle with academic grind in a way that makes most people tired just reading about it.",
            {"Palmer Park regular", "Checks rec center hours", "Weekend warrior", "Fitness-food balancer", "Outdoors over gym, usually"},
            "#00D4FF"};
    } else if (night_mode && !grinder) {
        return {"The Night Browser", "🌙",
            "The internet is quieter at 3am.",
            "Peak activity: well past midnight. Whether it's NYT rabbit holes, random YouTube, or online shopping when your guard is down, you've discovered that the best (and worst) browsing happens when you should be sleeping.",
            {"Adds to cart at 2am", "NYT at midnight", "Tomorrow's problem: tomorrow", "Best decisions after midnight", "Sleep schedule: optional"},
            "#A78BFA"};
    } else {
        return {"The Renaissance Browser", "🌐",
            "You contain multitudes.",
            "No single obsession defines your browser history. School, food, code, shopping, news, fitness — you pivot between life domains with the ease of someone who refuses to be boxed in. You're genuinely curious and impossible to predict.",
            {"Genuinely curious about everything", "Context-switcher pro", "Multiple browser personas", "Tabs tell a story", "Unpredictable, in the best way"},
            "#FF6B9D"};
    }
}

// ══════════════════════════════════════════════════════════
//  CORE ANALYSIS ENGINE
// ══════════════════════════════════════════════════════════

WrappedResult analyze(const std::vector<HistoryEntry>& entries) {
    WrappedResult r;
    r.month_counts = std::vector<int>(12, 0);
    r.hour_counts  = std::vector<int>(24, 0);
    r.dow_counts   = std::vector<int>(7, 0);

    if (entries.empty()) return r;

    // Date range
    int64_t min_t = INT64_MAX, max_t = INT64_MIN;
    for (const auto& e : entries) {
        if (e.time_usec > 0) {
            min_t = std::min(min_t, e.time_usec);
            max_t = std::max(max_t, e.time_usec);
        }
    }
    auto bt0 = usec_to_broken(min_t);
    auto bt1 = usec_to_broken(max_t);
    r.year_start = bt0.year;
    r.year_end   = bt1.year;

    double days_total = (max_t - min_t) / 1e6 / 86400.0;
    if (days_total < 1) days_total = 1;

    // Accumulators
    std::unordered_map<std::string, int> domain_freq;
    std::unordered_map<std::string, int> query_freq;
    int night_cnt = 0, weekend_cnt = 0;
    int question_cnt = 0;
    int total_qwords = 0;
    r.longest_query = "";
    int longest_wc = 0;

    for (const auto& e : entries) {
        // Time breakdown
        if (e.month >= 0 && e.month < 12) r.month_counts[e.month]++;
        if (e.hour  >= 0 && e.hour  < 24) r.hour_counts[e.hour]++;
        if (e.dow   >= 0 && e.dow   <  7) r.dow_counts[e.dow]++;

        // Night owl: 23:00 - 04:00
        if (e.hour >= 23 || e.hour <= 4) night_cnt++;
        // Weekend: Sat=5, Sun=6
        if (e.dow >= 5) weekend_cnt++;

        if (!e.domain.empty()) domain_freq[e.domain]++;

        if (e.is_search && !e.search_query.empty()) {
            std::string ql = to_lower(e.search_query);
            query_freq[ql]++;
            int wc = count_words(ql);
            total_qwords += wc;
            if (wc > longest_wc) { longest_wc = wc; r.longest_query = e.search_query; }
            // Question detection
            if (ql.rfind("how ",0)==0 || ql.rfind("why ",0)==0 || ql.rfind("what ",0)==0 ||
                ql.rfind("when ",0)==0 || ql.rfind("where ",0)==0 || ql.rfind("is ",0)==0 ||
                ql.rfind("does ",0)==0 || ql.rfind("can ",0)==0 || ql.rfind("which ",0)==0) {
                question_cnt++;
            }
        }
    }

    r.total_visits    = (int)entries.size();
    r.total_searches  = (int)std::accumulate(query_freq.begin(), query_freq.end(), 0,
                            [](int a, const auto& b){ return a + b.second; });
    r.unique_queries  = (int)query_freq.size();
    r.unique_domains  = (int)domain_freq.size();
    r.night_owl_pct   = 100.0 * night_cnt   / r.total_visits;
    r.weekend_pct     = 100.0 * weekend_cnt / r.total_visits;
    r.question_count  = question_cnt;
    r.avg_query_words = r.total_searches > 0 ? (double)total_qwords / r.total_searches : 3.0;
    r.searches_per_day = r.total_searches / days_total;
    r.visits_per_day   = r.total_visits   / days_total;
    r.longest_query_words = longest_wc;

    // Peak hour label
    int peak_hr = (int)(std::max_element(r.hour_counts.begin(), r.hour_counts.end()) - r.hour_counts.begin());
    if      (peak_hr == 0)  r.peak_hour_label = "midnight";
    else if (peak_hr < 12)  r.peak_hour_label = std::to_string(peak_hr) + "am";
    else if (peak_hr == 12) r.peak_hour_label = "noon";
    else                    r.peak_hour_label = std::to_string(peak_hr - 12) + "pm";

    // Peak month
    static const std::string MONTHS[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    int peak_mo = (int)(std::max_element(r.month_counts.begin(), r.month_counts.end()) - r.month_counts.begin());
    r.peak_month_label = MONTHS[peak_mo];

    // Peak day
    static const std::string DAYS[] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
    int peak_dow = (int)(std::max_element(r.dow_counts.begin(), r.dow_counts.end()) - r.dow_counts.begin());
    r.peak_day_label = DAYS[peak_dow];

    // Top searches (sorted by freq)
    std::vector<std::pair<std::string,int>> sorted_q(query_freq.begin(), query_freq.end());
    std::sort(sorted_q.begin(), sorted_q.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    r.top_searches = std::vector<std::pair<std::string,int>>(sorted_q.begin(),
        sorted_q.begin() + std::min((int)sorted_q.size(), 10));

    // Top sites
    std::vector<std::pair<std::string,int>> sorted_d(domain_freq.begin(), domain_freq.end());
    // Remove google.com from top sites (it's the search engine)
    sorted_d.erase(std::remove_if(sorted_d.begin(), sorted_d.end(),
        [](const auto& p){ return p.first == "google.com" || p.first.empty(); }), sorted_d.end());
    std::sort(sorted_d.begin(), sorted_d.end(), [](const auto& a, const auto& b){ return a.second > b.second; });

    for (int i = 0; i < std::min((int)sorted_d.size(), 12); i++) {
        const auto& [dom, cnt] = sorted_d[i];
        TopSite ts;
        ts.domain = dom;
        ts.visits = cnt;
        auto it = DOMAIN_DB.find(dom);
        if (it != DOMAIN_DB.end()) {
            ts.label    = it->second.label;
            ts.category = it->second.category;
            ts.color    = it->second.color;
        } else {
            // Capitalize domain name
            ts.label    = dom;
            ts.category = "Other";
            ts.color    = "#4A5A8A";
        }
        r.top_sites.push_back(ts);
    }

    // Category breakdown (based on all queries joined)
    std::string all_q;
    for (const auto& [q, _] : query_freq) all_q += " " + q;
    all_q = to_lower(all_q);

    // Also score domains
    std::string all_domains_str;
    for (const auto& [dom, cnt] : sorted_d) {
        for (int k = 0; k < std::min(cnt, 10); k++) all_domains_str += " " + dom;
    }

    std::vector<std::pair<std::string,int>> cat_scores;
    for (const auto& [cat, kws] : SEARCH_CATS) {
        int score = 0;
        for (const auto& kw : kws) {
            size_t pos = 0;
            const std::string& haystack = all_q;
            while ((pos = haystack.find(kw, pos)) != std::string::npos) {
                score++; pos += kw.size();
            }
        }
        // Bonus from domain visits
        for (const auto& ts : r.top_sites) {
            if (ts.category == cat) score += ts.visits / 10;
        }
        cat_scores.push_back({cat, score});
    }
    std::sort(cat_scores.begin(), cat_scores.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    r.category_breakdown = cat_scores;

    // Search clusters (group top queries by theme)
    // Simple: group top 30 queries by category
    std::unordered_map<std::string, SearchCluster> cluster_map;
    for (const auto& [cat, _] : SEARCH_CATS) {
        SearchCluster sc;
        sc.theme = cat;
        sc.total = 0;
        if (cat == "Food & Nutrition") sc.icon = "🍜";
        else if (cat == "Coding & Tech") sc.icon = "💻";
        else if (cat == "Job Hunting") sc.icon = "🎯";
        else if (cat == "School") sc.icon = "📚";
        else if (cat == "Health & Fitness") sc.icon = "🏃";
        else if (cat == "Shopping") sc.icon = "🛍️";
        else if (cat == "Entertainment") sc.icon = "🎵";
        else if (cat == "News & Research") sc.icon = "📰";
        else sc.icon = "🗺️";
        cluster_map[cat] = sc;
    }

    for (const auto& [q, cnt] : sorted_q) {
        std::string ql = to_lower(q);
        std::string best_cat = "News & Research";
        int best_score = 0;
        for (const auto& [cat, kws] : SEARCH_CATS) {
            int score = 0;
            for (const auto& kw : kws) {
                if (ql.find(kw) != std::string::npos) score++;
            }
            if (score > best_score) { best_score = score; best_cat = cat; }
        }
        auto& cl = cluster_map[best_cat];
        if (cl.queries.size() < 4) cl.queries.push_back(q);
        cl.total += cnt;
    }

    for (const auto& [_, sc] : cluster_map) {
        if (sc.total > 0) r.clusters.push_back(sc);
    }
    std::sort(r.clusters.begin(), r.clusters.end(), [](const auto& a, const auto& b){ return a.total > b.total; });

    // TF-IDF search fingerprint
    r.tfidf_terms = compute_tfidf(query_freq, r.unique_queries);

    // Cosine-similarity personality engine
    int peak_hr_int = (int)(std::max_element(r.hour_counts.begin(), r.hour_counts.end())
                            - r.hour_counts.begin());
    r.personality = compute_personality_cosine(
        cat_scores,
        r.night_owl_pct / 100.0,
        r.weekend_pct   / 100.0,
        question_cnt,
        r.total_searches,
        peak_hr_int
    );

    return r;
}

// ══════════════════════════════════════════════════════════
//  JSON SERIALIZATION
// ══════════════════════════════════════════════════════════

json result_to_json(const WrappedResult& r) {
    json j;
    j["total_visits"]      = r.total_visits;
    j["total_searches"]    = r.total_searches;
    j["unique_queries"]    = r.unique_queries;
    j["unique_domains"]    = r.unique_domains;
    j["year_start"]        = r.year_start;
    j["year_end"]          = r.year_end;
    j["peak_hour"]         = r.peak_hour_label;
    j["peak_day"]          = r.peak_day_label;
    j["peak_month"]        = r.peak_month_label;
    j["night_owl_pct"]     = std::round(r.night_owl_pct * 10) / 10.0;
    j["weekend_pct"]       = std::round(r.weekend_pct   * 10) / 10.0;
    j["question_count"]    = r.question_count;
    j["avg_query_words"]   = std::round(r.avg_query_words * 10) / 10.0;
    j["searches_per_day"]  = std::round(r.searches_per_day * 10) / 10.0;
    j["visits_per_day"]    = std::round(r.visits_per_day   * 10) / 10.0;
    j["longest_query"]     = r.longest_query;
    j["longest_query_words"] = r.longest_query_words;
    j["month_counts"]      = r.month_counts;
    j["hour_counts"]       = r.hour_counts;
    j["dow_counts"]        = r.dow_counts;

    json sites = json::array();
    for (const auto& s : r.top_sites) {
        json item;
        item["domain"] = s.domain; item["visits"] = s.visits;
        item["label"]  = s.label;  item["category"] = s.category;
        item["color"]  = s.color;
        sites.push_back(item);
    }
    j["top_sites"] = sites;

    json searches = json::array();
    for (const auto& [q, c] : r.top_searches) {
        json item; item["query"] = q; item["count"] = c;
        searches.push_back(item);
    }
    j["top_searches"] = searches;

    json clusters = json::array();
    for (const auto& cl : r.clusters) {
        json item;
        item["theme"] = cl.theme; item["icon"] = cl.icon;
        item["total"] = cl.total; item["queries"] = cl.queries;
        clusters.push_back(item);
    }
    j["clusters"] = clusters;

    json cats = json::array();
    for (const auto& [n, s] : r.category_breakdown) {
        json item; item["name"] = n; item["score"] = s;
        cats.push_back(item);
    }
    j["category_breakdown"] = cats;

    // TF-IDF fingerprint words
    json tfidf = json::array();
    for (const auto& [term, score] : r.tfidf_terms) {
        json item; item["term"] = term; item["score"] = std::round(score * 100) / 100.0;
        tfidf.push_back(item);
    }
    j["tfidf_terms"] = tfidf;

    j["personality"] = {
        {"archetype",        r.personality.archetype},
        {"icon",             r.personality.icon},
        {"tagline",          r.personality.tagline},
        {"narrative",        r.personality.narrative},
        {"evidence",         r.personality.evidence},
        {"color",            r.personality.color},
        {"match_pct",        std::round(r.personality.match_pct * 10) / 10.0},
        {"shadow_archetype", r.personality.shadow_archetype},
        {"shadow_icon",      r.personality.shadow_icon},
        {"shadow_pct",       std::round(r.personality.shadow_pct * 10) / 10.0},
        {"radar",            r.personality.radar},
        {"radar_labels",     r.personality.radar_labels}
    };

    return j;
}

// ══════════════════════════════════════════════════════════
//  DEMO DATA
// ══════════════════════════════════════════════════════════

WrappedResult make_demo() {
    std::vector<HistoryEntry> entries;
    // Simulate realistic Chrome history
    struct SimEntry { std::string url; std::string title; int64_t base_h; int spread_h; };
    std::vector<SimEntry> templates = {
        {"https://leetcode.com/problems/", "LeetCode", 2, 3},
        {"https://linkedin.com/jobs/", "LinkedIn Jobs", 20, 4},
        {"https://purdue.brightspace.com/", "Brightspace", 14, 6},
        {"https://www.google.com/search?q=topological+sort+algorithm", "topological sort - Google", 1, 2},
        {"https://www.google.com/search?q=how+to+ace+technical+interview", "interview tips - Google", 22, 3},
        {"https://github.com/", "GitHub", 3, 4},
        {"https://www.google.com/search?q=dutch+bros+menu+nutrition", "dutch bros calories - Google", 13, 2},
        {"https://www.aeropostale.com/", "Aeropostale", 21, 3},
        {"https://www.google.com/search?q=palmer+park+trails", "palmer park - Google", 10, 2},
        {"https://nytimes.com/", "NYTimes", 23, 2},
        {"https://www.google.com/search?q=subway+calories", "subway calories - Google", 12, 1},
        {"https://youtube.com/", "YouTube", 22, 4},
        {"https://piazza.com/", "Piazza", 15, 4},
        {"https://www.google.com/search?q=punjabi+cocktail+samosa", "samosa recipe - Google", 19, 3},
        {"https://us.shein.com/", "SHEIN", 21, 3},
        {"https://gradescope.com/", "Gradescope", 16, 4},
    };

    int64_t base_usec = 1714521600LL * 1000000LL; // Apr 1 2025
    srand(42);
    for (int day = 0; day < 340; day++) {
        int n_entries = 20 + rand() % 30;
        for (int k = 0; k < n_entries; k++) {
            const auto& tmpl = templates[rand() % templates.size()];
            HistoryEntry e;
            e.url   = tmpl.url;
            e.title = tmpl.title;
            int offset_sec = (tmpl.base_h * 3600) + (rand() % (tmpl.spread_h * 3600)) - (tmpl.spread_h * 1800);
            e.time_usec = base_usec + (int64_t)day * 86400LL * 1000000LL + (int64_t)offset_sec * 1000000LL;
            auto bt = usec_to_broken(e.time_usec);
            e.year = bt.year; e.month = bt.month; e.hour = bt.hour; e.dow = bt.dow;
            e.domain = extract_domain(e.url);
            if (e.url.find("google.com/search") != std::string::npos) {
                e.is_search = true;
                e.search_query = extract_query_param(e.url, "q");
            }
            entries.push_back(std::move(e));
        }
    }
    return analyze(entries);
}

// ══════════════════════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════════════════════

int main() {
    httplib::Server svr;
    svr.set_payload_max_length(50 * 1024 * 1024); // 50 MB — handles large History.json files

    // CORS
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

    // POST /api/analyze
    svr.Post("/api/analyze", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::cerr << "DEBUG body size=" << req.body.size() 
                  << " first200=[" << req.body.substr(0, 200) << "]\n";
            auto body  = json::parse(req.body);
            auto entries = parse_history(body);
            if (entries.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"No history entries found. Upload a Chrome History.json (Browser History array)."})", "application/json");
                return;
            }
            res.set_content(result_to_json(analyze(entries)).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err; err["error"] = std::string(e.what());
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/demo
    svr.Get("/api/demo", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(result_to_json(make_demo()).dump(), "application/json");
    });

    // GET /api/health
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","engine":"Browser DNA C++ v2.0"})", "application/json");
    });

    svr.set_mount_point("/", "./frontend");

    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout <<   "║   Browser DNA — C++ Engine  v2.0         ║\n";
    std::cout <<   "║   http://localhost:8080                   ║\n";
    std::cout <<   "║   Upload: Chrome > Settings > Export      ║\n";
    std::cout <<   "╚═══════════════════════════════════════════╝\n\n";

    svr.listen("0.0.0.0", 8080);
    return 0;
}