from __future__ import annotations

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "pdf"
OUT_PDF = OUT_DIR / "program_explanation_A4_revised_2026.pdf"

PAGE_W, PAGE_H = landscape(A4)
MX = 38
MY_TOP = PAGE_H - 34
MY_BOTTOM = 32
CW = PAGE_W - MX * 2

FONT_REG = "BizUDGothicR"
FONT_BOLD = "BizUDGothicB"
FONT_MONO = "Courier"

INK = colors.HexColor("#111827")
MUTE = colors.HexColor("#475569")
LINE = colors.HexColor("#CBD5E1")
PAPER = colors.HexColor("#F8FAFC")
NAVY = colors.HexColor("#0B1220")
BLUE = colors.HexColor("#2563EB")
TEAL = colors.HexColor("#0F766E")
GREEN = colors.HexColor("#16A34A")
ORANGE = colors.HexColor("#EA580C")
RED = colors.HexColor("#DC2626")
PURPLE = colors.HexColor("#7C3AED")


def register_fonts() -> None:
    pdfmetrics.registerFont(TTFont(FONT_REG, r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, r"C:\Windows\Fonts\BIZ-UDGothicB.ttc"))


def sw(text: str, size: float, font: str = FONT_REG) -> float:
    return pdfmetrics.stringWidth(text, font, size)


def wrap(text: str, width: float, size: float, font: str = FONT_REG) -> list[str]:
    out: list[str] = []
    for raw in str(text).splitlines():
        line = ""
        for ch in raw.strip():
            trial = line + ch
            if not line or sw(trial, size, font) <= width:
                line = trial
            else:
                out.append(line)
                line = ch
        out.append(line)
    return out or [""]


def draw_text(c: canvas.Canvas, x: float, y: float, text: str, size: float = 9, color=INK, font: str = FONT_REG) -> None:
    c.setFillColor(color)
    c.setFont(font, size)
    c.drawString(x, y, text)


def para(c: canvas.Canvas, x: float, y: float, text: str, width: float, size: float = 8.5, leading: float = 12, color=INK, font: str = FONT_REG) -> float:
    for line in wrap(text, width, size, font):
        draw_text(c, x, y, line, size, color, font)
        y -= leading
    return y


def footer(c: canvas.Canvas, page: int) -> None:
    c.setStrokeColor(LINE)
    c.line(MX, 24, PAGE_W - MX, 24)
    draw_text(c, MX, 11, "KuboEngine / DirectXGame - revised technical explanation", 6.8, MUTE)
    c.setFillColor(MUTE)
    c.setFont(FONT_REG, 6.8)
    c.drawRightString(PAGE_W - MX, 11, f"{page:02d}")


def heading(c: canvas.Canvas, page_no: int, title: str, lead: str) -> float:
    draw_text(c, MX, MY_TOP, f"{page_no:02d}", 8, BLUE, FONT_BOLD)
    draw_text(c, MX + 32, MY_TOP, title, 18, NAVY, FONT_BOLD)
    y = para(c, MX + 32, MY_TOP - 22, lead, CW - 32, 8.7, 11.5, MUTE)
    c.setStrokeColor(LINE)
    c.line(MX, y - 5, PAGE_W - MX, y - 5)
    return y - 24


def box(c: canvas.Canvas, x: float, y: float, w: float, h: float, title: str, body: str = "", accent=BLUE, fill=colors.white, body_size: float = 8.0) -> None:
    c.setFillColor(fill)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 5, stroke=1, fill=1)
    c.setFillColor(accent)
    c.rect(x, y - 4, w, 4, stroke=0, fill=1)
    draw_text(c, x + 10, y - 20, title, 9.5, INK, FONT_BOLD)
    if body:
        para(c, x + 10, y - 36, body, w - 20, body_size, body_size + 3.5)


def callout(c: canvas.Canvas, x: float, y: float, w: float, text: str, accent=ORANGE) -> float:
    lines = wrap(text, w - 22, 8.1)
    h = 22 + len(lines) * 11
    c.setFillColor(colors.HexColor("#FFF7ED"))
    c.setStrokeColor(accent)
    c.roundRect(x, y - h, w, h, 5, stroke=1, fill=1)
    draw_text(c, x + 10, y - 17, "判断 / 注意", 8.2, accent, FONT_BOLD)
    yy = y - 32
    for line in lines:
        draw_text(c, x + 10, yy, line, 8.1, INK)
        yy -= 11
    return y - h


def bullets(c: canvas.Canvas, x: float, y: float, items: list[str], w: float, color=BLUE, size: float = 8.2) -> float:
    for item in items:
        c.setFillColor(color)
        c.circle(x + 3, y + 3, 2, stroke=0, fill=1)
        for line in wrap(item, w - 16, size):
            draw_text(c, x + 14, y, line, size, INK)
            y -= size + 3.7
        y -= 2
    return y


def code(c: canvas.Canvas, x: float, y: float, w: float, lines: list[str], size: float = 7.4) -> float:
    max_chars = max(34, int((w - 24) / sw("M", size, FONT_MONO)))
    wrapped: list[str] = []
    for raw in lines:
        if raw == "":
            wrapped.append("")
            continue
        text = raw
        while len(text) > max_chars:
            wrapped.append(text[:max_chars])
            text = "  " + text[max_chars:]
        wrapped.append(text)
    h = 18 + len(wrapped) * (size + 3.5)
    c.setFillColor(colors.HexColor("#0F172A"))
    c.roundRect(x, y - h, w, h, 5, stroke=0, fill=1)
    yy = y - 13
    for line in wrapped:
        draw_text(c, x + 10, yy, line, size, colors.HexColor("#F8FAFC"), FONT_MONO)
        yy -= size + 3.5
    return y - h


def node(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str, fill) -> None:
    c.setFillColor(fill)
    c.roundRect(x, y - h, w, h, 5, stroke=0, fill=1)
    lines = wrap(label, w - 12, 7.6, FONT_BOLD)
    yy = y - h / 2 + (len(lines) - 1) * 4.4
    for line in lines:
        c.setFillColor(colors.white)
        c.setFont(FONT_BOLD, 7.6)
        c.drawCentredString(x + w / 2, yy, line)
        yy -= 9.4


def arrow(c: canvas.Canvas, x1: float, y1: float, x2: float, y2: float) -> None:
    c.setStrokeColor(LINE)
    c.line(x1, y1, x2, y2)
    dx = 1 if x2 >= x1 else -1
    c.line(x2, y2, x2 - 5 * dx, y2 + 3)
    c.line(x2, y2, x2 - 5 * dx, y2 - 3)


def evidence_placeholder(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str) -> None:
    c.setFillColor(PAPER)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 5, stroke=1, fill=1)
    c.setStrokeColor(colors.HexColor("#94A3B8"))
    c.setDash(4, 3)
    c.rect(x + 8, y - h + 8, w - 16, h - 16, stroke=1, fill=0)
    c.setDash()
    c.setFillColor(MUTE)
    c.setFont(FONT_BOLD, 8.2)
    c.drawCentredString(x + w / 2, y - h / 2 + 4, label)
    c.setFont(FONT_REG, 7.2)
    c.drawCentredString(x + w / 2, y - h / 2 - 10, "提出前に実ゲーム画面へ差し替え")


def page01(c: canvas.Canvas) -> None:
    y = heading(c, 1, "作品概要 / 何を作ったか", "DirectX 12 と C++20 で制作した Windows 向けゲームエンジンと、3Dサバイバルシューティング。概要羅列ではなく、実装証拠を中心に説明する。")
    evidence_placeholder(c, MX, y, 360, 215, "Gameplay screenshot")
    x = MX + 382
    draw_text(c, x, y, "KuboEngine / DirectXGame", 17, NAVY, FONT_BOLD)
    para(c, x, y - 28, "`engine/` に描画・入力・音声・リソース・scene管理を置き、`game/directxgame/` にタイトル、ゲームプレイ、リザルト、敵、武器、HUD、演出、debug、CSV調整を分離しました。", CW - 382, 8.7, 12)
    yy = y - 94
    w = (CW - 382 - 12) / 2
    box(c, x, yy, w, 70, "深掘り 1", "敵管理 / OBB衝突 / 空間分割 / Telemetry", BLUE, colors.white)
    box(c, x + w + 12, yy, w, 70, "深掘り 2", "CSVデータ駆動 / 選定理由 / strict parse", GREEN, colors.white)
    box(c, x, yy - 88, w, 70, "深掘り 3", "DirectX 12 FrameContext / SRV管理", ORANGE, colors.white)
    box(c, x + w + 12, yy - 88, w, 70, "検証", "CPU regression / scene stress / PIX preflight", PURPLE, colors.white)
    callout(c, MX, y - 240, CW, "FPSやGPU時間は未計測のため、資料では性能改善値として主張しない。現時点で出せる数値は、scene stress と soft-cap telemetry に限定する。")
    footer(c, 1)


def page02(c: canvas.Canvas) -> None:
    y = heading(c, 2, "担当範囲 / 技術スタック", "個人制作として、ゲームロジックだけでなくDX12描画基盤、scene遷移、resource管理、debug UI、CSV調整、CPU test、stress runnerまで担当。")
    col = (CW - 24) / 3
    box(c, MX, y, col, 175, "Engine", "`Framework`\n`DirectXCommon`\n`SrvManager`\ninput / audio / camera\nparticle / model / texture", BLUE, colors.white)
    box(c, MX + col + 12, y, col, 175, "Game", "`TitleScene`\n`PlayScene`\n`ResultScene`\nplayer / enemy / weapon\nHUD / effects / debug", GREEN, colors.white)
    box(c, MX + (col + 12) * 2, y, col, 175, "Data / Tools", "`Resources/DirectXGame/data/*.csv`\nCPU regression checks\nscene transition stress\nPIX preflight", ORANGE, colors.white)
    y -= 205
    draw_text(c, MX, y, "使用技術", 13, NAVY, FONT_BOLD)
    bullets(c, MX, y - 24, [
        "C++20 / MSVC / Visual Studio solution / Windows 10-11 x64",
        "DirectX 12 / HLSL / DirectXTex / Assimp",
        "Dear ImGui / ImGuizmo / ImPlot / imgui-node-editor / FontAwesome icon font",
        "設計対象: rendering, frame resources, descriptors, gameplay flow, CSV tuning, debug telemetry",
    ], CW, BLUE)
    footer(c, 2)


def page03(c: canvas.Canvas) -> None:
    y = heading(c, 3, "全体アーキテクチャ", "`main.cpp` は起動と例外処理に絞り、実行本体は `Engine::Scene::Game` へ委譲。scene生成は `GameSceneFactory` が担当する。")
    node(c, MX + 20, y, 120, 42, "WinMain / Application", NAVY)
    node(c, MX + 185, y, 140, 42, "Engine::Scene::Game", BLUE)
    node(c, MX + 370, y, 140, 42, "GameSceneFactory", TEAL)
    arrow(c, MX + 140, y - 21, MX + 185, y - 21)
    arrow(c, MX + 325, y - 21, MX + 370, y - 21)
    sy = y - 100
    for i, (name, colr) in enumerate([("TitleScene", GREEN), ("PlayScene", ORANGE), ("ResultScene", PURPLE)]):
        x = MX + 110 + i * 205
        node(c, x, sy, 130, 38, name, colr)
        arrow(c, MX + 440, y - 42, x + 65, sy)
    y -= 175
    left = (CW - 24) * 0.54
    draw_text(c, MX, y, "コード証拠: main.cpp", 11, NAVY, FONT_BOLD)
    code(c, MX, y - 18, left, [
        "auto directXGame = std::make_unique<Engine::Scene::Game>(",
        "    std::make_unique<DirectXGame::GameSceneFactory>(),",
        "    DirectXGame::SceneId::kTitle);",
        "std::unique_ptr<Engine::Base::Framework> game = std::move(directXGame);",
        "game->Run();",
    ])
    box(c, MX + left + 24, y, CW - left - 24, 130, "設計意図", "起動処理、共通Framework、ゲーム固有sceneの変更理由を分ける。新しいsceneを追加する場合も、main loop を触らず factory 側へ閉じられる。", BLUE, PAPER, 8.3)
    footer(c, 3)


def page04(c: canvas.Canvas) -> None:
    y = heading(c, 4, "現行ゲーム構成", "`PlayScene` はgameplayのcomposition root。内部処理は flow, player, enemy, HUD, effect, debug へ分割している。")
    rows = [
        ("GameplayFlowController", "Start / Playing / BossIntro / Boss / BossDefeated / Paused / LevelUp / Dead"),
        ("PlayerManager", "HP, EXP, Level, weapon upgrade の窓口"),
        ("EnemyManager", "enemy, EXP Orb, collision, boss state"),
        ("GameplayHudPresentation", "HP / EXP / timer / minimap などのHUD"),
        ("GameParticleEffects", "level-up, combat, trail などの粒子演出"),
        ("DebugContext / DebugUI::*", "freeze, debug state, editor連携"),
    ]
    x1 = MX
    x2 = MX + 258
    for i, (name, body) in enumerate(rows):
        yy = y - i * 47
        box(c, x1, yy, 236, 37, name, "", [BLUE, GREEN, ORANGE, TEAL, PURPLE, RED][i], colors.white)
        para(c, x2, yy - 15, body, CW - 258, 8.4, 11.5)
    callout(c, MX, y - 305, CW, "旧資料に残っていた古いscene/session/debug系の名称は、現行コードの `PlayScene`, `GameSession`, `GameSceneFactory`, `DebugContext / DebugUI::*` へ更新済み。", BLUE)
    footer(c, 4)


def page05(c: canvas.Canvas) -> None:
    y = heading(c, 5, "深掘り1: 敵管理と OBB 衝突", "敵、通常弾、衛星弾、ドローン弾、EXP Orb が同時に増える場面で、候補数と一時確保が問題になる。")
    evidence_placeholder(c, MX, y, 290, 170, "OBB / Collider debug screenshot")
    x = MX + 312
    bullets(c, x, y - 6, [
        "`EnemyCollisionContext` が `spatialMap`, `activeEnemies`, `nearbyEnemies` を保持。",
        "`EnemyCollisionSystem` が `RebuildContext`, `CheckCollisions`, `ApplyAreaDamage`, `ResolveEnemySeparation` を担当。",
        "OBBはnarrow phaseとして使い、見た目の回転やスケールと当たり判定のずれを減らす。",
    ], CW - 312, GREEN)
    y -= 205
    draw_text(c, MX, y, "コード証拠: GameplayRules::MakeCellKey", 11, NAVY, FONT_BOLD)
    code(c, MX, y - 18, 390, [
        "inline uint64_t MakeCellKey(int32_t cellX, int32_t cellZ)",
        "{",
        "    return (static_cast<uint64_t>(",
        "        static_cast<uint32_t>(cellX)) << 32) |",
        "        static_cast<uint32_t>(cellZ);",
        "}",
    ])
    callout(c, MX + 415, y - 18, CW - 415, "負座標のcellも扱うため、`uint32_t` 経由で `uint64_t` にpackする。負値を左shiftする形は未定義動作の危険があるため、この形にした。", ORANGE)
    footer(c, 5)


def page06(c: canvas.Canvas) -> None:
    y = heading(c, 6, "深掘り1: Telemetry と負荷監視", "`SoftCapTelemetry` は enemy数、kill数、EXP Orb数、弾数、prune数、particle数をsnapshotとして取得する。")
    values = [
        ("level", "8"), ("killCount", "209"), ("expOrbCap", "160"), ("expOrbPeak", "161"),
        ("expOrbPrunes", "31"), ("expOrbPrunesPerMinute", "10.2245"), ("normalBulletCap", "96"), ("droneBulletCap", "64"), ("particleCount", "75"),
    ]
    w = (CW - 32) / 3
    for i, (k, v) in enumerate(values):
        x = MX + (i % 3) * (w + 16)
        yy = y - (i // 3) * 54
        box(c, x, yy, w, 42, k, v, [BLUE, GREEN, ORANGE][i % 3], PAPER, 10)
    callout(c, MX, y - 185, CW, "この数値はBefore/After性能改善値ではなく、上限管理と実行中監視の証拠。空間分割の効果を示すには、総当たり版との比較benchmarkを追加する。", RED)
    evidence_placeholder(c, MX, y - 255, CW, 92, "ImGui telemetry / debug screenshot")
    footer(c, 6)


def page07(c: canvas.Canvas) -> None:
    y = heading(c, 7, "深掘り2: CSV データ駆動の技術選定", "武器レベル、敵パラメータ、UI座標のような表形式データを、コードからCSVへ分離した。")
    cols = [(BLUE, "CSV", "表形式、Excel編集、Git差分が見やすい", "採用"),
            (GREEN, "JSON", "階層構造に強い", "今回の中心データでは冗長"),
            (ORANGE, "独自バイナリ", "読み込み効率に強い", "手編集とdebugが難しい"),
            (PURPLE, "C++直書き", "型安全にしやすい", "調整ごとにビルドが必要")]
    w = (CW - 24) / 4
    for i, (colr, name, good, decision) in enumerate(cols):
        box(c, MX + i * (w + 8), y, w, 128, name, f"良い点: {good}\n判断: {decision}", colr, colors.white, 7.8)
    y -= 165
    draw_text(c, MX, y, "現行CSV", 12, NAVY, FONT_BOLD)
    bullets(c, MX, y - 24, [
        "`playerStatus.csv`, `weaponUpgradeSettings.csv`, `enemyTypes.csv`, `enemySpawnSettings.csv`",
        "`levelupWeights.csv`, `ui_layout_title/hud/levelup/pause/result.csv`",
        "`resource_manifest.csv`, `debug_tuning.csv`, `soft_cap_telemetry.csv`",
    ], CW, BLUE)
    footer(c, 7)


def page08(c: canvas.Canvas) -> None:
    y = heading(c, 8, "深掘り2: CSV の弱点と対策", "CSVは編集しやすい反面、型情報が弱い。`1.0x` や `nan` の混入を防ぐため、strict parse とCPU testで境界を固定した。")
    left = (CW - 20) / 2
    draw_text(c, MX, y, "CsvReader.cpp", 10.5, NAVY, FONT_BOLD)
    code(c, MX, y - 17, left, [
        "const float result = std::stof(text, &parsedLength);",
        "if (parsedLength != text.size() || !std::isfinite(result)) {",
        "    throw std::invalid_argument(\"not a finite float\");",
        "}",
    ])
    draw_text(c, MX + left + 20, y, "cpu_regression_checks.cpp", 10.5, NAVY, FONT_BOLD)
    code(c, MX + left + 20, y - 17, left, [
        "RequireThrows([] {",
        "    CsvReader::ParseFloat(\"1.0x\", \"test\");",
        "}, \"float trailing garbage\");",
        "RequireThrows([] {",
        "    CsvReader::ParseFloat(\"nan\", \"test\");",
        "}, \"float nan rejection\");",
    ])
    y -= 150
    evidence_placeholder(c, MX, y, 300, 125, "CSV file screenshot")
    box(c, MX + 322, y, CW - 322, 125, "設計判断", "CSVの弱点を隠さず、parse側で壊れ方を決める。採用理由は「編集しやすさ」であり、型安全性はCPU regression checksで補う。", ORANGE, PAPER, 8.4)
    footer(c, 8)


def page09(c: canvas.Canvas) -> None:
    y = heading(c, 9, "深掘り3: DirectX 12 リソース寿命管理", "CPU側で不要に見えるupload resourceでも、GPUがまだ参照している可能性がある。FrameContextで寿命境界を明示した。")
    left = 420
    code(c, MX, y, left, [
        "struct FrameContext",
        "{",
        "    ComPtr<ID3D12CommandAllocator> commandAllocator;",
        "    ComPtr<ID3D12Resource> uploadArena;",
        "    std::vector<ComPtr<ID3D12Resource>> deferredReleaseResources;",
        "    std::byte* uploadCpuAddress = nullptr;",
        "    size_t uploadOffset = 0;",
        "    uint64_t fenceValue = 0;",
        "};",
    ], 7.0)
    x = MX + left + 24
    node(c, x, y, 120, 38, "Frame 0", BLUE)
    node(c, x + 160, y, 120, 38, "Frame 1", TEAL)
    arrow(c, x + 120, y - 19, x + 160, y - 19)
    box(c, x, y - 70, CW - left - 24, 125, "設計意図", "command allocator, upload arena, deferred release resources, fence value をframeごとに持つ。frame再利用時にGPU完了を確認し、deferredReleaseResourcesをclearする。", BLUE, PAPER, 8.3)
    callout(c, MX, y - 230, CW, "PIX timingは未計測のため、ここでは定量性能値として扱わない。現時点では「壊れにくいリソース寿命設計」として説明する。", RED)
    footer(c, 9)


def page10(c: canvas.Canvas) -> None:
    y = heading(c, 10, "改善履歴 / 問題解決能力", "提出前レビューで見つけた危険箇所を、動作確認だけで済ませず、設計として直した。")
    rows = [
        ("SRVが増え続ける危険", "descriptor ownerが曖昧", "`SrvManager`へ集約、free/high-watermark追加", "scene stressで105/105"),
        ("upload resource寿命が曖昧", "GPU完了前に解放され得る", "`FrameContext` + deferred release", "frame単位で寿命管理"),
        ("CSV不正値の危険", "CSVは型情報が弱い", "strict parse + CPU tests", "`nan`, overflow等を検出"),
        ("乱数の再現性不足", "process global random", "run seedを`GameSession`へ", "debug/testで再現しやすい"),
        ("debug名の冗長化", "`Gameplay*Debug*`が増えた", "`DebugUI::*` namespaceへ整理", "stateless処理を軽量化"),
    ]
    widths = [150, 155, 245, CW - 150 - 155 - 245 - 24]
    x_positions = [MX, MX + widths[0] + 8, MX + widths[0] + widths[1] + 16, MX + widths[0] + widths[1] + widths[2] + 24]
    heads = ["問題", "原因", "修正", "結果"]
    for x, w, head in zip(x_positions, widths, heads):
        box(c, x, y, w, 30, head, "", NAVY, PAPER)
    for r, row in enumerate(rows):
        yy = y - 40 - r * 50
        for i, cell in enumerate(row):
            para(c, x_positions[i] + 6, yy - 10, cell, widths[i] - 12, 7.5, 10.2)
            c.setStrokeColor(LINE)
            c.rect(x_positions[i], yy - 42, widths[i], 42, stroke=1, fill=0)
    footer(c, 10)


def page11(c: canvas.Canvas) -> None:
    y = heading(c, 11, "検証 / 実装の証拠", "資料に載せる数値は、現行プロジェクト内に残っているログとテストに限定する。")
    left = 380
    draw_text(c, MX, y, "scene_transition_stress.txt", 10.5, NAVY, FONT_BOLD)
    code(c, MX, y - 18, left, [
        "status=PASS",
        "targetCycles=3",
        "runCount=3",
        "titleVisits=3",
        "gameVisits=3",
        "resultVisits=3",
        "maxSrvUsed=105",
        "maxSrvHighWatermark=105",
    ])
    box(c, MX + left + 24, y, CW - left - 24, 100, "Scene stress の意味", "`TitleScene -> PlayScene -> ResultScene` を自動遷移し、scene訪問回数とSRV使用量を記録する。descriptor使用量がscene遷移で増え続けないかを見る検証。", GREEN, PAPER, 8.2)
    y -= 160
    box(c, MX, y, 260, 112, "CPU regression checks", "cell key packing\ngauge clamp\nstrict CSV parse\nweapon interval normalization\nrun seed derivation\nstrong SoundHandle", BLUE, colors.white, 7.8)
    box(c, MX + 282, y, 260, 112, "PIX preflight", "status=READY\nPIX installed=True\nCR-012 dynamic texture load latency\nCR-026 shadow pass timing", ORANGE, colors.white, 7.8)
    callout(c, MX + 564, y, CW - 564, "PIXは準備完了まで。GPU pass timeやdraw-call breakdownは未計測なので、提出資料では今後の改善に置く。", RED)
    footer(c, 11)


def page12(c: canvas.Canvas) -> None:
    y = heading(c, 12, "今後の改善 / 提出前に追加する証拠", "現時点の資料は、古い内容を現行コードに合わせて更新した版。最後に、企業提出前に追加すべき証拠を明記する。")
    col = (CW - 24) / 3
    box(c, MX, y, col, 150, "計測を追加", "collision benchmark\n総当たり vs 空間分割\ncandidate数 / narrow phase数\ncollision ms をgame内記録", BLUE, colors.white)
    box(c, MX + col + 12, y, col, 150, "画像を追加", "gameplay画面\nOBB debug画面\nImGui telemetry\nCSV変更反映画面", GREEN, colors.white)
    box(c, MX + (col + 12) * 2, y, col, 150, "DX12証拠を追加", "PIX capture\nshadow pass timing\ndynamic texture load latency\nSRV usage graph", ORANGE, colors.white)
    y -= 185
    draw_text(c, MX, y, "締めの文章", 12, NAVY, FONT_BOLD)
    para(c, MX, y - 24, "KuboEngineでは、描画APIを使うだけでなく、リソース寿命、scene遷移、調整データ、負荷監視、検証を自分で扱いました。次の段階では、実装した設計を計測値と画面証拠でさらに説明できる資料にします。", CW, 9.0, 13)
    footer(c, 12)


PAGES = [page01, page02, page03, page04, page05, page06, page07, page08, page09, page10, page11, page12]


def build() -> None:
    register_fonts()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUT_PDF), pagesize=landscape(A4))
    c.setTitle("プログラム説明資料 A4 改訂版 - KuboEngine")
    c.setAuthor("TAKU OKUBO")
    for i, page in enumerate(PAGES):
        page(c)
        if i != len(PAGES) - 1:
            c.showPage()
    c.save()
    print(OUT_PDF)


if __name__ == "__main__":
    build()
