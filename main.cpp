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
#include <chrono>
#include <ctime>
#include <numeric>
#include <cmath>

using json = nlohmann::json;

// ─────────────────────────────────────────
// DATA STRUCTURES
// ─────────────────────────────────────────

struct SearchEntry {
    std::string query;
    std::string raw_time;
    int year   = 0;
    int month  = 0; // 0-based
    int hour   = 0;
    int dow    = 0; // 0=Sun
};

struct WrappedResult {
    int    total_searches;
    int    unique_queries;
    int    year;
    std::string peak_month;
    std::string peak_hour_label;
    double avg_per_day;
    int    longest_query_words;
    std::string longest_query;
    std::vector<std::pair<std::string,int>> top_searches;  // query → count
    std::vector<int> month_counts;   // [0..11]
    std::vector<int> hour_counts;    // [0..23]
    std::vector<int> dow_counts;     // [0..6]
    std::vector<std::pair<std::string,int>> top_words;     // word → freq
    std::vector<std::pair<std::string,int>> top_themes;    // theme → score
    std::string personality_type;
    std::string personality_icon;
    std::string personality_headline;
    std::string personality_desc;
    std::vector<std::string> personality_traits;
    double night_owl_pct;    // % of searches 10pm-3am
    double weekend_pct;
    int    question_searches; // queries starting with how/why/what/when/where
    int    avg_query_words;
};

// ─────────────────────────────────────────
// STOP WORDS
// ─────────────────────────────────────────

static const std::set<std::string> STOP_WORDS = {
    "the","a","an","is","are","was","were","be","been","being",
    "have","has","had","do","does","did","will","would","could",
    "should","may","might","shall","can","need","dare","ought",
    "how","what","why","when","where","who","which","that","this",
    "these","those","then","than","too","very","just","but","and",
    "or","nor","for","so","yet","both","either","neither","not",
    "only","own","same","such","while","about","above","after",
    "again","against","all","any","because","before","between",
    "by","down","during","each","few","from","here","if","in",
    "into","it","its","itself","more","most","my","no","of",
    "off","on","once","out","over","own","per","some","still",
    "than","then","to","up","use","used","using","with","within",
    "without","you","your","i","me","we","our","they","their",
    "he","she","him","her","his","hers","get","got","go","going",
    "make","made","much","also","like","new","best","good","right",
    "long","old","time","year","work","part","take","come","know"
};

// ─────────────────────────────────────────
// THEME KEYWORDS
// ─────────────────────────────────────────

static const std::vector<std::pair<std::string, std::vector<std::string>>> THEMES = {
    {"Technology & AI",     {"ai","gpt","model","machine learning","deep learning","neural","llm","chatgpt","openai","python","javascript","react","code","api","software","algorithm","github","docker","kubernetes","programming","developer","framework","database","cloud","aws","tensorflow","pytorch","cuda"}},
    {"Food & Cooking",      {"recipe","food","restaurant","eat","cook","meal","diet","nutrition","calories","ingredient","dish","bake","grill","vegan","keto","cuisine","flavor","taste","dinner","lunch","breakfast","coffee","wine","beer","cocktail","sushi","pizza","pasta","burger"}},
    {"Health & Fitness",    {"exercise","workout","gym","run","yoga","sleep","health","fitness","weight","muscle","cardio","protein","vitamin","supplement","mental health","anxiety","depression","therapy","doctor","symptoms","pain","diet","calories","steps","heart rate"}},
    {"Science & Space",     {"science","physics","biology","chemistry","space","nasa","quantum","theory","experiment","research","study","universe","black hole","galaxy","planet","evolution","gene","dna","particle","atom","relativity","climate","earth","ocean","nature"}},
    {"News & Politics",     {"news","politics","election","president","government","policy","law","senate","congress","economy","inflation","stock","market","war","ukraine","china","russia","democracy","republican","democrat","vote","climate change","supreme court","media"}},
    {"Travel & Places",     {"travel","hotel","flight","trip","vacation","visit","city","country","map","restaurant","tourist","visa","passport","beach","mountain","museum","culture","language","currency","weather","timezone","airport","driving","directions"}},
    {"Entertainment",       {"movie","film","show","series","netflix","spotify","music","song","album","artist","game","gaming","youtube","twitch","podcast","book","anime","manga","comic","tv","watch","stream","listen","download","trailer","review","actor","director"}},
    {"Finance & Career",    {"salary","job","resume","interview","invest","stock","crypto","bitcoin","budget","mortgage","loan","tax","retirement","career","startup","business","entrepreneur","freelance","remote work","linkedin","side hustle","passive income"}},
    {"Shopping & Products",  {"buy","price","review","best","cheap","deal","amazon","ebay","discount","coupon","compare","vs","alternative","top","rated","recommend","purchase","cost","affordable","brand"}},
};

// ─────────────────────────────────────────
// TEXT UTILITIES
// ─────────────────────────────────────────

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string word;
    for (char c : text) {
        if (std::isalpha(c) || c == '\'' || c == '-') {
            word += std::tolower(c);
        } else {
            if (!word.empty()) { tokens.push_back(word); word.clear(); }
        }
    }
    if (!word.empty()) tokens.push_back(word);
    return tokens;
}

int count_words(const std::string& s) {
    std::istringstream iss(s);
    int count = 0;
    std::string word;
    while (iss >> word) count++;
    return count;
}

// ─────────────────────────────────────────
// TIME PARSING  (ISO 8601 or Google's format)
// ─────────────────────────────────────────

bool parse_time(const std::string& ts, int& year, int& month, int& hour, int& dow) {
    // Try ISO: "2024-03-15T22:30:00Z" or "2024-03-15T22:30:00.000Z"
    std::regex iso(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))");
    std::smatch m;
    if (std::regex_search(ts, m, iso)) {
        year  = std::stoi(m[1]);
        month = std::stoi(m[2]) - 1; // 0-based
        int day = std::stoi(m[3]);
        hour  = std::stoi(m[4]);
        // Zeller's congruence for DOW
        if (month < 2) { month += 12; year--; }
        int k = year % 100, j = year / 100;
        dow = (day + (13*(month+1))/5 + k + k/4 + j/4 - 2*j) % 7;
        dow = ((dow % 7) + 7) % 7;
        if (month >= 2) month -= 2; else { month += 10; year++; }
        month = std::stoi(m[2]) - 1;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────
// PARSE GOOGLE TAKEOUT JSON
// ─────────────────────────────────────────

std::vector<SearchEntry> parse_takeout(const json& root) {
    std::vector<SearchEntry> entries;

    // Takeout format: array of objects with "title" and "time"
    auto process_item = [&](const json& item) {
        if (!item.contains("title")) return;
        std::string title = item["title"].get<std::string>();

        // Strip "Searched for " prefix
        const std::string prefix = "Searched for ";
        if (title.rfind(prefix, 0) != 0) return;
        std::string query = trim(title.substr(prefix.size()));
        if (query.empty()) return;

        SearchEntry e;
        e.query = query;

        if (item.contains("time")) {
            e.raw_time = item["time"].get<std::string>();
            int yr, mo, hr, dw;
            if (parse_time(e.raw_time, yr, mo, hr, dw)) {
                e.year  = yr;
                e.month = mo;
                e.hour  = hr;
                e.dow   = dw;
            }
        }
        entries.push_back(e);
    };

    if (root.is_array()) {
        for (const auto& item : root) process_item(item);
    } else if (root.contains("items")) {
        for (const auto& item : root["items"]) process_item(item);
    }

    return entries;
}

// ─────────────────────────────────────────
// PERSONALITY ENGINE
// ─────────────────────────────────────────

struct PersonalityDef {
    std::string type;
    std::string icon;
    std::string headline;
    std::string desc;
    std::vector<std::string> traits;
};

PersonalityDef detect_personality(
    const std::vector<std::pair<std::string,int>>& themes,
    double night_owl_pct,
    double weekend_pct,
    int    question_searches,
    int    total,
    int    avg_words
) {
    // Map theme name → score
    std::unordered_map<std::string,int> ts;
    for (const auto& [name, score] : themes) ts[name] = score;

    bool heavy_tech    = ts["Technology & AI"]   > ts["Food & Cooking"] && ts["Technology & AI"] > ts["News & Politics"];
    bool heavy_food    = ts["Food & Cooking"]    > ts["Technology & AI"] && ts["Food & Cooking"] > ts["Science & Space"];
    bool heavy_sci     = ts["Science & Space"]   > ts["Food & Cooking"] && ts["Science & Space"] > ts["News & Politics"];
    bool heavy_news    = ts["News & Politics"]   > ts["Technology & AI"] && ts["News & Politics"] > ts["Science & Space"];
    bool heavy_travel  = ts["Travel & Places"]   > ts["Technology & AI"] && ts["Travel & Places"] > ts["Food & Cooking"];
    bool heavy_finance = ts["Finance & Career"]  > ts["Entertainment"] && ts["Finance & Career"] > ts["Health & Fitness"];
    bool night_owl     = night_owl_pct > 0.25;
    bool question_freak = (double)question_searches / std::max(total, 1) > 0.35;
    bool verbose        = avg_words >= 5;

    if (heavy_tech && night_owl) {
        return {"The Midnight Engineer", "⚡",
            "You debug at midnight and ship at dawn.",
            "Your searches are a changelog of the digital frontier. Stack traces, API docs, framework comparisons — you live in the terminal and your browser history is basically a second degree in computer science. The rest of the world is asleep when you're most productive.",
            {"Tabs upon tabs", "Night mode always on", "Stack Overflow VIP", "Early adopter", "Asks the rubber duck first"}};
    } else if (heavy_sci && question_freak) {
        return {"The Deep Diver", "🔭",
            "You don't just skim the surface.",
            "Your searches reveal a restless, curious mind that chases rabbit holes at all hours. You don't accept simple answers — your queries are long, multi-part, and often begin with 'why'. You've probably lost hours to Wikipedia spirals and you'd do it again.",
            {"Curious by default", "Wikipedia black hole survivor", "Verbose queries", "Rabbit hole specialist", "Asks the real questions"}};
    } else if (heavy_food) {
        return {"The Sensory Explorer", "🍜",
            "Life is too short for bad meals.",
            "You search with your senses. Restaurant recs in every city, recipes attempted at 9pm on a Tuesday, ingredient substitutions mid-cook. Your query history reads like a food diary crossed with a travel memoir — and it sounds delicious.",
            {"Searches before eating", "Recipe modifier", "Local food detective", "Weekend menu planner", "Knows every cuisine"}};
    } else if (heavy_travel) {
        return {"The Restless Wanderer", "✈️",
            "You're always planning the next escape.",
            "Flights, hotels, visa requirements, 'things to do in [city]' — your searches map a life in motion. You research obsessively before every trip and still manage to discover something unexpected once you arrive.",
            {"Packs light, searches heavy", "3-city tabs open always", "Knows exchange rates by heart", "Offline maps devotee", "First at the airport"}};
    } else if (heavy_news && !night_owl) {
        return {"The Informed Citizen", "📰",
            "You stay in the loop so others don't have to.",
            "Current events, market moves, policy debates — you search to understand the world. You fact-check things people say at dinner. Your friends probably ask you what's going on in the news and you definitely know.",
            {"Fact-checks everything", "Multiple sources, always", "Long-form reader", "Healthy skeptic", "First to know"}};
    } else if (heavy_finance) {
        return {"The Strategic Mind", "📈",
            "You play the long game.",
            "Salary benchmarks, investment strategies, career moves — your searches reflect someone building something deliberately. You're not just curious; you're optimizing. Every search is part of a larger plan.",
            {"Always optimizing", "Runs the numbers", "5-year plan person", "Obsessive researcher", "Plays chess not checkers"}};
    } else if (verbose && question_freak) {
        return {"The Philosopher", "💭",
            "You question everything.",
            "Your searches are sentences, not keywords. 'What is the meaning of...' 'Why do humans...' 'Is it possible that...' You treat Google like a philosophical sparring partner and you've probably had a search session that started with Python and ended at existentialism.",
            {"Searches in full sentences", "Connects unrelated ideas", "Comfortable with ambiguity", "Late-night thinker", "Too curious for their own good"}};
    } else {
        return {"The Generalist", "🌐",
            "You contain multitudes.",
            "No single obsession defines your searches — and that's your superpower. You pivot from recipe research to geopolitics to obscure historical facts within a single afternoon. You're hard to predict, impossible to categorize, and genuinely interesting to talk to.",
            {"Curious about everything", "No rabbit hole too deep", "Jack of all searches", "Surprising conversation topics", "Impossible to out-random"}};
    }
}

// ─────────────────────────────────────────
// CORE ANALYSIS
// ─────────────────────────────────────────

WrappedResult analyze(const std::vector<SearchEntry>& entries) {
    WrappedResult r;
    r.total_searches  = (int)entries.size();
    r.month_counts    = std::vector<int>(12, 0);
    r.hour_counts     = std::vector<int>(24, 0);
    r.dow_counts      = std::vector<int>(7, 0);

    // Determine dominant year
    std::unordered_map<int,int> year_freq;
    for (const auto& e : entries) if (e.year > 0) year_freq[e.year]++;
    r.year = 2024;
    if (!year_freq.empty()) {
        r.year = std::max_element(year_freq.begin(), year_freq.end(),
            [](const auto& a, const auto& b){ return a.second < b.second; })->first;
    }

    // Counts
    std::unordered_map<std::string,int> query_freq;
    std::unordered_map<std::string,int> word_freq;
    int total_words    = 0;
    int night_owl_cnt  = 0;
    int weekend_cnt    = 0;
    int question_cnt   = 0;
    r.longest_query    = "";
    int longest_wc     = 0;

    for (const auto& e : entries) {
        std::string ql = to_lower(e.query);
        query_freq[ql]++;

        // Month/hour/dow
        if (e.year == r.year || e.year == 0) {
            if (e.month >= 0 && e.month < 12) r.month_counts[e.month]++;
            if (e.hour  >= 0 && e.hour  < 24) r.hour_counts[e.hour]++;
            if (e.dow   >= 0 && e.dow   <  7) r.dow_counts[e.dow]++;
        }

        // Word stats
        auto words = tokenize(ql);
        int wc = (int)words.size();
        total_words += wc;
        if (wc > longest_wc) { longest_wc = wc; r.longest_query = e.query; }

        for (const auto& w : words) {
            if (w.size() > 3 && STOP_WORDS.find(w) == STOP_WORDS.end()) {
                word_freq[w]++;
            }
        }

        // Night owl: 22:00-03:00
        if (e.hour >= 22 || e.hour <= 3) night_owl_cnt++;

        // Weekend: Sat(7) or Sun(1) in Zeller — actually Sat=0,Sun=1 varies; use 0=Sun,6=Sat
        if (e.dow == 0 || e.dow == 6) weekend_cnt++;

        // Question searches
        if (ql.rfind("how ",0)==0 || ql.rfind("why ",0)==0 || ql.rfind("what ",0)==0 ||
            ql.rfind("when ",0)==0 || ql.rfind("where ",0)==0 || ql.rfind("is ",0)==0 ||
            ql.rfind("does ",0)==0 || ql.rfind("can ",0)==0) {
            question_cnt++;
        }
    }

    r.unique_queries    = (int)query_freq.size();
    r.longest_query_words = longest_wc;
    r.night_owl_pct     = r.total_searches > 0 ? (double)night_owl_cnt / r.total_searches : 0.0;
    r.weekend_pct       = r.total_searches > 0 ? (double)weekend_cnt   / r.total_searches : 0.0;
    r.question_searches = question_cnt;
    r.avg_per_day       = r.total_searches / 365.0;
    r.avg_query_words   = r.total_searches > 0 ? total_words / r.total_searches : 3;

    // Top searches (top 8)
    std::vector<std::pair<std::string,int>> sorted_queries(query_freq.begin(), query_freq.end());
    std::sort(sorted_queries.begin(), sorted_queries.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    r.top_searches = std::vector<std::pair<std::string,int>>(sorted_queries.begin(),
        sorted_queries.begin() + std::min((int)sorted_queries.size(), 8));

    // Top words (top 20)
    std::vector<std::pair<std::string,int>> sorted_words(word_freq.begin(), word_freq.end());
    std::sort(sorted_words.begin(), sorted_words.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    r.top_words = std::vector<std::pair<std::string,int>>(sorted_words.begin(),
        sorted_words.begin() + std::min((int)sorted_words.size(), 20));

    // Theme scoring
    std::string all_text;
    for (const auto& [q, _] : query_freq) all_text += " " + q;

    std::vector<std::pair<std::string,int>> theme_scores;
    for (const auto& [theme_name, keywords] : THEMES) {
        int score = 0;
        for (const auto& kw : keywords) {
            size_t pos = 0;
            while ((pos = all_text.find(kw, pos)) != std::string::npos) {
                score++; pos += kw.size();
            }
        }
        theme_scores.push_back({theme_name, score});
    }
    std::sort(theme_scores.begin(), theme_scores.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    r.top_themes = theme_scores;

    // Peak month
    static const std::string MONTH_NAMES[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    int peak_mo = (int)(std::max_element(r.month_counts.begin(), r.month_counts.end()) - r.month_counts.begin());
    r.peak_month = MONTH_NAMES[peak_mo];

    // Peak hour label
    int peak_hr = (int)(std::max_element(r.hour_counts.begin(), r.hour_counts.end()) - r.hour_counts.begin());
    if (peak_hr == 0) r.peak_hour_label = "midnight";
    else if (peak_hr < 12) r.peak_hour_label = std::to_string(peak_hr) + "am";
    else if (peak_hr == 12) r.peak_hour_label = "noon";
    else r.peak_hour_label = std::to_string(peak_hr - 12) + "pm";

    // Personality
    auto p = detect_personality(theme_scores, r.night_owl_pct, r.weekend_pct,
                                r.question_searches, r.total_searches, r.avg_query_words);
    r.personality_type     = p.type;
    r.personality_icon     = p.icon;
    r.personality_headline = p.headline;
    r.personality_desc     = p.desc;
    r.personality_traits   = p.traits;

    return r;
}

// ─────────────────────────────────────────
// JSON SERIALISATION
// ─────────────────────────────────────────

json result_to_json(const WrappedResult& r) {
    json j;
    j["total_searches"]     = r.total_searches;
    j["unique_queries"]     = r.unique_queries;
    j["year"]               = r.year;
    j["peak_month"]         = r.peak_month;
    j["peak_hour"]          = r.peak_hour_label;
    j["avg_per_day"]        = std::round(r.avg_per_day * 10) / 10.0;
    j["longest_query"]      = r.longest_query;
    j["longest_query_words"]= r.longest_query_words;
    j["night_owl_pct"]      = std::round(r.night_owl_pct * 1000) / 10.0;
    j["weekend_pct"]        = std::round(r.weekend_pct   * 1000) / 10.0;
    j["question_searches"]  = r.question_searches;
    j["avg_query_words"]    = r.avg_query_words;

    j["month_counts"] = r.month_counts;
    j["hour_counts"]  = r.hour_counts;
    j["dow_counts"]   = r.dow_counts;

    json top = json::array();
    for (const auto& [q, c] : r.top_searches) {
        json item; item["query"] = q; item["count"] = c;
        top.push_back(item);
    }
    j["top_searches"] = top;

    json words = json::array();
    for (const auto& [w, c] : r.top_words) {
        json item; item["word"] = w; item["count"] = c;
        words.push_back(item);
    }
    j["top_words"] = words;

    json themes = json::array();
    for (const auto& [n, s] : r.top_themes) {
        json item; item["name"] = n; item["score"] = s;
        themes.push_back(item);
    }
    j["top_themes"] = themes;

    j["personality"] = {
        {"type",     r.personality_type},
        {"icon",     r.personality_icon},
        {"headline", r.personality_headline},
        {"desc",     r.personality_desc},
        {"traits",   r.personality_traits}
    };

    return j;
}

// ─────────────────────────────────────────
// DEMO DATA GENERATOR
// ─────────────────────────────────────────

WrappedResult make_demo() {
    std::vector<SearchEntry> entries;
    std::vector<std::string> demo_queries = {
        "how does transformer architecture work", "best ramen in tokyo japan", "why do cats knock things off tables",
        "dark matter explained simply", "is sourdough actually healthier than white bread",
        "packing list 2 weeks europe backpack", "what time is it in auckland nz",
        "openai gpt-4 vs claude sonnet benchmark", "recipe for shakshuka easy",
        "quantum entanglement explained for beginners", "best mechanical keyboard 2024",
        "how to negotiate salary software engineer", "airbnb vs hotel which is better",
        "why is the sky blue scattering light", "how to make cold brew coffee at home",
        "best beginner python projects portfolio", "climate change solutions renewable energy",
        "how to sleep better circadian rhythm", "noise cancelling headphones review",
        "stoicism philosophy for modern life", "how to start a side project developer",
        "what is retrieval augmented generation", "best hiking trails colorado springs",
        "how long to boil pasta al dente", "rust programming language vs c++ performance",
        "how to read more books habit stacking", "anthropic claude vs chatgpt comparison",
        "what causes deja vu neuroscience", "intermittent fasting 16 8 benefits",
        "docker kubernetes difference explained", "how do black holes form end of star",
    };

    // Simulate 3800 entries spread across 2024
    srand(42);
    for (int i = 0; i < 3800; i++) {
        SearchEntry e;
        int qi = rand() % demo_queries.size();
        // Add slight variation
        e.query = demo_queries[qi] + (rand() % 3 == 0 ? " 2024" : "");
        e.year  = 2024;
        e.month = rand() % 12;
        // Bias toward evening hours (typical user)
        int h = rand() % 24;
        e.hour  = (h < 12) ? (h + 16) % 24 : h % 24;
        e.dow   = rand() % 7;
        entries.push_back(e);
    }

    return analyze(entries);
}

// ─────────────────────────────────────────
// MAIN / SERVER
// ─────────────────────────────────────────

int main() {
    httplib::Server svr;

    // Allow CORS for local dev
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // ── POST /api/analyze ──────────────────
    svr.Post("/api/analyze", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            auto entries = parse_takeout(body);

            if (entries.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"No search entries found. Make sure you uploaded a Google Takeout My Activity JSON."})", "application/json");
                return;
            }

            auto result = analyze(entries);
            auto j = result_to_json(result);
            res.set_content(j.dump(), "application/json");

        } catch (const json::exception& e) {
            res.status = 400;
            json err; err["error"] = std::string("JSON parse error: ") + e.what();
            res.set_content(err.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err; err["error"] = std::string("Server error: ") + e.what();
            res.set_content(err.dump(), "application/json");
        }
    });

    // ── GET /api/demo ──────────────────────
    svr.Get("/api/demo", [](const httplib::Request&, httplib::Response& res) {
        auto result = make_demo();
        auto j = result_to_json(result);
        res.set_content(j.dump(), "application/json");
    });

    // ── GET /api/health ───────────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","engine":"C++ Search Wrapped v1.0"})", "application/json");
    });

    // ── Serve frontend static files ────────
    svr.set_mount_point("/", "/home/claude/search_wrapped/frontend");

    std::cout << "╔═══════════════════════════════════════╗\n";
    std::cout << "║   Search Wrapped — C++ Backend        ║\n";
    std::cout << "║   http://localhost:8080               ║\n";
    std::cout << "╚═══════════════════════════════════════╝\n";

    svr.listen("0.0.0.0", 8080);
    return 0;
}