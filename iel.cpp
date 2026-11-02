// iel — 雅思 6.5 命令行背单词。用法见 README.md
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using namespace std;
namespace fs = std::filesystem;

struct Word { string w, ph, m; int seen = 0, right = 0, wrong = 0, streak = 0; };
const int MASTER = 3;  // 连续答对 MASTER 次视为掌握
string header;
fs::path dbPath;

vector<Word> load() {
    ifstream f(dbPath); vector<Word> v; string line;
    getline(f, header);
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        stringstream ss(line); Word x; string a[7];
        for (auto& s : a) getline(ss, s, '\t');
        x.w = a[0]; x.ph = a[1]; x.m = a[2];
        x.seen = atoi(a[3].c_str()); x.right = atoi(a[4].c_str());
        x.wrong = atoi(a[5].c_str()); x.streak = atoi(a[6].c_str());
        if (!x.w.empty()) v.push_back(x);
    }
    return v;
}
void save(const vector<Word>& v) {
    ofstream f(dbPath, ios::binary);
    f << header << '\n';
    for (auto& x : v) f << x.w << '\t' << x.ph << '\t' << x.m << '\t' << x.seen << '\t' << x.right << '\t' << x.wrong << '\t' << x.streak << '\n';
}

bool readLine(string& s) {
#ifdef _WIN32  // 控制台下用宽字符读，避免中文输入乱码
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE); DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        wchar_t buf[1024]; DWORD n;
        if (!ReadConsoleW(h, buf, 1023, &n, nullptr) || n == 0) return false;
        while (n && (buf[n - 1] == L'\n' || buf[n - 1] == L'\r')) n--;
        int len = WideCharToMultiByte(CP_UTF8, 0, buf, n, nullptr, 0, nullptr, nullptr);
        s.assign(len, 0); WideCharToMultiByte(CP_UTF8, 0, buf, n, s.data(), len, nullptr, nullptr);
        return true;
    }
#endif
    if (!getline(cin, s)) return false;
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return true;
}

u32string u32(const string& s) {  // UTF-8 → code points
    u32string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i]; int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0x3F >> (n - 1));
        for (int k = 1; k < n && i + k < s.size(); k++) cp = cp << 6 | (s[i + k] & 0x3F);
        r += cp; i += n;
    }
    return r;
}
string lower(string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }
string trim(const string& s) { size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t"); return a == string::npos ? "" : s.substr(a, b - a + 1); }

// 把释义/输入切成若干义项：去掉词性、括号内容，按标点和空白分割
vector<u32string> senses(const string& text) {
    u32string t = u32(text), cur; vector<u32string> out; int depth = 0;
    const u32string seps = U"；;，,、|/… \t", open = U"(（[【<《", close = U")）]】>》";
    for (size_t i = 0; i < t.size(); i++) {
        char32_t c = t[i];
        if (open.find(c) != u32string::npos) { depth++; continue; }
        if (close.find(c) != u32string::npos) { depth = max(0, depth - 1); continue; }
        if (depth) continue;
        if (c < 128 && (isalpha((int)c) || c == '.')) continue;  // 词性 n. adj. 等
        if (seps.find(c) != u32string::npos) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// 中文软匹配：输入的任一片段与任一义项 包含/被包含，或字符重合度足够高
bool matchCn(const string& input, const string& meaning) {
    for (auto& a : senses(input)) for (auto& s : senses(meaning)) {
        if (a.size() >= 2 || s.size() == 1) if (s.find(a) != u32string::npos || a.find(s) != u32string::npos) return true;
        int common = 0; for (char32_t c : a) common += s.find(c) != u32string::npos;
        if (common >= 2 && common * 10 >= (int)a.size() * 6 && common * 2 >= (int)s.size()) return true;
    }
    return false;
}
int editDist(const string& a, const string& b) {
    vector<int> d(b.size() + 1); for (size_t j = 0; j <= b.size(); j++) d[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        int prev = d[0]; d[0] = i;
        for (size_t j = 1; j <= b.size(); j++) { int t = d[j]; d[j] = min({d[j] + 1, d[j - 1] + 1, prev + (a[i - 1] != b[j - 1])}); prev = t; }
    }
    return d[b.size()];
}

void stat(const vector<Word>& v) {
    int m = 0, l = 0, u = 0, r = 0, w = 0;
    for (auto& x : v) { x.streak >= MASTER ? m++ : x.seen ? l++ : u++; r += x.right; w += x.wrong; }
    printf("总词数 %zu | 已掌握 %d | 学习中 %d | 未学 %d\n累计答题 %d | 对 %d | 错 %d | 正确率 %.1f%%\n",
           v.size(), m, l, u, r + w, r, w, r + w ? 100.0 * r / (r + w) : 0.0);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    string mode = argc > 1 ? argv[1] : "e2c";
    int n = argc > 2 ? atoi(argv[2]) : 20;
    for (fs::path p : {fs::current_path(), fs::absolute(argv[0]).parent_path(), fs::absolute(argv[0]).parent_path().parent_path(),
                       fs::absolute(argv[0]).parent_path().parent_path().parent_path()})
        if (fs::exists(p / "words.tsv")) { dbPath = p / "words.tsv"; break; }
    if (dbPath.empty()) { puts("找不到 words.tsv"); return 1; }
    auto v = load();
    if (mode == "stat") { stat(v); return 0; }
    if (mode != "e2c" && mode != "c2e" && mode != "mix") {
        puts("用法: iel [e2c|c2e|mix|stat] [题数=20]\n  e2c 看英文写中文  c2e 看中文写英文  mix 混合  stat 进度");
        return 0;
    }
    // 选题：跳过已掌握；乱序后按 易错词 > 未学 > 学习中 排序
    mt19937 rng(random_device{}());
    vector<int> idx;
    for (int i = 0; i < (int)v.size(); i++) if (v[i].streak < MASTER) idx.push_back(i);
    shuffle(idx.begin(), idx.end(), rng);
    auto rank = [&](int i) { auto& x = v[i]; return x.seen == 0 ? 1 : x.wrong > x.right || x.streak == 0 ? 0 : 2; };
    stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return rank(a) < rank(b); });
    if ((int)idx.size() > n) idx.resize(n);

    puts("回车=不会  q=退出  判错时输入 y 可改判为对\n");
    int done = 0, ok = 0;
    for (int i : idx) {
        auto& x = v[i];
        bool e2c = mode == "e2c" || (mode == "mix" && rng() % 2);
        printf("[%d/%zu] %s\n> ", done + 1, idx.size(), e2c ? (x.w + "  /" + x.ph + "/").c_str() : x.m.c_str());
        fflush(stdout);
        string in; if (!readLine(in)) break;
        in = trim(in);
        if (in == "q") break;
        bool right = false;
        if (!in.empty()) {
            if (e2c) right = matchCn(in, x.m);
            else {
                right = lower(in) == lower(x.w);
                if (!right && editDist(lower(in), lower(x.w)) == 1) puts("  拼写差一点！");
                if (!right) for (auto& y : v) if (lower(y.w) == lower(in)) { printf("  %s 的意思是：%s\n", y.w.c_str(), y.m.c_str()); break; }
            }
        }
        printf("%s  %s  /%s/  %s\n", right ? "√ 正确" : "× 错误", x.w.c_str(), x.ph.c_str(), x.m.c_str());
        if (!right && !in.empty()) {
            printf("  (y 改判为对，回车继续) "); fflush(stdout);
            string c; if (readLine(c) && trim(c) == "y") right = true;
        }
        x.seen++; right ? (x.right++, x.streak++) : (x.wrong++, x.streak = 0);
        if (x.streak == MASTER) puts("  ★ 已掌握，之后不再出现");
        done++; ok += right;
        save(v);
        puts("");
    }
    printf("本轮 %d 题，答对 %d\n", done, ok);
    stat(v);
}
