# Rapid Descent 仕様（mycrawl）

Rapid Descent は Dungeon Descent の短縮版モードです。目標はクリアまでのプレイ時間を
Descent の約半分にすることです。そのために、branch の構成（どの branch がどの順で
つながるか）は変えずに、各 branch の階数をおよそ半分に減らします。階数を減らすと、
そのままでは経験値やアイテムがクリア水準に届きません。そこで、残した階で得られる
経験値・信仰・床アイテムを増やし、飛ばした階の shop・portal・良品の分も抽選して
補います。一方、戦闘で使い切る消耗品は、戦闘が減った分だけ減らします。

- メニュー: "Rapid Descent"
- rc: `type = rapid-descent`
- コマンドライン: `crawl -rapid-descent`
- セーブ: `saves/rapid-descent/`。スコアファイルとモーグは Descent とは別になります。
- Descent のルールはすべてそのまま適用されます。具体的には次のとおりです。
  - 一方通行で、降りた直後だけ上り階段で一つ前の階を覗き戻れる。
  - Temple と Tomb はない。
  - 水系・毒系の branch はそれぞれ片方だけ。
  - Zot に入るのにルーンは不要。
  - Gem はない。
  - 罠でプレイヤーが落ちたり飛ばされたりしない。
  - 開始時に shop voucher が 1 枚ある。
- 実装: `source/rapid-descent.{h,cc}`。コード中では `game_is_descent()` が Rapid
  Descent でも真になり、`game_is_rapid_descent()` で区別します。

## 1. 参考にした短縮版

| 作品 | 短縮のしかた | 生成深度の扱い | 補填 |
|---|---|---|---|
| Dungeon Crawl Chili（DCSS 0.34 系） | 各 branch の最下層を 1 階削る（D15→14、Lair 5→4、Lair 支流 4→3、Vaults/Slime 5→4、Depths 4→3、地獄 7→6、Zot 5→4）。3 ルーンの経路で約 17% 減 | absdepth は変えない。各階は元の深さのまま生成される | 経験値 +10%（以前は +5%）、D:1 に shop、D:2-3 に祭壇 |
| Crawl Light 0.2（DCSS 派生、2012） | D27→18、Lair/Vaults 8→5 など | モンスターの出現深度を調整 | モンスター経験値 +50%、時間経過湧きなし |
| bcrawl | D12、Vaults 3、Lair 支流 3、Orc 1 など | － | shop を出やすく |
| Rapid Brogue（Brogue CE） | 40 階→10 階、アミュレットは D26→D6（約 1/4） | `depthAccelerator=4` は金・光量などにだけ使う。モンスター群の出現深度は表を手で調整 | 総量を一定に保つ方針。強化の巻物（効果 2 倍）・生命の薬・筋力の薬（効果 2 倍）がそれぞれ毎階 1 個程度、毎階 +4 アイテム、報酬部屋が約 1.5 部屋/階、ルミネストーン総数は同じ、識別も速い |
| Bullet Brogue | アミュレット D3、最深 D5 | accelerator 8、表も調整 | 強化・筋力が約 2 個/階（効果 2 倍）、生命が約 1 個/階、毎階 +8 アイテム、報酬部屋が約 3 部屋/階、D1 に武器 vault を確定配置 |
| Quickband（NPPAngband 系） | 12 階 | `effective_depth` 表（1→1、2→5、…、12→44）でモンスターとアイテムの生成レベルを決める | 経験値表を短縮、良品を多めに、モンスター数を増やす |
| FrogComposband Coffee-break | 階段がすべて 2 階分落ちる穴になり、クエスト階では必ず止まる。約半分の階だけを訪れる | 生成は本来の深さのまま。重要な守護者は必ず訪れる階に置く | 撃破経験値 ×8（高レベルでは逓減）、ドロップ ×2、良品率を上げる |
| DCSS Sprint | 1 階だけ | － | スキル経験値 ×9、信仰 ×9（探索型の神は ×27、Oka・Usk は除外）。呪文練習による信仰は ×1/9 に戻して二重取りを防ぐ |

ここから得た設計指針:

1. **深度の付け替え（Quickband・Brogue 型）より、本来の深度のまま階を飛ばす方式
   （Coffee-break・Chili 型）が DCSS には向いています。** DCSS の生成は「その階の
   深さ」に強く依存しています。具体的にはモンスターの出現表、vault の `DEPTH:` タグ、
   `$`（最下層）指定、des 内の Lua（`you.depth()` や `you.absdepth()`）です。階を
   飛ばす方式ならこれらを一切変えずに済みます。
2. **重要な階は必ず残します（Coffee-break のクエスト階に相当）。** 各 branch の最下層
   には、ルーン、branch 末端の vault、次の branch の入口、Orb があります。
3. **総量をそろえます（Rapid Brogue）。** 補填は「飛ばした分」に比例させ、objstat で
   実測して確かめます。
4. **二重取りを避けます（Sprint）。** 経験値で増えるものと信仰で増えるものが重ならない
   ようにします。

## 2. 階層構成

各 branch で残す階は次のとおりです。それ以外の階は存在しません。

| Branch | Descent | Rapid | 残す階 |
|---|---|---|---|
| Dungeon | 12 | 6 | 1, 3, 5, 8, 10, 12 |
| Lair | 5 | 3 | 1, 3, 5 |
| Orcish Mines | 2 | 1 | 2 |
| Swamp / Shoals / Snake / Spider | 4 | 2 | 2, 4 |
| Elven Halls | 3 | 2 | 1, 3 |
| Crypt | 3 | 2 | 1, 3 |
| Vaults | 5 | 3 | 1, 3, 5 |
| Slime Pits | 5 | 3 | 1, 3, 5 |
| Depths | 4 | 2 | 2, 4 |
| Zot | 5 | 3 | 1, 3, 5 |

- 決め方: 最下層を必ず残し、そこから上へ 1 階おきに残します。D だけは開始階の D:1 を
  残し、残りを均等に配置します。
- 通常のルート（D → Lair → Orc → Lair 支流 → Elf か Crypt → Vaults → Slime →
  Depths → Zot）は 45 階から 25 階（56%）になります。Slime を通らないルートは
  40 階から 22 階（55%）です。
- 階の名前は本来の深さのままです（D:1 → D:3 → D:5 → D:8 …）。表示上の番号が、
  そのまま難しさの目安になります。
- portal（Sewer、Ossuary など）、Abyss、Pandemonium、ziggurat は短縮しません。

階のつながり（すべて「次に存在する階」へ読み替えます）:

- 下り階段と下り hatch は次の残存階へ、覗き戻り用の上り階段は一つ前の残存階へ
  つながります。branch の入口はその branch の最初の残存階（Orc なら Orc:2）へ
  つながります。
- 旅行コマンド（`G` の既定階や `<`/`>`、存在しない階を入力したときの補正）、
  ダンジョン概要、落とし穴の行き先、`-pregen` も同じ読み替えをします。
- Descent では神の祭壇がすべて D:3〜10 の overflow 祭壇になります。Rapid Descent
  では、これを残存する D の階（3, 5, 8, 10）だけに置きます。
- Delver は 2 番目の残存階（D:3）から始まります。

## 3. 補填

### モデル: 残した階が飛ばした階を肩代わりする

branch の本来の階数を N、残す階数を M とします。

- **最下層は 1 階分として数えます。** ルーン vault・末端 vault・Orb は一度きりの
  内容で、階を飛ばしても失われないからです。
- **最下層以外の残存階は、残りの N−1 階を均等に肩代わりします。** 1 階あたりの倍率は
  r = (N−1)/(M−1) です。
- 合計すると branch 全体でちょうど N 階分になります（M=1 の Orc は Orc:1 の分を
  補填しません）。

| Branch | 最下層以外の倍率 r | 最下層 |
|---|---|---|
| Dungeon | 11/5 = 2.2 | 1 |
| Lair、Vaults、Slime、Zot | 4/2 = 2 | 1 |
| Swamp / Shoals / Snake / Spider、Depths | 3/1 = 3 | 1 |
| Elf、Crypt | 2/1 = 2 | 1 |
| Orc | （残存階は最下層のみ） | 1 |
| その他（portal など） | 1 | 1 |

### 何に倍率を掛けるか

いずれも、獲得した時点でプレイヤーがいる階の倍率を使います。

- **経験値** ×r（`gain_exp()`）。XL とスキルの両方が増えます。経験値で治る状態
  （drain からの回復、bane の解除）、神の怒りの間隔、evolution の変異も同じ比率で
  進みます。Enkindle の充填も ×r です。
- **信仰** ×r（`gain_piety()`）。撃破による信仰も探索による信仰（Nemelex、Hep、
  Dith、Ely、Jiyva）も対象です。Ashenzari の呪い進行と Ru の生贄進行も ×r です。
  - 例外は Uskayaw で、信仰が戦闘ごとに増減するため倍率を掛けません。
  - 呪文練習による信仰は、経験値の増加ですでに増えているので 1/r に戻します（Sprint
    と同じ扱い）。
  - Zin の献金（tithe と寄付）による信仰も 1/r に戻します。金そのものがすでに補填
    されているためです。
- **床アイテム** ×r ×1.25（`_builder_items()`。最下層は ×1.25）。×1.25
  （`RAPID_DESCENT_ITEM_BONUS`）は、飛ばした階の vault に入っていたはずの戦利品の
  分です。金も床アイテムに含まれます。消耗品は次の「減らすもの」で間引きます。
- **Gozag の死体の金** ×r。

単純な「経験値とアイテムを 2 倍」にしない理由: 残す階は深い側の階と最下層です。
こちらの方がモンスターも多く強いので、一律 2 倍では多すぎます（実測は次節）。

### 飛ばした階の分も抽選するもの

残した階は、受け持つ飛ばした階（その下の、次の残存階までの階。branch の最初の残存階
なら、その上の階も）の分も、次のものを抽選します（`rapid_descent_stand_ins()`）。

- **shop**: serial_shops（shop をまとめて置く CHANCE vault）の確率を「1 + 受け持つ
  階数」倍にします（上限 100%）。
- **portal**: 受け持つ階ごとに、その階の深さで portal 入口の CHANCE（Sewer、Ossuary、
  Bailey、Ice Cave、Volcano、Wizlab、Desolation、Necropolis）と Gauntlet を振り直し、
  当たったものをこの階に置きます。Vaults の部屋で出る Wizlab・Desolation・
  Necropolis も同じ回数だけ振ります（`dgn.rapid_descent_portal_rolls`）。
- **良品**: 受け持つ階 1 つにつき 0.5 個（`RAPID_DESCENT_GOOD_ITEMS`）、vault の
  `|` と同じ作り方の良品を床に置きます。飛ばした階の vault にあったアーティファクトの
  分です。

### 減らすもの

階が減った分、戦闘も減ります（モンスター数は Descent の 0.64）。戦闘で使い切る
資源は、戦闘の量に合わせて減らします。経験値・装備・信仰など、最後まで残る強さは
減らしません。

- **床の消耗品**: 床アイテムとして作られた巻物・薬・ワンドは、確率 50%
  （`RAPID_DESCENT_CONSUMABLE_KEEP`）でしか置きません。結果として Descent の約 0.7 に
  なります。shop、vault の配置品、モンスターの持ち物は減らしません。
  - 例外（必ず置く）: 長く効く強さになるもの。?enchant armour、?enchant weapon、
    ?brand weapon、?acquirement、!experience。
- **経験値で充填されるもの**: 雑貨（evocable）の充填、ドラコニアンとドラゴン変身の
  ブレス、cacophony、bat form、watery grave は、×r を掛ける前の経験値で充填します。
  1 撃破あたりは Descent と同じで、1 ゲームの合計は約 0.68 になります。
- 信仰は減らしません。段階（★）の上がり方と贈り物の時期が Descent と同じになるように、
  ×r のままにしています。

決め方の根拠（他の短縮版の扱い、戦闘量の見積もり、試算）は、実験ブランチ
`claude/rapid-descent-loot-experiments` の `docs/rapid_descent_targets.md` と
`docs/rapid_descent_loot_experiments.md` にあります。

## 4. 検証（objstat、各 30 回生成）

計測には次のコマンドを使いました。Descent でも同じように計測できるよう、このために
objstat と mapstat を Descent / Rapid Descent の生成に対応させました。

```
crawl -descent       -objstat "D,Lair,Orc,Swamp,Shoals,Snake,Spider,Elf,Crypt,Vaults,Slime,Depths,Zot" -iters 30
crawl -rapid-descent -objstat "D,Lair,Orc,Swamp,Shoals,Snake,Spider,Elf,Crypt,Vaults,Slime,Depths,Zot" -iters 30
```

通常ルート（D、Lair、Orc、Lair 支流 1 本、Elf か Crypt、Vaults、Slime、Depths、Zot）の
合計で、Descent に対する比を示します。

生成されたモンスターの経験値（倒しきった場合）:

| 補填なし | 一律 ×2 | ×N/M | 採用モデル |
|---|---|---|---|
| 0.67 | 1.34 | 1.16 | **1.00**（2 回の計測で 0.99 と 1.00） |

- 採用モデルでも、branch ごとの比は 0.85（Elf、Orc）〜1.18（Depths）とばらつきます。
  Depths は 2 回の計測で 1.03 と 1.18 で、計測誤差の範囲です。
- 残存階の 1 階ごとの経験値は Descent の同じ階とほぼ一致しました。生成が Descent と
  同一であることの確認にもなっています。

アイテム数（Descent に対する比。左が採用値、右が ×1.25 を外した場合）:

| 種類 | 採用（×r ×1.25） | ×r のみ |
|---|---|---|
| 巻物 | 1.03 | 0.88 |
| 薬 | 1.06 | 0.88 |
| ワンド | 1.13 | 0.94 |
| 装身具 | 0.87 | 0.82 |
| 杖 | 0.91 | 0.99 |
| 雑貨（evocable） | 1.03 | 0.87 |
| 魔法書 | 1.02 | 0.90 |
| タリスマン | 1.04 | 0.86 |
| 技能書 | 0.61 | 0.79 |
| 金 | 0.95 | 0.88 |
| 武器・防具・矢弾 | 0.70〜0.75 | 0.64〜0.71 |

- 巻物・薬など床に落ちる消耗品は、×1.25 でほぼ Descent 並みになりました。
- 装身具・杖・技能書は vault に置かれることが多い品で、床アイテムを増やしても
  あまり増えません（技能書は 1 ゲームに 3〜5 冊と少なく、比のぶれも大きいです）。
- 武器・防具・矢弾は大半がモンスターの所持品で、倒すモンスターの数に比例して減ります
  （主にゴミ装備です）。

## 5. 調整用のつまみ

- 残す階の表: `rapid-descent.cc` の `_kept_floors()`。
- 床アイテムの上乗せ率: `rapid-descent.h` の `RAPID_DESCENT_ITEM_BONUS`（%）。
- 飛ばした階 1 つあたりの良品: `RAPID_DESCENT_GOOD_ITEMS`（%、50 で 0.5 個）。
- 床の消耗品を残す確率: `RAPID_DESCENT_CONSUMABLE_KEEP`（%）。50 で約 0.7、
  57 で約 0.75、64 で約 0.8（Descent 比、机上の試算）。
- より短くする案（約 47%）: Lair、Vaults、Slime、Zot を 3 階から 2 階（例 3, 5）に
  します。倍率は表から自動で決まります（r = 3）。

## 6. 既知の差・今後の検討

- **ユニークとの遭遇が減ります**（数で 0.56）。ユニークは階ごとに置かれるためです。
- **技能書と魔法書は少し少なめです**（0.9 前後と 0.85 前後）。不足分は主に shop の
  品で、shop の抽選を増やしても戻りきりません。
- **Abyss・Pan の門は約半分です。** Descent が Depths:3 に必ず置く Abyss の門は、
  Depths:2 が受け持つものの、Depths:2 には Pan の門が必ずあるので置かれません。
  通常ルートには関係しません。
- **Yredelemnul の torch** は 1 階に 1 回しか使えないので、使える回数は 25/45 に
  減ります。そのままにしています。
- **Descent の既存仕様として、D の最下層（D:12）に Depths の入口が出ます。**
  depths_entry vault が `D:$` に置かれるためで、入るときに警告が出ます。Rapid Descent
  でも同じです。
- **実プレイでの所要時間はまだ計測していません。** 階数と回る面積は約 0.56 ですが、
  危ない戦闘（到着時の XL 以上の強さの敵）は 0.8〜0.9 残ります。実時間が危ない戦闘で
  決まるなら、時間の比は 0.65〜0.75 程度になるかもしれません（推定）。
