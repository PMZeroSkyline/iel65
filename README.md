# iel65 — 雅思 6.5 背单词

- `words.tsv`：词库 + 学习记录（5274 词，来自 [kajweb/dict](https://github.com/kajweb/dict) 的 IELTS 词书，释义用 [ECDICT](https://github.com/skywind3000/ECDICT) 补全）
- `iel.cpp`：命令行刷词，会把对错写回 `words.tsv`
- `index.html`：手机网页，读取仓库里的 `words.tsv`，可以刷词（不保存）、看进度

## 数据表格式

`word	phonetic	meaning	seen	right	wrong	streak`（Tab 分隔）

`streak` 是连续答对次数，**连续答对 3 次 = 已掌握**，之后不再出题；答错清零。
想加词就直接在表里加一行，统计列填 `0 0 0 0`。

## 命令行

```sh
cmake -S . -B build && cmake --build build --config Release   # 产物：build/Release/iel.exe
iel            # 英→中，20 题
iel c2e 50     # 中→英，50 题
iel mix        # 混合
iel stat       # 看进度
iel review     # 只刷已掌握的词，答错踢回未掌握（可组合：iel c2e 30 review）
```

- 出题顺序：易错词 > 没学过的 > 学习中的，已掌握的跳过
- 英→中是软匹配：任意一个义项对上即可，可以输入多个意思（空格或逗号分开），会忽略词性、括号和"的"之类的差异
- 中→英要求拼写完全正确（不区分大小写），拼错一个字母会提示；答成另一个词会显示那个词的意思
- 答错时如果你的答案其实是别的词（比如 approve 写成"放弃"，那是 abandon 的意思），会提示出来；那个词要是已掌握，会问你是否移回学习中
- 已掌握的词答错时会先确认再移出（y=移出，回车=保留），避免同义词误杀；直接回车（不会）则直接移出
- 判错但你觉得自己是对的，输入 `y` 改判。回车 = 不会，`q` = 退出
- 出结果时自动朗读单词（Windows 自带语音 SAPI，离线，需要系统装有英文语音，默认自带 Zira）
- 每答一题就保存一次，刷完 `git commit -am "刷词" && git push`，网页就能看到最新进度

## 网页（GitHub Pages）

仓库 Settings → Pages → Source 选 `main` 分支根目录，打开 `https://<用户名>.github.io/iel65/`。
