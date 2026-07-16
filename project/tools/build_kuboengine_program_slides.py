from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import landscape, A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "pdf"
PDF_PATH = OUT_DIR / "KuboEngine_program_explanation_slides.pdf"

PAGE_W, PAGE_H = landscape(A4)
MARGIN_X = 38
MARGIN_TOP = 34
MARGIN_BOTTOM = 28

NAVY = colors.HexColor("#15324A")
BLUE = colors.HexColor("#27678A")
GREEN = colors.HexColor("#39735D")
ORANGE = colors.HexColor("#B46A32")
INK = colors.HexColor("#1C252B")
GRAY = colors.HexColor("#66717A")
LIGHT_BLUE = colors.HexColor("#EAF2F6")
LIGHT_GREEN = colors.HexColor("#EAF3EE")
LIGHT_ORANGE = colors.HexColor("#F8EFE7")
LIGHT_GRAY = colors.HexColor("#F3F5F6")
WHITE = colors.white
BLACK = colors.black

FONT_REG = "NotoSansJP"
FONT_BOLD = "NotoSansJP-Bold"


def register_fonts():
    candidates = [
        Path(r"C:\Windows\Fonts\NotoSansJP-VF.ttf"),
        Path(r"C:\Users\k023g\AppData\Local\Microsoft\Windows\Fonts\NotoSansJP-VariableFont_wght.ttf"),
        Path(r"C:\Windows\Fonts\meiryo.ttc"),
    ]
    font_path = next((p for p in candidates if p.exists()), None)
    if font_path is None:
        raise FileNotFoundError("Japanese font was not found.")
    pdfmetrics.registerFont(TTFont(FONT_REG, str(font_path)))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, str(font_path)))


def text_width(text, size, font=FONT_REG):
    return pdfmetrics.stringWidth(text, font, size)


def wrap_text(text, width, size, font=FONT_REG):
    lines = []
    current = ""
    for raw in text.split("\n"):
        if raw == "":
            lines.append("")
            current = ""
            continue
        for ch in raw:
            trial = current + ch
            if text_width(trial, size, font) <= width or not current:
                current = trial
            else:
                lines.append(current)
                current = ch
        if current:
            lines.append(current)
            current = ""
    return lines


def draw_text(c, text, x, y, size=12, color=INK, font=FONT_REG, max_width=None, leading=None):
    c.setFont(font, size)
    c.setFillColor(color)
    leading = leading or size * 1.45
    if max_width is None:
        c.drawString(x, y, text)
        return y - leading
    for line in wrap_text(text, max_width, size, font):
        c.drawString(x, y, line)
        y -= leading
    return y


def draw_centered(c, text, x, y, w, size=12, color=INK, font=FONT_REG):
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawCentredString(x + w / 2, y, text)


def rect(c, x, y, w, h, fill=WHITE, stroke=BLACK, radius=0, stroke_width=1):
    c.setStrokeColor(stroke)
    c.setFillColor(fill)
    c.setLineWidth(stroke_width)
    if radius:
        c.roundRect(x, y, w, h, radius, stroke=1, fill=1)
    else:
        c.rect(x, y, w, h, stroke=1, fill=1)


def title(c, number, heading, subtitle=None, accent=BLUE):
    c.setFillColor(accent)
    c.rect(0, PAGE_H - 10, PAGE_W, 10, stroke=0, fill=1)
    draw_text(c, f"{number:02d}", MARGIN_X, PAGE_H - 46, 12, accent, FONT_BOLD)
    draw_text(c, heading, MARGIN_X + 34, PAGE_H - 49, 23, NAVY, FONT_BOLD)
    if subtitle:
        draw_text(c, subtitle, MARGIN_X + 34, PAGE_H - 72, 9.5, GRAY, FONT_REG, PAGE_W - MARGIN_X * 2 - 34)
    c.setStrokeColor(colors.HexColor("#D9DEE2"))
    c.setLineWidth(0.8)
    c.line(MARGIN_X, PAGE_H - 82, PAGE_W - MARGIN_X, PAGE_H - 82)


def footer(c, page_no):
    c.setFont(FONT_REG, 7.5)
    c.setFillColor(GRAY)
    c.drawString(MARGIN_X, 16, "KuboEngine / DirectXGame")
    c.drawRightString(PAGE_W - MARGIN_X, 16, str(page_no))


def bullet(c, text, x, y, w, size=11, color=INK):
    c.setFillColor(color)
    c.circle(x + 4, y + 3, 2.2, stroke=0, fill=1)
    return draw_text(c, text, x + 14, y, size, color, FONT_REG, w - 14, size * 1.48)


def callout(c, label, text, x, y, w, h, fill, accent):
    rect(c, x, y, w, h, fill=fill, stroke=BLACK, radius=6, stroke_width=0.9)
    c.setFillColor(accent)
    c.roundRect(x, y + h - 25, w, 25, 6, stroke=0, fill=1)
    draw_centered(c, label, x, y + h - 18, w, 10.5, WHITE, FONT_BOLD)
    draw_text(c, text, x + 12, y + h - 43, 9.2, INK, FONT_REG, w - 24, 13.2)


def table(c, x, y_top, col_widths, headers, rows, row_h=31, header_fill=LIGHT_BLUE, font_size=8.6):
    total_w = sum(col_widths)
    h = row_h * (len(rows) + 1)
    y = y_top - h
    rect(c, x, y, total_w, h, fill=WHITE, stroke=BLACK, stroke_width=0.9)
    cx = x
    for i, width in enumerate(col_widths):
        c.setFillColor(header_fill)
        c.rect(cx, y_top - row_h, width, row_h, stroke=0, fill=1)
        if i:
            c.setStrokeColor(BLACK)
            c.setLineWidth(0.7)
            c.line(cx, y, cx, y_top)
        draw_text(c, headers[i], cx + 7, y_top - 20, font_size, NAVY, FONT_BOLD, width - 14, font_size * 1.15)
        cx += width
    c.setStrokeColor(BLACK)
    c.setLineWidth(0.7)
    for r in range(len(rows) + 1):
        yy = y_top - row_h * r
        c.line(x, yy, x + total_w, yy)
    for r, row in enumerate(rows):
        cx = x
        yy = y_top - row_h * (r + 1) - 20
        for i, cell in enumerate(row):
            draw_text(c, str(cell), cx + 7, yy, font_size, INK, FONT_REG, col_widths[i] - 14, font_size * 1.18)
            cx += col_widths[i]
    return y - 10


def flow(c, labels, x, y, w, h, fill=LIGHT_BLUE, accent=BLUE):
    gap = 9
    box_w = (w - gap * (len(labels) - 1)) / len(labels)
    for i, label in enumerate(labels):
        bx = x + i * (box_w + gap)
        rect(c, bx, y, box_w, h, fill=fill if i % 2 == 0 else WHITE, stroke=BLACK, radius=5, stroke_width=0.9)
        lines = wrap_text(label, box_w - 16, 9.5, FONT_BOLD)
        ty = y + h / 2 + (len(lines) - 1) * 6
        for line in lines:
            draw_centered(c, line, bx, ty, box_w, 9.5, accent, FONT_BOLD)
            ty -= 12
        if i < len(labels) - 1:
            c.setStrokeColor(accent)
            c.setLineWidth(1.5)
            c.line(bx + box_w + 2, y + h / 2, bx + box_w + gap - 3, y + h / 2)
            c.setFillColor(accent)
            c.line(bx + box_w + gap - 3, y + h / 2, bx + box_w + gap - 8, y + h / 2 + 4)
            c.line(bx + box_w + gap - 3, y + h / 2, bx + box_w + gap - 8, y + h / 2 - 4)


def codebox(c, text, x, y, w, h, label=None):
    rect(c, x, y, w, h, fill=colors.HexColor("#FAFBFC"), stroke=BLACK, radius=4, stroke_width=0.8)
    if label:
        c.setFillColor(NAVY)
        c.roundRect(x, y + h - 23, w, 23, 4, stroke=0, fill=1)
        draw_text(c, label, x + 10, y + h - 16, 8.5, WHITE, FONT_BOLD)
        y_start = y + h - 39
    else:
        y_start = y + h - 16
    draw_text(c, text, x + 10, y_start, 8.2, INK, FONT_REG, w - 20, 11.5)


def slide_1(c):
    title(c, 1, "KuboEngine / DirectXGame", "C++ / DirectX 12 自作エンジン上で制作した3Dサバイバルアクション", ORANGE)
    draw_text(c, "敵を倒して経験値を集め、レベルアップで武器や能力を選びながらボス戦まで生き残るゲームです。Title / Play / Resultを実装し、獲得コインを恒久強化やキャラクター解放へ引き継ぎます。", 58, 444, 15, INK, FONT_REG, 720, 23)
    flow(c, ["C++20", "DirectX 12", "HLSL", "Dear ImGui", "CSV", "Assimp"], 60, 350, 720, 52, LIGHT_BLUE, BLUE)
    table(c, 72, 300, [130, 540], ["範囲", "実装内容"], [
        ("エンジン", "描画、入力、音声、モデル、スプライト、パーティクル、カメラ、シーン管理"),
        ("ゲーム", "Player、Enemy、Weapon、HUD、GameSession、レベルアップ、ボス戦"),
        ("検証", "CPU回帰テスト、シーン遷移ストレス、SRVテレメトリ、衝突CPU計測"),
    ], row_h=38, header_fill=LIGHT_ORANGE, font_size=9.2)


def slide_2(c):
    title(c, 2, "エンジン層とゲーム層を分ける", "ゲーム固有の変更を共通基盤へ混ぜないための境界", BLUE)
    y = bullet(c, "共通機能は engine/、作品固有の処理は game/directxgame/ に配置。", 62, 455, 730, 11.5)
    y = bullet(c, "main.cpp は起動と例外処理に絞り、ゲーム固有のシーン生成は GameSceneFactory に渡す。", 62, y - 8, 730, 11.5)
    bullet(c, "結果として、エンジン側は Title / Play / Result を直接知らず、ゲーム側で構成を差し替えられる。", 62, y - 8, 730, 11.5)
    flow(c, ["main.cpp", "Game", "GameSceneFactory", "Title / Play / Result"], 80, 298, 680, 65, LIGHT_BLUE, BLUE)
    callout(c, "なぜ分けたか", "ゲーム固有の変更が描画・入力・音声などの共通処理へ混ざると、別作品へ使い回すときに切り離しにくくなるためです。", 92, 175, 300, 82, LIGHT_BLUE, BLUE)
    callout(c, "どうなったか", "起動処理、共通基盤、作品固有のシーン生成の責務が分かれ、変更時に見る範囲を限定しやすくなりました。", 450, 175, 300, 82, LIGHT_GREEN, GREEN)


def slide_3(c):
    title(c, 3, "PlaySceneは構成の起点に絞る", "GameSceneFactory と GameplayFlowController の関係を図で示す", BLUE)
    codebox(c, "main.cpp\n  │ inject\n  ▼\nGame\n  │ uses\n  ▼\nGameSceneFactory\n  ├─ TitleScene\n  ├─ PlayScene\n  │    ├─ GameplayFlowController\n  │    ├─ PlayerManager\n  │    ├─ PlayerWeaponController\n  │    ├─ EnemyManager\n  │    └─ GameplayHudPresentation\n  └─ ResultScene\n\nGameSession は Title / Play / Result をまたぐ値を保持", 55, 135, 340, 330, "簡略クラス図")
    table(c, 430, 455, [170, 230], ["クラス", "主な役割"], [
        ("GameSceneFactory", "Title、Play、Resultを生成"),
        ("PlayScene", "各機能を保持して接続"),
        ("GameplayFlowController", "Start、Playing、Boss、Pause、LevelUp、Deadを管理"),
        ("PlayerWeaponController", "武器更新と強化処理を集約"),
        ("EnemyManager", "敵生成、EXP、衝突、ボス進行"),
        ("GameSession", "結果、コイン、恒久強化を保持"),
    ], row_h=43, header_fill=LIGHT_BLUE, font_size=8.4)
    draw_text(c, "PlaySceneへ処理を追加し続けると、1機能の変更でもシーン全体を確認する必要があります。そこで、PlaySceneは接続役に寄せ、処理は変更理由ごとのクラスへ分けました。", 430, 130, 10.2, INK, FONT_REG, 350, 15)


def slide_4(c):
    title(c, 4, "ゲーム進行とシーン間データ", "通常更新・ポーズ・レベルアップ・死亡処理を混ぜない", GREEN)
    flow(c, ["Play\nRunResult保存", "Result\n集計・表示", "Title\n強化・解放", "Play\n次のラン"], 75, 375, 690, 76, LIGHT_GREEN, GREEN)
    callout(c, "GameplayFlowController", "Start、Playing、BossIntro、Boss、BossDefeated、Paused、LevelUp、Deadに分け、同時に動いてはいけない処理を状態で止めます。", 70, 230, 330, 95, LIGHT_GREEN, GREEN)
    callout(c, "GameSession", "経験値、コイン、最終レベル、撃破数、経過フレームをRunResultとして保持し、シーンをまたぐ値の置き場所を限定します。", 445, 230, 330, 95, LIGHT_BLUE, BLUE)
    draw_text(c, "この構成にした理由は、各シーンが互いの内部状態を直接参照し始めると、シーン遷移のたびに依存関係が増えるためです。", 80, 155, 11, INK, FONT_REG, 680, 16)


def slide_5(c):
    title(c, 5, "DirectX 12のフレーム・リソース管理", "CPUとGPUの参照タイミングのずれをFenceで扱う", GREEN)
    codebox(c, "時間 ─────────────────────────────→\n\nCPU  Frame 0を記録      Frame 1を記録      Frame 0を再利用\n     Allocator 0        Allocator 1        Fence 0完了後\n     Upload Arena 0     Upload Arena 1\n          │ Fence 0         │ Fence 1\n          ▼                 ▼\nGPU       Frame 0を実行      Frame 1を実行", 60, 305, 720, 150, "CPU / GPU タイムライン")
    table(c, 72, 255, [170, 500], ["管理対象", "実装"], [
        ("フレーム寿命", "FrameContext / CommandAllocator / Upload Arena / deferred release / Fence"),
        ("SRV寿命", "retired descriptorをFence完了後に再利用"),
        ("状態遷移", "DirectXCommonで現在状態を追跡し、ResourceBarrierを発行"),
        ("追加バリア", "UAV barrier / aliasing barrierを共通処理として用意"),
    ], row_h=32, header_fill=LIGHT_GREEN, font_size=8.5)
    draw_text(c, "資料では「別々の契約」という表現は使わず、「いつ再利用できるか」と「今どの状態で使えるか」を分けて追跡した、と説明します。", 78, 58, 9.8, ORANGE, FONT_BOLD, 690, 14)


def slide_6(c):
    title(c, 6, "Shadow Mapとオフスクリーン描画", "実装済みの描画経路と、次にPIXで見る仮説", GREEN)
    flow(c, ["PIXEL_SHADER_RESOURCE", "DEPTH_WRITE\nShadow Pass", "PIXEL_SHADER_RESOURCE", "通常描画で参照"], 60, 390, 720, 70, LIGHT_ORANGE, ORANGE)
    y = bullet(c, "ライト視点の深度マップを作成し、Pixel Shaderで SampleCmpLevelZero による比較を行う。", 70, 328, 720, 10.5)
    y = bullet(c, "3×3比較サンプリングで影の境界を調整。ライト範囲外のcasterは除外し、候補数・描画数・除外数を記録。", 70, y - 5, 720, 10.5)
    bullet(c, "オフスクリーン描画では RENDER_TARGET → PIXEL_SHADER_RESOURCE の状態遷移を行い、最終出力でポストエフェクトを適用。", 70, y - 5, 720, 10.5)
    callout(c, "PIXで確認する仮説", "Shadow PassのGPU時間だけでなく、ResourceBarrierの位置、Draw Call数、通常描画側のシャドウサンプリングもボトルネック候補として見る。", 95, 120, 650, 90, LIGHT_GREEN, GREEN)


def slide_7(c):
    title(c, 7, "OBBと固定グリッドによる衝突判定", "回転を反映した判定を保ちつつ、詳細判定の候補を減らす", ORANGE)
    codebox(c, "上から見たXZ平面\n\n┌────┬────┬────┬────┐\n│    │ E  │    │    │\n├────┼────┼────┼────┤\n│    │ ●──┼─── │    │  ●: 弾またはプレイヤー\n├────┼────┼────┼────┤  E: 候補になる敵\n│    │ E  │ E  │    │\n└────┴────┴────┴────┘\n\n周辺セルの敵だけを候補にし、その後でOBBの詳細判定を行う。", 55, 155, 340, 310, "固定グリッドの概念図")
    y = bullet(c, "AABBだけでは、回転したモデルの見た目より判定が広がるため、詳細判定にはOBBを使用。", 430, 430, 345, 10.5)
    y = bullet(c, "全敵へ毎回OBB判定を行うと、敵数と弾数に応じて候補数が増える。", 430, y - 5, 345, 10.5)
    y = bullet(c, "本作は主にXZ平面で敵と弾が毎フレーム動くため、登録と周辺検索が単純な固定グリッドを採用。", 430, y - 5, 345, 10.5)
    table(c, 430, y - 8, [105, 130, 130], ["方式", "特徴", "今回の判断"], [
        ("総当たり", "単純", "比較用"),
        ("固定グリッド", "再登録と周辺検索が単純", "採用"),
        ("四分木", "密度差に強い", "分割管理が増える"),
        ("BVH", "静的形状に向く", "毎フレーム更新が必要"),
    ], row_h=33, header_fill=LIGHT_ORANGE, font_size=7.6)


def slide_8(c):
    title(c, 8, "空間分割の実測結果", "総当たりと固定グリッドを同条件で切り替えて比較", ORANGE)
    table(c, 62, 450, [90, 150, 165, 120], ["敵数", "総当たり 中央値", "固定グリッド 中央値", "CPU時間削減"], [
        ("27体", "0.1056 ms", "0.0542 ms", "48.7%"),
        ("52体", "0.3645 ms", "0.0888 ms", "75.6%"),
        ("84体", "0.9304 ms", "0.1654 ms", "82.2%"),
    ], row_h=43, header_fill=LIGHT_ORANGE, font_size=9.5)
    table(c, 610, 450, [62, 62, 62], ["総当たり", "グリッド", "削減"], [
        ("7140件", "835件", "88.3%"),
    ], row_h=48, header_fill=LIGHT_BLUE, font_size=7.8)
    draw_text(c, "84体時の候補数中央値", 612, 332, 8.5, GRAY, FONT_REG, 180, 12)
    callout(c, "計測環境", "CPU: 11th Gen Intel Core i7-11800H（8C/16T） / GPU: NVIDIA GeForce RTX 3060 Laptop GPU / Release x64 / 固定シード20260708 / 各1200フレーム", 70, 205, 700, 78, LIGHT_GRAY, BLUE)
    draw_text(c, "この数値は、空間マップ構築・衝突判定・敵同士の押し戻しを合計したCPU時間です。FPSやGPU時間の改善値ではありません。", 78, 145, 10.5, ORANGE, FONT_BOLD, 690, 15)
    draw_text(c, "敵数が増えるほど総当たりとの差が広がったため、広いXZ平面に敵が分散する本作では固定グリッドの効果を確認できました。", 78, 105, 10.5, INK, FONT_REG, 690, 15)


def slide_9(c):
    title(c, 9, "CSVとデバッグ表示で調整を早くする", "C++再ビルドなしで値を変え、実行中の状態を確認する", BLUE)
    codebox(c, "# weaponUpgradeSettings.csv の掲載例\nweapon,level,damage,interval,bulletCount,range\nNormal,1,10,0.45,1,0\nNormal,2,14,0.40,1,0\nOrbit,1,6,0.00,2,3.2\nLightning,1,20,1.20,1,5.0", 55, 230, 360, 205, "CSVサンプル")
    y = bullet(c, "プレイヤー能力、武器強化、敵設定、スポーン、UI配置、デバッグ調整値をCSVへ分離。", 450, 420, 320, 10.5)
    y = bullet(c, "数値変換時は、文字列全体の消費、有限値、範囲を確認し、不正値を早い段階で止める。", 450, y - 5, 320, 10.5)
    y = bullet(c, "ImGuiでは敵数、弾数、FPS、SRV使用数、Shadow Pass統計、衝突形状、武器レベルを確認。", 450, y - 5, 320, 10.5)
    callout(c, "言い換え", "「調整と検証を実装の一部にする」ではなく、「CSVとデバッグ表示で、調整と原因確認を早くする」と書く。", 450, 145, 320, 85, LIGHT_BLUE, BLUE)


def slide_10(c):
    title(c, 10, "検証", "壊れやすい処理を同じ条件で再確認できるようにする", GREEN)
    table(c, 60, 440, [150, 410, 150], ["確認方法", "対象", "結果・用途"], [
        ("CPU回帰テスト", "セルキー、候補削減率、HP、CSV、武器間隔、乱数", "Debug / Release PASS"),
        ("Scene Stress", "Title → Play → Resultを3周", "各シーン3回訪問"),
        ("SRV telemetry", "使用数 / High-watermark", "105 / 105"),
        ("衝突計測", "Spatial Grid / Brute Force切り替え", "CPU時間と候補数をCSV化"),
    ], row_h=46, header_fill=LIGHT_GREEN, font_size=8.6)
    draw_text(c, "検証で伝えたいことは、単に「テストを作った」ことではありません。仕様変更や調整をした後でも、CSVの読み込み、HPの範囲制限、乱数シード、シーン遷移など、壊れやすい部分を同じ条件で再確認できるようにした点です。", 75, 140, 11, INK, FONT_REG, 690, 16)


def slide_11(c):
    title(c, 11, "制作中に見直した点", "リファクタリング、負荷対策、検証追加を分けて説明する", NAVY)
    draw_text(c, "最初は機能追加を優先していましたが、PlaySceneやPlayerに処理が集まり始めると、1つの修正で確認する範囲が広がりました。そこで、変更理由ごとにクラスを分け、同時に負荷が増えやすい衝突判定と再発確認が必要な処理を整理しました。", 60, 455, 11, INK, FONT_REG, 720, 16)
    table(c, 60, 365, [155, 155, 160], ["リファクタリング", "対応", "結果"], [
        ("シーン生成を固定したくない", "GameSceneFactory", "ゲーム側で差し替え"),
        ("進行処理がPlaySceneへ集中", "GameplayFlowController", "状態遷移を一か所で追跡"),
        ("結果受け渡しが複雑", "GameSession", "各シーンの役割を限定"),
        ("武器処理がPlayerへ増える", "PlayerWeaponController", "武器処理を集約"),
    ], row_h=36, header_fill=LIGHT_BLUE, font_size=7.8)
    table(c, 540, 365, [115, 115, 115], ["分類", "対応", "結果"], [
        ("負荷対策", "固定グリッド + OBB", "近傍候補へ限定"),
        ("描画管理", "FrameContext / Fence", "再利用条件を明示"),
        ("検証追加", "CPUテスト / Stress", "同じ条件で再確認"),
    ], row_h=43, header_fill=LIGHT_GREEN, font_size=7.6)


def slide_12(c):
    title(c, 12, "今後の改善", "自分用メモではなく、次に何を確認するかを書く", ORANGE)
    callout(c, "PIX計測", "Shadow Pass有効／無効、caster数、Draw Call数、ResourceBarrierの位置、GPU Timestampを同じ条件で比較する。", 62, 365, 335, 86, LIGHT_ORANGE, ORANGE)
    callout(c, "実プレイ再生ベンチ", "弾数や敵配置を保存し、同じ入力を再生する形に近づける。単体処理だけでなく実ゲームに近い条件で見る。", 445, 365, 335, 86, LIGHT_GREEN, GREEN)
    callout(c, "リソース管理", "起動後に追加されたモデルの索引更新、非同期ロード、ID取り違えを防ぐ型付きハンドルを検討する。", 62, 235, 335, 86, LIGHT_BLUE, BLUE)
    callout(c, "テスト拡張", "CSVスキーマとゲーム進行をGPUなしで確認できるfixtureを増やし、調整やリファクタリング後の破損を早く拾う。", 445, 235, 335, 86, LIGHT_GRAY, NAVY)
    draw_text(c, "制約: Windows / DirectX 12専用。Shadow MapのGPU時間、最終的なFPS維持への寄与は今後の定量確認項目です。", 80, 125, 11, ORANGE, FONT_BOLD, 690, 16)


SLIDES = [
    slide_1,
    slide_2,
    slide_3,
    slide_4,
    slide_5,
    slide_6,
    slide_7,
    slide_8,
    slide_9,
    slide_10,
    slide_11,
    slide_12,
]


def build():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    register_fonts()
    c = canvas.Canvas(str(PDF_PATH), pagesize=(PAGE_W, PAGE_H))
    c.setTitle("KuboEngine プログラム説明資料 - スライド型")
    c.setAuthor("KuboEngine Developer")
    for idx, slide in enumerate(SLIDES, start=1):
        c.setFillColor(WHITE)
        c.rect(0, 0, PAGE_W, PAGE_H, stroke=0, fill=1)
        slide(c)
        footer(c, idx)
        c.showPage()
    c.save()
    print(PDF_PATH)


if __name__ == "__main__":
    build()
