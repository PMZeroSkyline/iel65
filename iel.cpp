// iel — 雅思 6.5 命令行背单词。用法见 README.md
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
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
#include <sapi.h>
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")
ISpVoice* voice = nullptr;  // Windows 自带语音合成，离线
#endif
// 异步朗读英文；新的朗读会打断上一个
void speak(const std::string& text) {
#ifdef _WIN32
    if (!voice) return;
    std::string xml = "<voice required=\"Language=409\">" + text + "</voice>";  // 409 = en-US
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, xml.c_str(), -1, nullptr, 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, xml.c_str(), -1, w.data(), (int)w.size());
    voice->Speak(w.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_XML, nullptr);
#endif
}
using namespace std;
namespace fs = std::filesystem;

struct Word { string w, ph, m; int seen = 0, right = 0, wrong = 0, streak = 0; string swept; };  // swept = 按章遍历里最近一次答过的日期
const int MASTER = 3;  // 连续答对 MASTER 次视为掌握
string header;
fs::path dbPath;

vector<Word> load() {
    ifstream f(dbPath); vector<Word> v; string line;
    getline(f, header);
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header.find("\tswept") == string::npos) header += "\tswept";  // 旧数据没有这一列
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        stringstream ss(line); Word x; string a[8];
        for (auto& s : a) getline(ss, s, '\t');
        x.w = a[0]; x.ph = a[1]; x.m = a[2];
        x.seen = atoi(a[3].c_str()); x.right = atoi(a[4].c_str());
        x.wrong = atoi(a[5].c_str()); x.streak = atoi(a[6].c_str()); x.swept = a[7];
        if (!x.w.empty()) v.push_back(x);
    }
    return v;
}
void save(const vector<Word>& v) {
    ofstream f(dbPath, ios::binary);
    f << header << '\n';
    for (auto& x : v) f << x.w << '\t' << x.ph << '\t' << x.m << '\t' << x.seen << '\t' << x.right << '\t' << x.wrong << '\t' << x.streak << '\t' << x.swept << '\n';
}
// "2026-11-14" -> 天数（只用于比较先后）；空字符串返回很小的数，表示从没过
long dayNum(const string& d) {
    int y, m, dd;
    if (sscanf(d.c_str(), "%d-%d-%d", &y, &m, &dd) != 3) return -1000000;
    y -= m <= 2; long era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy;
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

// ===== 学习时长：time.tsv 每天一行（日期、秒数、答题数）=====
map<string, long> times, answers;
const long IDLE_MS = 60000;  // 两次操作之间最多算 60 秒，走开不累加（网页端相同）
string dayStr(time_t t) { char b[16]; strftime(b, sizeof b, "%Y-%m-%d", localtime(&t)); return b; }
void loadTimes() {
    ifstream f(dbPath.parent_path() / "time.tsv"); string line;
    getline(f, line);
    while (getline(f, line)) {
        stringstream ss(line); string d, s, a;
        getline(ss, d, '\t'); getline(ss, s, '\t'); getline(ss, a, '\t');
        if (!d.empty()) { times[d] += atol(s.c_str()); answers[d] += atol(a.c_str()); }
    }
}
void saveTimes() {
    ofstream f(dbPath.parent_path() / "time.tsv", ios::binary);
    f << "date\tseconds\tanswers\n";
    for (auto& [d, s] : times) f << d << '\t' << s << '\t' << answers[d] << '\n';
}
string fmtTime(long s) {
    char b[32];
    if (s < 60) snprintf(b, sizeof b, "%ld 秒", s);
    else if (s < 3600) snprintf(b, sizeof b, "%ld 分钟", (s + 30) / 60);
    else snprintf(b, sizeof b, "%.1f 小时", s / 3600.0);
    return b;
}

void stat(const vector<Word>& v) {
    int m = 0, l = 0, u = 0, r = 0, w = 0;
    for (auto& x : v) { x.streak >= MASTER ? m++ : x.seen ? l++ : u++; r += x.right; w += x.wrong; }
    printf("总词数 %zu | 已掌握 %d | 学习中 %d | 未学 %d\n累计答题 %d | 对 %d | 错 %d | 正确率 %.1f%%\n",
           v.size(), m, l, u, r + w, r, w, r + w ? 100.0 * r / (r + w) : 0.0);
    long all = 0, two = 0; string last, now = dayStr(time(nullptr)), from = dayStr(time(nullptr) - 13 * 86400);
    for (auto& [d, s] : times) { all += s; if (d >= from) two += s; if (s > 0) last = d; }
    if (all) printf("学习时长 %s | 过去两周 %s | 今天 %s | 最后学习 %s\n", fmtTime(all).c_str(), fmtTime(two).c_str(),
                    fmtTime(times.count(now) ? times[now] : 0).c_str(), last.c_str());
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    CoInitialize(nullptr);
    CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&voice);
#endif
    string mode = "e2c", chapSpec; int n = 20; bool review = false, sweep = false, sweepStat = false;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "review") review = true;
        else if (a == "sweep") sweep = true;
        else if (sweep && a == "stat") sweepStat = true;
        else if (isdigit((unsigned char)a[0])) { if (sweep) chapSpec = a; else n = atoi(a.c_str()); }  // sweep 后面的数字是章节
        else mode = a;
    }
    for (fs::path p : {fs::current_path(), fs::absolute(argv[0]).parent_path(), fs::absolute(argv[0]).parent_path().parent_path(),
                       fs::absolute(argv[0]).parent_path().parent_path().parent_path()})
        if (fs::exists(p / "words.tsv")) { dbPath = p / "words.tsv"; break; }
    if (dbPath.empty()) { puts("找不到 words.tsv"); return 1; }
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
    const string q = " 2>nul ";
#else
    const string q = " 2>/dev/null ";
#endif
    const string g = "git -C \"" + dbPath.parent_path().string() + "\" ";
    if (mode != "push") {   // 网页可能上传过进度：远程有新提交就先 pull，避免之后推送冲突
        int behind = 0;
        if (FILE* p = popen((g + "fetch -q" + q + "&& " + g + "rev-list --count HEAD..@{u}" + q).c_str(), "r")) {
            if (fscanf(p, "%d", &behind) != 1) behind = 0;
            pclose(p);
        }
        if (behind > 0) {
            printf("远程有 %d 个新提交（可能是网页上传的进度）。回车=自动 git pull，n=跳过：", behind); fflush(stdout);
            string c; readLine(c);
            if (trim(c) != "n" && system((g + "pull --ff-only -q").c_str()) != 0) { puts("自动 pull 失败，请手动处理后再刷"); return 1; }
        }
    }
    auto v = load();
    loadTimes();
    if (mode == "stat") { stat(v); return 0; }
    if (mode == "push") {  // 只提交学习数据，然后推送；网页刚上传过就 rebase 后再推
        const string files = " -- words.tsv time.tsv";
        system((g + "add" + files + q).c_str());
        bool changed = system((g + "diff --cached --quiet" + files + q).c_str()) != 0;
        if (changed) {
            int m = 0, a = 0; for (auto& x : v) { m += x.streak >= MASTER; a += x.seen; }
            string msg = "刷词：已掌握 " + to_string(m) + "，累计答题 " + to_string(a) + "\n";
            FILE* p = popen((g + "commit -q -F -" + files).c_str(), "w");  // 从标准输入传说明，避免中文被命令行转码
            if (p) fputs(msg.c_str(), p);
            if (!p || pclose(p) != 0) { puts("提交失败"); return 1; }
            printf("已提交：%s", msg.c_str());
        } else puts("没有新记录要提交");
        if (system((g + "push -q" + q).c_str()) != 0) {
            if (system((g + "pull --rebase --autostash -q" + q).c_str()) != 0) {
                system((g + "rebase --abort" + q).c_str());
                puts("推送失败：和远程冲突（可能网页上传过同一批单词）。本地提交还在，请手动处理");
                return 1;
            }
            if (system((g + "push -q" + q).c_str()) != 0) { puts("推送失败，请检查网络后重新运行 iel push"); return 1; }
        }
        puts("已推送");
        return 0;
    }
    if (mode != "e2c" && mode != "c2e" && mode != "mix") {
        puts("用法: iel [e2c|c2e|mix|stat|push] [题数=20] [review]\n  e2c 看英文写中文  c2e 看中文写英文  mix 混合  stat 进度  push 提交并推送学习记录\n"
             "  review 只刷已掌握的词，答错踢回未掌握\n  iel sweep [章节|stat]  按章刷已掌握的词，如 iel sweep 1-3,5；stat 看每章进度");
        return 0;
    }
    mt19937 rng(random_device{}());
    vector<int> idx;
    const long today = dayNum(dayStr(time(nullptr)));
    auto age = [&](const string& d) {  // 从没过 / 今天 / 昨天 / N 天前
        if (d.empty()) return string("从没过");
        long n = today - dayNum(d);
        return n <= 0 ? string("今天") : n == 1 ? string("昨天") : to_string(n) + " 天前";
    };
    // 把一组词按 swept 日期分块：从没过（空）最先，然后从老到新
    auto blocks = [&](const vector<int>& ids) { map<string, int> c; for (int i : ids) c[v[i].swept]++; return c; };
    auto blockLine = [&](const map<string, int>& c) {
        string s; for (auto& [d, k] : c) s += (s.empty() ? "" : " · ") + age(d) + " " + to_string(k);
        return s;
    };
    if (sweep) {  // 按章刷：按上次刷的日期分块，最老的块先出，块内随机
        const int CH = 500, nch = ((int)v.size() + CH - 1) / CH;
        if (chapSpec.empty() || sweepStat) {
            for (int c = 0; c < nch; c++) {
                vector<int> ids; int e = min((c + 1) * CH, (int)v.size());
                for (int i = c * CH; i < e; i++) if (v[i].streak >= MASTER) ids.push_back(i);
                printf("第%2d章  已掌握 %3zu  %s\n", c + 1, ids.size(), ids.empty() ? "" : blockLine(blocks(ids)).c_str());
            }
            if (sweepStat) return 0;
            printf("刷哪几章？（如 1-3,5）："); fflush(stdout);
            if (!readLine(chapSpec)) return 0;
        }
        vector<bool> sel(nch + 1, false);
        stringstream ss(chapSpec); string part;  // 解析 "1-3,5"
        while (getline(ss, part, ',')) {
            int a = 0, b = 0;
            if (sscanf(part.c_str(), "%d-%d", &a, &b) < 2) b = a;
            for (int c = max(1, a); c <= min(b, nch); c++) sel[c] = true;
        }
        string names; for (int c = 1; c <= nch; c++) if (sel[c]) names += " " + to_string(c);
        for (int i = 0; i < (int)v.size(); i++) if (sel[i / CH + 1] && v[i].streak >= MASTER) idx.push_back(i);
        if (idx.empty()) { puts(names.empty() ? "没有选中任何章节" : "这几章还没有已掌握的单词"); return 0; }
        shuffle(idx.begin(), idx.end(), rng);  // 打乱后按日期稳定排序 = 块内随机
        stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return v[a].swept < v[b].swept; });
        printf("第%s 章：%zu 个（%s）\n", names.c_str(), idx.size(), blockLine(blocks(idx)).c_str());
    } else {
        // 选题：跳过已掌握（review 时只要已掌握）；乱序后按 易错（错过） > 学习中（刷过没错过） > 未学 排序
        for (int i = 0; i < (int)v.size(); i++) if ((v[i].streak >= MASTER) == review) idx.push_back(i);
        if (idx.empty()) { puts(review ? "还没有已掌握的单词" : "全部掌握了！"); return 0; }
        shuffle(idx.begin(), idx.end(), rng);
        auto rank = [&](int i) { auto& x = v[i]; return x.wrong > 0 ? 0 : x.seen > 0 ? 1 : 2; };
        stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return rank(a) < rank(b); });
        if ((int)idx.size() > n) idx.resize(n);
    }

    puts("回车=不会  q=退出  判错时输入 y 可改判为对\n");
    int done = 0, ok = 0;
    auto last = chrono::steady_clock::now(); long long ms = 0; long sessSec = 0;
    auto tick = [&] {  // 把距上次操作的时间记入今天，超过 IDLE_MS 的部分不算
        auto now = chrono::steady_clock::now();
        ms += min<long long>(IDLE_MS, chrono::duration_cast<chrono::milliseconds>(now - last).count());
        last = now;
        long s = (long)(ms / 1000); ms %= 1000;
        if (s) { times[dayStr(time(nullptr))] += s; sessSec += s; saveTimes(); }
    };
    vector<string> blk; for (int i : idx) blk.push_back(v[i].swept);  // 每题原来所在的块（答完 swept 会变成今天）
    for (size_t k = 0; k < idx.size(); k++) {
        auto& x = v[idx[k]];
        if (sweep && (k == 0 || blk[k - 1] != blk[k])) {  // 进入新的一块
            size_t e = k; while (e < idx.size() && blk[e] == blk[k]) e++;
            printf("—— %s的 %zu 个 ——\n", age(blk[k]).c_str(), e - k);
        }
        bool e2c = mode == "e2c" || (mode == "mix" && rng() % 2);
        printf("[%d/%zu] %s\n> ", done + 1, idx.size(), e2c ? (x.w + "  /" + x.ph + "/").c_str() : x.m.c_str());
        fflush(stdout);
        string in; bool got = readLine(in);
        tick();
        if (!got) break;
        in = trim(in);
        if (in == "q") break;
        bool right = false;
        vector<Word*> mixed;  // 和本题混淆的词：你写的答案其实是它们的意思/拼写
        if (!in.empty()) {
            right = e2c ? matchCn(in, x.m) : lower(in) == lower(x.w);
            if (!right && !e2c && editDist(lower(in), lower(x.w)) == 1) puts("  拼写差一点！");
            if (!right) for (auto& y : v)
                if (&y != &x && mixed.size() < 5 && (e2c ? matchCn(in, y.m) : lower(y.w) == lower(in))) mixed.push_back(&y);
            for (size_t k = 0; k < mixed.size(); k++) printf("  你的答案是 %s 的意思：%s\n", mixed[k]->w.c_str(), mixed[k]->m.c_str());
        }
        printf("%s  %s  /%s/  %s\n", right ? "√ 正确" : "× 错误", x.w.c_str(), x.ph.c_str(), x.m.c_str());
        speak(x.w);
        if (!right && !in.empty()) {
            printf("  (y 改判为对，回车继续) "); fflush(stdout);
            string c; if (readLine(c) && trim(c) == "y") right = true;
        }
        // 本题判错且没改判为对 → 直接踢回学习中；被混淆的已掌握词确认后才踢（避免同义词误杀）
        auto kick = [&](Word& y, bool ask) {
            if (y.streak < MASTER) return true;
            if (ask) {
                printf("  %s 是已掌握的词，移出已掌握？(y=移出，回车=保留) ", y.w.c_str()); fflush(stdout);
                string c; if (!readLine(c) || trim(c) != "y") return false;
            }
            printf("  ↓ %s 移出已掌握\n", y.w.c_str());
            return true;
        };
        if (!right) for (auto* y : mixed) if (y->streak >= MASTER && kick(*y, true)) y->streak = 0;
        x.seen++;
        if (sweep) x.swept = dayStr(time(nullptr));  // 记下这次遍历过的日期
        if (right) x.right++, x.streak++;
        else { x.wrong++; if (kick(x, false)) x.streak = 0; }
        if (x.streak == MASTER) puts("  ★ 已掌握，之后不再出现");
        done++; ok += right;
        answers[dayStr(time(nullptr))]++; times[dayStr(time(nullptr))]; saveTimes();  // 当天答题数（热力图用）
        save(v);
        tick();
        puts("");
    }
    printf("本轮 %d 题，答对 %d，用时 %s\n", done, ok, fmtTime(sessSec).c_str());
    stat(v);
#ifdef _WIN32
    if (voice) voice->WaitUntilDone(3000);  // 让最后一个词读完再退出
#endif
}
