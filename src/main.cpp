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
    std::string domain;
    int visits;
    std::string category;
    std::string label;      // human-readable name
    std::string color;
};

struct SearchCluster {
    std::string theme;
    std::string icon;
    std::vector<std::string> queries;
    int total;
};

struct PersonalityResult {
    std::string archetype;
    std::string icon;
    std::string tagline;
    std::string narrative;
    std::vector<std::string> evidence;
    std::string color;
};

struct WrappedResult {
    // Core counts
    int total_visits;
    int total_searches;
    int unique_queries;
    int unique_domains;
    int year_start;
    int year_end;

    // Time patterns
    std::vector<int> month_counts;     // 12
    std::vector<int> hour_counts;      // 24
    std::vector<int> dow_counts;       // 7
    std::string peak_hour_label;
    std::string peak_day_label;
    std::string peak_month_label;
    double night_owl_pct;
    double weekend_pct;

    // Sites
    std::vector<TopSite> top_sites;    // top 12

    // Searches
    std::vector<std::pair<std::string,int>> top_searches;
    std::vector<SearchCluster> clusters;
    std::string longest_query;
    int longest_query_words;
    int question_count;
    double avg_query_words;

    // Categories
    std::vector<std::pair<std::string,int>> category_breakdown;

    // Personality
    PersonalityResult personality;

    // Fun stats
    double searches_per_day;
    double visits_per_day;
    int binge_sessions;        // sessions > 30 consecutive minutes
    std::string alter_ego;     // based on peak hour label
};

// ══════════════════════════════════════════════════════════
//  DOMAIN KNOWLEDGE BASE
// ══════════════════════════════════════════════════════════

struct DomainInfo { std::string label; std::string category; std::string color; };

static const std::unordered_map<std::string, DomainInfo> DOMAIN_DB = {
    // Career
    {"linkedin.com",          {"LinkedIn",       "Career",        "#0A66C2"}},
    {"careers.purdue.edu",    {"Purdue Careers",  "Career",       "#CEB888"}},
    {"glassdoor.com",         {"Glassdoor",      "Career",        "#0CAA41"}},
    {"indeed.com",            {"Indeed",         "Career",        "#2164F3"}},
    {"handshake.com",         {"Handshake",      "Career",        "#E95234"}},
    // School
    {"purdue.brightspace.com",{"Brightspace",    "School",        "#CEB888"}},
    {"sso.purdue.edu",        {"Purdue SSO",     "School",        "#9D7535"}},
    {"purdue.edu",            {"Purdue",         "School",        "#CEB888"}},
    {"piazza.com",            {"Piazza",         "School",        "#4285F4"}},
    {"gradescope.com",        {"Gradescope",     "School",        "#009BDE"}},
    // Coding
    {"leetcode.com",          {"LeetCode",       "Coding",        "#FFA116"}},
    {"github.com",            {"GitHub",         "Coding",        "#24292E"}},
    {"stackoverflow.com",     {"Stack Overflow", "Coding",        "#F48024"}},
    {"replit.com",            {"Replit",         "Coding",        "#F26207"}},
    {"codesandbox.io",        {"CodeSandbox",    "Coding",        "#151515"}},
    {"docs.google.com",       {"Google Docs",    "Productivity",  "#4285F4"}},
    // Shopping
    {"amazon.com",            {"Amazon",         "Shopping",      "#FF9900"}},
    {"aeropostale.com",       {"Aeropostale",    "Shopping",      "#EC1C24"}},
    {"us.shein.com",          {"SHEIN",          "Shopping",      "#E83E70"}},
    {"etsy.com",              {"Etsy",           "Shopping",      "#F1641E"}},
    {"pinterest.com",         {"Pinterest",      "Social",        "#E60023"}},
    // Social / Entertainment
    {"youtube.com",           {"YouTube",        "Entertainment", "#FF0000"}},
    {"instagram.com",         {"Instagram",      "Social",        "#E1306C"}},
    {"reddit.com",            {"Reddit",         "Social",        "#FF4500"}},
    {"tiktok.com",            {"TikTok",         "Social",        "#010101"}},
    {"spotify.com",           {"Spotify",        "Entertainment", "#1DB954"}},
    {"netflix.com",           {"Netflix",        "Entertainment", "#E50914"}},
    {"nytimes.com",           {"NY Times",       "News",          "#000000"}},
    {"theweeknd.com",         {"The Weeknd",     "Entertainment", "#8B0000"}},
    {"ticketmaster.com",      {"Ticketmaster",   "Entertainment", "#026CDF"}},
    // Productivity
    {"mail.google.com",       {"Gmail",          "Productivity",  "#EA4335"}},
    {"calendar.google.com",   {"Calendar",       "Productivity",  "#4285F4"}},
    {"google.com",            {"Google",         "Search",        "#4285F4"}},
    // Other
    {"duosecurity.com",       {"Duo Auth",       "Security",      "#6BBE4E"}},
    {"onboarding-us10.hr.cloud.sap",{"SAP HR",  "Work",          "#008FD3"}},
    {"xo.store",              {"XO Store",       "Shopping",      "#8B0000"}},
};

// ══════════════════════════════════════════════════════════
//  SEARCH CATEGORY KEYWORDS
// ══════════════════════════════════════════════════════════

static const std::vector<std::pair<std::string, std::vector<std::string>>> SEARCH_CATS = {
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
    auto start = s.find_first_not_of(" \t\n\r");
    auto end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Extract domain from URL string (fast, no library)
std::string extract_domain(const std::string& url) {
    // strip scheme
    size_t start = url.find("://");
    if (start == std::string::npos) start = 0;
    else start += 3;
    // strip www.
    if (url.substr(start, 4) == "www.") start += 4;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}

// Extract query param from URL
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
    // URL-decode common encodings
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

std::vector<HistoryEntry> parse_history(const json& root) {
    std::vector<HistoryEntry> entries;

    const json* items = nullptr;
    if (root.is_array()) items = &root;
    else if (root.contains("Browser History") && root["Browser History"].is_array())
        items = &root["Browser History"];
    else return entries;

    for (const auto& item : *items) {
        HistoryEntry e;
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

        e.domain = extract_domain(e.url);

        // Detect Google search
        if (e.url.find("google.com/search") != std::string::npos) {
            std::string q = extract_query_param(e.url, "q");
            if (!q.empty()) {
                e.is_search = true;
                e.search_query = trim(q);
            }
        }

        entries.push_back(std::move(e));
    }

    return entries;
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

    // Personality
    r.personality = compute_personality(cat_scores, r.night_owl_pct / 100.0,
                                        r.weekend_pct / 100.0, question_cnt,
                                        r.total_searches, r.peak_hour_label, r.top_sites);

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

    j["personality"] = {
        {"archetype", r.personality.archetype},
        {"icon",      r.personality.icon},
        {"tagline",   r.personality.tagline},
        {"narrative", r.personality.narrative},
        {"evidence",  r.personality.evidence},
        {"color",     r.personality.color}
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

    svr.set_mount_point("/", "/home/claude/search_wrapped/frontend");

    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout <<   "║   Browser DNA — C++ Engine  v2.0         ║\n";
    std::cout <<   "║   http://localhost:8080                   ║\n";
    std::cout <<   "║   Upload: Chrome > Settings > Export      ║\n";
    std::cout <<   "╚═══════════════════════════════════════════╝\n\n";

    svr.listen("0.0.0.0", 8080);
    return 0;
}
